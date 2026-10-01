#pragma once
#include "db/SqliteDatabase.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/email/Mailer.hpp"
#include "repositories/members/NotificationPrefs.hpp"
#include "auth/SessionStore.hpp"
#include "chat/ChatHub.hpp"
#include <memory>
#include <string>

// Sends members notifications (see notify()): a chat DM where possible, else
// email. Every email carries a no-login unsubscribe link and List-Unsubscribe
// headers.
class Notifier {
public:
    Notifier(SqliteDatabase& db, DiscordClient& discord, std::shared_ptr<Mailer> mailer, std::string public_url)
        : db_(db), discord_(discord), mailer_(std::move(mailer)), public_url_(std::move(public_url)) {}

    bool email_enabled() const { return mailer_ && mailer_->enabled() && !public_url_.empty(); }

    void set_chat(std::shared_ptr<chat::ChatHub> hub) { chat_owner_ = hub; chat_ = hub.get(); }
    chat::ChatHub* chat() const { return chat_; }

    // Sends message template `key` to a member as a NotificationPrefs `kind`:
    // a direct message on a chat service where they have an account and DMs
    // are on, otherwise an email (when SMTP and the public URL are set up and
    // they haven't turned email off). {name} is filled in. async: on the
    // integration worker pool (request handlers); returns true once queued.
    bool notify(int64_t member_id, const std::string& kind, const std::string& key, chat::Values v, bool async = false) {
        NotificationPrefs prefs(db_);
        if (!prefs.wants(member_id, kind)) return false;
        std::string email, name;
        {
            auto st = db_.prepare("SELECT COALESCE(email,''), display_name FROM members WHERE id=?");
            st.bind(1, member_id);
            if (!st.step()) return false;
            email = st.col_text(0); name = st.col_text(1);
        }
        if (!v.count("name")) v["name"] = name;
        auto deliver = [this, member_id, kind, key, v, email, name]() {
            if (chat_ && chat_->direct_message(member_id, key, v)) return true;
            NotificationPrefs p(db_);
            if (email.empty() || !email_enabled() || !p.wants(member_id, "email")) return false;
            chat::TemplateStore t(db_);
            send_email(member_id, email, name, kind, t.render_subject(key, v), t.render_body(key, v));
            return true;
        };
        if (async && chat_ && chat_->can_dm(member_id)) { discord_.run_async(deliver); return true; }
        return deliver();
    }

    // Emails template `key` (sign-in emails) - no preference check.
    void send_email_template(int64_t member_id, const std::string& email, const std::string& name,
                             const std::string& key, chat::Values v) {
        if (!v.count("name")) v["name"] = name;
        chat::TemplateStore t(db_);
        send_email(member_id, email, name, "", t.render_subject(key, v), t.render_body(key, v));
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
    std::shared_ptr<chat::ChatHub> chat_owner_;
    chat::ChatHub* chat_ = nullptr;
};
