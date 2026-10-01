#include "auth/SessionStore.hpp"
#include "utils/Crypto.hpp"
#include <vector>
#include <openssl/rand.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string now_plus_hours(int h) {
    auto tp = std::chrono::system_clock::now() + std::chrono::hours(h);
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

static std::string now_iso() {
    auto tp = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// Returns true if expires_at (ISO 8601 UTC, no Z) is in the past relative to now.
static bool is_expired(const std::string& expires_at) {
    if (expires_at.empty()) return true;
    return expires_at <= now_iso();
}

// ---------------------------------------------------------------------------
// SessionStore
// ---------------------------------------------------------------------------

// Overwrites the session's role/display name with the member's current values.
// Returns false if the member no longer exists. Done on every lookup (one PK
// query) so a role change or member deletion takes effect immediately instead
// of lingering for the session's 24h lifetime.
static bool refresh_from_member(SqliteDatabase& db, Session& s) {
    auto stmt = db.prepare("SELECT role, display_name FROM members WHERE id=?");
    stmt.bind(1, s.member_id);
    if (!stmt.step()) return false;
    s.role         = stmt.col_text(0);
    s.display_name = stmt.col_text(1);
    return true;
}

SessionStore::SessionStore(SqliteDatabase& db) : db_(db) {
    // The sessions table comes from the migrations (001, 045). Hash any
    // tokens stored raw by older versions - see 045_hash_session_tokens.sql.
    Transaction tx(db_);
    std::vector<std::string> raw_tokens;
    {
        auto stmt = db_.prepare("SELECT token FROM sessions WHERE token_is_hash=0");
        while (stmt.step()) raw_tokens.push_back(stmt.col_text(0));
    }
    for (const auto& t : raw_tokens) {
        auto upd = db_.prepare("UPDATE sessions SET token=?, token_is_hash=1 WHERE token=?");
        upd.bind(1, sha256_hex(t));
        upd.bind(2, t);
        upd.step();
    }
    tx.commit();
}

std::string SessionStore::generate_token() {
    unsigned char buf[32];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        throw std::runtime_error("RAND_bytes failed: cannot generate session token");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str(); // 64-char lowercase hex string
}

std::string SessionStore::create(int64_t member_id, const std::string& role,
                                  const std::string& display_name, int hours,
                                  const std::string& user_agent) {
    std::string token      = generate_token();
    std::string expires_at = now_plus_hours(hours);
    std::string created_at = now_iso();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto stmt = db_.prepare(
            "INSERT INTO sessions (token, member_id, role, expires_at, created_at, token_is_hash, user_agent) "
            "VALUES (?, ?, ?, ?, ?, 1, ?)");
        stmt.bind(1, sha256_hex(token)) // only the hash is persisted
            .bind(2, member_id)
            .bind(3, role)
            .bind(4, expires_at)
            .bind(5, created_at)
            .bind(6, user_agent.substr(0, 300));
        stmt.step();

        Session s;
        s.token        = token;
        s.member_id    = member_id;
        s.role         = role;
        s.display_name = display_name;
        s.expires_at   = expires_at;
        s.created_at   = created_at;
        cache_[token]  = s;
    }
    return token;
}

std::optional<Session> SessionStore::find(const std::string& token) {
    if (token.empty()) return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check in-memory cache first
    auto it = cache_.find(token);
    if (it != cache_.end()) {
        if (is_expired(it->second.expires_at)) {
            // Expired: remove from cache and DB
            cache_.erase(it);
            auto del = db_.prepare("DELETE FROM sessions WHERE token = ?");
            del.bind(1, sha256_hex(token));
            del.step();
            return std::nullopt;
        }
        if (!refresh_from_member(db_, it->second)) {
            cache_.erase(it);
            return std::nullopt;
        }
        return it->second;
    }

    // Fall back to database
    auto stmt = db_.prepare(
        "SELECT token, member_id, role, expires_at, created_at "
        "FROM sessions WHERE token = ?");
    stmt.bind(1, sha256_hex(token));

    if (!stmt.step()) return std::nullopt;

    Session s;
    s.token      = token; // DB holds only the hash; callers keep the raw token
    s.member_id  = stmt.col_int(1);
    s.role       = stmt.col_text(2);
    s.expires_at = stmt.col_text(3);
    s.created_at = stmt.col_text(4);

    if (is_expired(s.expires_at)) {
        // Remove expired session
        auto del = db_.prepare("DELETE FROM sessions WHERE token = ?");
        del.bind(1, sha256_hex(token));
        del.step();
        return std::nullopt;
    }

    if (!refresh_from_member(db_, s)) return std::nullopt;
    cache_[s.token] = s;
    return s;
}

void SessionStore::remove(const std::string& token) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(token);
    auto stmt = db_.prepare("DELETE FROM sessions WHERE token = ?");
    stmt.bind(1, sha256_hex(token));
    stmt.step();
}

void SessionStore::purge_expired() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove expired sessions from DB
    // expires_at is stored as YYYY-MM-DDTHH:MM:SS; compare in the same format
    // (datetime('now') uses a space, which never sorts below 'T' same-day).
    auto stmt = db_.prepare("DELETE FROM sessions WHERE expires_at < strftime('%Y-%m-%dT%H:%M:%S','now')");
    stmt.step();

    // Clean up expired entries from in-memory cache
    std::string now = now_iso();
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->second.expires_at <= now) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<SessionStore::Info> SessionStore::list_for_member(int64_t member_id,
                                                              const std::string& current_token) {
    std::string current_hash = current_token.empty() ? "" : sha256_hex(current_token);
    std::lock_guard<std::mutex> lock(mutex_);
    auto stmt = db_.prepare(
        "SELECT token, created_at, expires_at, user_agent FROM sessions "
        "WHERE member_id=? AND expires_at > strftime('%Y-%m-%dT%H:%M:%S','now') ORDER BY created_at DESC");
    stmt.bind(1, member_id);
    std::vector<Info> out;
    while (stmt.step()) {
        Info i;
        std::string hash = stmt.col_text(0);
        i.id         = hash.substr(0, 12);
        i.created_at = stmt.col_text(1);
        i.expires_at = stmt.col_text(2);
        i.user_agent = stmt.col_text(3);
        i.current    = !current_hash.empty() && hash == current_hash;
        out.push_back(std::move(i));
    }
    return out;
}

int SessionStore::remove_all_for_member(int64_t member_id, const std::string& keep_token) {
    std::string keep_hash = keep_token.empty() ? "" : sha256_hex(keep_token);
    std::lock_guard<std::mutex> lock(mutex_);
    auto stmt = db_.prepare("DELETE FROM sessions WHERE member_id=? AND token<>? RETURNING token");
    stmt.bind(1, member_id);
    stmt.bind(2, keep_hash);
    int n = 0;
    while (stmt.step()) ++n;
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->second.member_id == member_id && it->first != keep_token) it = cache_.erase(it);
        else ++it;
    }
    return n;
}
