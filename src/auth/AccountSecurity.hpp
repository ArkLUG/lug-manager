#pragma once
// Passwords, TOTP two-factor, recovery codes, sign-in challenges and
// password-reset links (migration 068). Routes: routes/AccountSecurityRoutes.
#include "auth/Password.hpp"
#include "auth/Totp.hpp"
#include "db/SqliteDatabase.hpp"
#include "utils/Crypto.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class AccountSecurity {
public:
    explicit AccountSecurity(SqliteDatabase& db) : db_(db) {}

    static std::string utc_in(int seconds) {
        std::time_t t = std::time(nullptr) + seconds;
        std::tm tm{};
        gmtime_r(&t, &tm);
        char b[32];
        std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tm);
        return b;
    }
    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    struct Account {
        int64_t id = 0;
        std::string role, display_name, email, password_hash;
        bool two_factor = false;
    };

    std::optional<Account> by_id(int64_t id) {
        auto st = db_.prepare("SELECT id, role, display_name, COALESCE(email,''), password_hash, totp_enabled_at IS NOT NULL "
                              "FROM members WHERE id=?");
        st.bind(1, id);
        if (!st.step()) return std::nullopt;
        return Account{st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_int(5) != 0};
    }

    // The one member whose email this is (case-insensitive). None if no
    // member or several members share it - an ambiguous email can't sign in.
    std::optional<Account> by_email(const std::string& email) {
        std::string e = lower(email);
        if (e.find('@') == std::string::npos || e.size() > 254) return std::nullopt;
        auto st = db_.prepare("SELECT id FROM members WHERE lower(COALESCE(email,''))=? LIMIT 2");
        st.bind(1, e);
        std::vector<int64_t> ids;
        while (st.step()) ids.push_back(st.col_int(0));
        if (ids.size() != 1) return std::nullopt;
        return by_id(ids[0]);
    }

    // Another member already has this email (so it can't be a sign-in email).
    bool email_taken(const std::string& email, int64_t except_id) {
        auto st = db_.prepare("SELECT 1 FROM members WHERE lower(COALESCE(email,''))=? AND id<>? LIMIT 1");
        st.bind(1, lower(email)); st.bind(2, except_id);
        return st.step();
    }

    // ── Passwords ──
    bool set_password(int64_t id, const std::string& pw) {
        std::string h = password::hash(pw);
        if (h.empty()) return false;
        auto st = db_.prepare("UPDATE members SET password_hash=?, password_changed_at=? WHERE id=?");
        st.bind(1, h); st.bind(2, utc_in(0)); st.bind(3, id);
        st.step();
        invalidate_reset_tokens(id);
        return true;
    }
    void clear_password(int64_t id) {
        auto st = db_.prepare("UPDATE members SET password_hash='', password_changed_at=? WHERE id=?");
        st.bind(1, utc_in(0)); st.bind(2, id);
        st.step();
    }
    // Checks the password; re-hashes with current settings when they've changed.
    bool check_password(const Account& a, const std::string& pw) {
        if (a.password_hash.empty()) { password::verify_dummy(pw); return false; }
        if (!password::verify(pw, a.password_hash)) return false;
        if (password::needs_rehash(a.password_hash)) set_password_hash_only(a.id, pw);
        return true;
    }

    // ── Two-factor ──
    std::string begin_totp_setup(int64_t id) {
        std::string s = totp::new_secret();
        auto st = db_.prepare("UPDATE members SET totp_pending_secret=? WHERE id=?");
        st.bind(1, s); st.bind(2, id);
        st.step();
        return s;
    }
    std::string pending_secret(int64_t id) {
        auto st = db_.prepare("SELECT totp_pending_secret FROM members WHERE id=?");
        st.bind(1, id);
        return st.step() ? st.col_text(0) : "";
    }
    // Confirms setup with a code from the app. Returns the new recovery codes
    // (shown once), or empty if the code was wrong.
    std::vector<std::string> enable_totp(int64_t id, const std::string& code) {
        std::string s = pending_secret(id);
        if (s.empty()) return {};
        int64_t step = totp::verify(s, code, std::time(nullptr));
        if (step < 0) return {};
        auto st = db_.prepare("UPDATE members SET totp_secret=totp_pending_secret, totp_pending_secret='', "
                              "totp_enabled_at=?, totp_last_step=? WHERE id=?");
        st.bind(1, utc_in(0)); st.bind(2, step); st.bind(3, id);
        st.step();
        return new_recovery_codes(id);
    }
    void disable_totp(int64_t id) {
        auto st = db_.prepare("UPDATE members SET totp_secret='', totp_pending_secret='', totp_enabled_at=NULL, "
                              "totp_last_step=0 WHERE id=?");
        st.bind(1, id);
        st.step();
        auto rc = db_.prepare("DELETE FROM recovery_codes WHERE member_id=?");
        rc.bind(1, id);
        rc.step();
    }
    // A code from the authenticator app (each accepted once) or an unused
    // recovery code (used up). `used_recovery` says which.
    bool check_second_factor(int64_t id, const std::string& code, bool* used_recovery = nullptr) {
        if (used_recovery) *used_recovery = false;
        std::string secret;
        int64_t last = 0;
        {
            auto st = db_.prepare("SELECT totp_secret, totp_last_step FROM members WHERE id=? AND totp_enabled_at IS NOT NULL");
            st.bind(1, id);
            if (!st.step()) return false;
            secret = st.col_text(0); last = st.col_int(1);
        }
        int64_t step = totp::verify(secret, code, std::time(nullptr), last);
        if (step >= 0) {
            // Compare-and-set, so two requests can't both use the same code.
            auto st = db_.prepare("UPDATE members SET totp_last_step=? WHERE id=? AND totp_last_step<? RETURNING id");
            st.bind(1, step); st.bind(2, id); st.bind(3, step);
            return st.step();
        }
        std::string norm = normalize_recovery(code);
        if (norm.size() != 10) return false;
        auto st = db_.prepare("UPDATE recovery_codes SET used_at=? WHERE id = (SELECT id FROM recovery_codes "
                              "WHERE member_id=? AND code_hash=? AND used_at IS NULL LIMIT 1) RETURNING id");
        st.bind(1, utc_in(0)); st.bind(2, id); st.bind(3, sha256_hex(norm));
        if (!st.step()) return false;
        if (used_recovery) *used_recovery = true;
        return true;
    }
    std::vector<std::string> new_recovery_codes(int64_t id) {
        {
            auto st = db_.prepare("DELETE FROM recovery_codes WHERE member_id=?");
            st.bind(1, id);
            st.step();
        }
        std::vector<std::string> out;
        static const char* A = "abcdefghjkmnpqrstuvwxyz23456789";   // no 0/o/1/l/i
        for (int n = 0; n < 10; ++n) {
            std::string hex = generate_random_hex(10), code;
            for (size_t i = 0; i < 10; ++i) code += A[std::stoi(hex.substr(i * 2, 2), nullptr, 16) % 31];
            auto st = db_.prepare("INSERT INTO recovery_codes (member_id, code_hash) VALUES (?,?)");
            st.bind(1, id); st.bind(2, sha256_hex(code));
            st.step();
            out.push_back(code.substr(0, 5) + "-" + code.substr(5));
        }
        return out;
    }
    int recovery_codes_left(int64_t id) {
        auto st = db_.prepare("SELECT COUNT(*) FROM recovery_codes WHERE member_id=? AND used_at IS NULL");
        st.bind(1, id);
        return st.step() ? static_cast<int>(st.col_int(0)) : 0;
    }

    // ── Who must use two-factor (setting auth_require_2fa) ──
    static bool role_requires(const std::string& setting, const std::string& role) {
        if (setting == "everyone") return true;
        if (setting == "staff") return role != "member";
        return false;
    }

    // ── Sign-in challenges (password ok, code still needed) ──
    std::string create_challenge(int64_t id, const std::string& method, const std::string& next) {
        {
            auto st = db_.prepare("DELETE FROM login_challenges WHERE expires_at < ?");
            st.bind(1, utc_in(0));
            st.step();
        }
        std::string token = generate_random_hex(32);
        auto st = db_.prepare("INSERT INTO login_challenges (token_hash, member_id, method, next_path, expires_at) VALUES (?,?,?,?,?)");
        st.bind(1, sha256_hex(token)); st.bind(2, id); st.bind(3, method); st.bind(4, next); st.bind(5, utc_in(300));
        st.step();
        return token;
    }
    struct Challenge { int64_t member_id = 0; std::string method, next; int attempts = 0; };
    std::optional<Challenge> challenge(const std::string& token) {
        if (token.size() != 64) return std::nullopt;
        auto st = db_.prepare("SELECT member_id, method, next_path, attempts FROM login_challenges "
                              "WHERE token_hash=? AND expires_at > ? AND attempts < 5");
        st.bind(1, sha256_hex(token)); st.bind(2, utc_in(0));
        if (!st.step()) return std::nullopt;
        return Challenge{st.col_int(0), st.col_text(1), st.col_text(2), static_cast<int>(st.col_int(3))};
    }
    void challenge_failed(const std::string& token) {
        auto st = db_.prepare("UPDATE login_challenges SET attempts=attempts+1 WHERE token_hash=?");
        st.bind(1, sha256_hex(token));
        st.step();
    }
    void challenge_done(const std::string& token) {
        auto st = db_.prepare("DELETE FROM login_challenges WHERE token_hash=?");
        st.bind(1, sha256_hex(token));
        st.step();
    }

    // ── Password reset / set-up links ──
    std::string create_reset_token(int64_t id, int64_t created_by, int hours) {
        std::string token = generate_random_hex(32);
        auto st = db_.prepare("INSERT INTO password_reset_tokens (token_hash, member_id, created_by, expires_at) VALUES (?,?,?,?)");
        st.bind(1, sha256_hex(token)); st.bind(2, id);
        if (created_by > 0) st.bind(3, created_by); else st.bind_null(3);
        st.bind(4, utc_in(hours * 3600));
        st.step();
        return token;
    }
    std::optional<int64_t> reset_token_member(const std::string& token) {
        if (token.size() != 64) return std::nullopt;
        auto st = db_.prepare("SELECT member_id FROM password_reset_tokens WHERE token_hash=? AND used_at IS NULL AND expires_at > ?");
        st.bind(1, sha256_hex(token)); st.bind(2, utc_in(0));
        if (!st.step()) return std::nullopt;
        return st.col_int(0);
    }
    bool use_reset_token(const std::string& token) {
        auto st = db_.prepare("UPDATE password_reset_tokens SET used_at=? WHERE token_hash=? AND used_at IS NULL AND expires_at > ? RETURNING member_id");
        st.bind(1, utc_in(0)); st.bind(2, sha256_hex(token)); st.bind(3, utc_in(0));
        return st.step();
    }
    int recent_reset_tokens(int64_t id) {
        auto st = db_.prepare("SELECT COUNT(*) FROM password_reset_tokens WHERE member_id=? AND created_by IS NULL "
                              "AND created_at > datetime('now','-1 hour')");
        st.bind(1, id);
        return st.step() ? static_cast<int>(st.col_int(0)) : 0;
    }
    // Email changed, password set, or 2FA reset: links that were sent are void.
    void invalidate_reset_tokens(int64_t id) {
        auto st = db_.prepare("UPDATE password_reset_tokens SET used_at=? WHERE member_id=? AND used_at IS NULL");
        st.bind(1, utc_in(0)); st.bind(2, id);
        st.step();
        auto el = db_.prepare("UPDATE email_login_tokens SET used_at=? WHERE member_id=? AND used_at IS NULL");
        el.bind(1, utc_in(0)); el.bind(2, id);
        el.step();
    }

private:
    void set_password_hash_only(int64_t id, const std::string& pw) {
        std::string h = password::hash(pw);
        if (h.empty()) return;
        auto st = db_.prepare("UPDATE members SET password_hash=? WHERE id=?");
        st.bind(1, h); st.bind(2, id);
        st.step();
    }
    static std::string normalize_recovery(const std::string& code) {
        std::string out;
        for (char c : code) if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    SqliteDatabase& db_;
};

// Simple in-memory sliding-window limiter for sign-in attempts. Keys are
// "ip:<addr>" and "acct:<email>", so unknown emails are limited too (no
// hint about which addresses exist). Resets on restart, which is fine.
class AttemptLimiter {
public:
    AttemptLimiter(size_t max, int window_seconds) : max_(max), window_(window_seconds) {}
    bool allowed(const std::string& key) {
        std::lock_guard<std::mutex> l(mu_);
        auto& q = hits_[key];
        prune(q);
        return q.size() < max_;
    }
    void hit(const std::string& key) {
        std::lock_guard<std::mutex> l(mu_);
        auto& q = hits_[key];
        prune(q);
        q.push_back(std::time(nullptr));
        if (hits_.size() > 50000) hits_.clear();
    }
    void clear(const std::string& key) {
        std::lock_guard<std::mutex> l(mu_);
        hits_.erase(key);
    }
    void reset() {
        std::lock_guard<std::mutex> l(mu_);
        hits_.clear();
    }

private:
    void prune(std::deque<std::time_t>& q) {
        std::time_t now = std::time(nullptr);
        while (!q.empty() && now - q.front() > window_) q.pop_front();
    }
    size_t max_;
    int window_;
    std::mutex mu_;
    std::map<std::string, std::deque<std::time_t>> hits_;
};
