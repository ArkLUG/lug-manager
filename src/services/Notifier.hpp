#pragma once
#include "db/SqliteDatabase.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/Mailer.hpp"
#include "repositories/NotificationPrefs.hpp"
#include "auth/SessionStore.hpp"
#include <memory>
#include <string>

// Sends a member a notification of one NotificationPrefs kind: a Discord DM
// when they have Discord linked, otherwise an email (when SMTP and the public
// URL are configured and they haven't turned email off). Every email carries
// a no-login unsubscribe link and List-Unsubscribe headers.
class Notifier {
public:
    Notifier(SqliteDatabase& db, DiscordClient& discord, std::shared_ptr<Mailer> mailer, std::string public_url)
        : db_(db), discord_(discord), mailer_(std::move(mailer)), public_url_(std::move(public_url)) {}

    bool email_enabled() const { return mailer_ && mailer_->enabled() && !public_url_.empty(); }

    // async: DM on the Discord worker pool (request handlers). Returns true if
    // something was sent or queued.
    bool notify(int64_t member_id, const std::string& kind, const std::string& subject,
                const std::string& text, bool async = false) {
        NotificationPrefs prefs(db_);
        if (!prefs.wants(member_id, kind)) return false;
        std::string discord_id, email, name;
        {
            auto st = db_.prepare("SELECT COALESCE(discord_user_id,''), COALESCE(email,''), display_name FROM members WHERE id=?");
            st.bind(1, member_id);
            if (!st.step()) return false;
            discord_id = st.col_text(0); email = st.col_text(1); name = st.col_text(2);
        }
        if (!discord_id.empty()) {
            if (async) { discord_.send_dm_async(discord_id, text); return true; }
            return discord_.send_dm(discord_id, text);
        }
        if (email.empty() || !email_enabled() || !prefs.wants(member_id, "email")) return false;
        send_email(member_id, email, name, kind, subject, text);
        return true;
    }

    // Emails a member directly (sign-in links etc.) - no preference check.
    void send_email(int64_t member_id, const std::string& email, const std::string& name, const std::string& kind,
                    const std::string& subject, const std::string& text) {
        if (!mailer_ || !mailer_->enabled()) return;
        std::string unsub = public_url_.empty() ? "" : public_url_ + "/unsubscribe/" + email_token(db_, member_id) +
                                                         (kind.empty() ? "" : "?kind=" + kind);
        std::string body = "Hi " + name + ",\n\n" + strip_markdown(text) + "\n";
        if (!unsub.empty())
            body += "\n--\nYou're getting this because you're a member of the LUG. "
                    "Stop these emails (no login needed): " + unsub + "\n";
        mailer_->send({email, subject, body, unsub});
    }

    // The member's unsubscribe token, created on first use.
    static std::string email_token(SqliteDatabase& db, int64_t member_id) {
        auto st = db.prepare("SELECT email_token FROM members WHERE id=?");
        st.bind(1, member_id);
        if (st.step() && !st.col_text(0).empty()) return st.col_text(0);
        st.reset();
        std::string token = SessionStore::generate_token().substr(0, 40);
        auto up = db.prepare("UPDATE members SET email_token=? WHERE id=? AND email_token=''");
        up.bind(1, token); up.bind(2, member_id);
        up.step();
        up.reset();
        auto again = db.prepare("SELECT email_token FROM members WHERE id=?");
        again.bind(1, member_id);
        return again.step() ? again.col_text(0) : token;
    }

    // Discord-flavoured text -> plain email text ("**x**" -> "x").
    static std::string strip_markdown(std::string s) {
        for (const char* t : {"**", "__"})
            for (size_t p; (p = s.find(t)) != std::string::npos;) s.erase(p, 2);
        return s;
    }

    Mailer* mailer() const { return mailer_.get(); }
    std::string timezone() const { return discord_.get_timezone(); }

private:
    SqliteDatabase& db_;
    DiscordClient& discord_;
    std::shared_ptr<Mailer> mailer_;
    std::string public_url_;
};
