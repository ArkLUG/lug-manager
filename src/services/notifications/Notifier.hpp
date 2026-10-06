#pragma once
#include "db/SqliteDatabase.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/email/Mailer.hpp"
#include "services/SiteSettings.hpp"
#include "repositories/members/NotificationPrefs.hpp"
#include "auth/SessionStore.hpp"
#include "chat/ChatHub.hpp"
#include <crow/json.h>
#include <ctime>
#include <functional>
#include <memory>
#include <vector>
#include <string>

// Sends members notifications (see notify()): a chat DM where possible, else
// email. Every email carries a no-login unsubscribe link and List-Unsubscribe
// headers.
class Notifier {
public:
    Notifier(SqliteDatabase& db, DiscordClient& discord, std::shared_ptr<Mailer> mailer, std::string public_url)
        : db_(db), discord_(discord), mailer_(std::move(mailer)), public_url_(std::move(public_url)) {}

    bool email_enabled() const { return mailer_ && mailer_->enabled() && !url().empty(); }

    void set_chat(std::shared_ptr<chat::ChatHub> hub) { chat_owner_ = hub; chat_ = hub.get(); }
    chat::ChatHub* chat() const { return chat_; }

    // Sends message template `key` to a member as a NotificationPrefs `kind`:
    // a direct message on a chat service where they have an account and DMs
    // are on, otherwise an email (when SMTP and the public URL are set up and
    // they haven't turned email off). {name} is filled in. async: on the
    // integration worker pool (request handlers); returns true once queued.
    // What a reminder is about, for the buttons on its chat DM: `ref` (event
    // id, shift signup id, loan id...) and the page to open ("/events/12").
    struct About { std::string ref; std::string link; std::string link_label; };

    // `ics`, when given, is attached to the email as a calendar file (a chat
    // DM has the item's link instead). `about` adds buttons to a chat DM:
    // the item's page, plus (when the service can send clicks back) Remind me
    // later, Don't remind me, and the kind's own action (Can't make it...).
    bool notify(int64_t member_id, const std::string& kind, const std::string& key, chat::Values v, bool async = false,
                std::string ics = "", About about = {}) {
        NotificationPrefs prefs(db_);
        if (!prefs.wants(member_id, kind)) return false;
        std::string email, name;
        {
            // Unconfirmed emails (typed in, link not clicked yet) get nothing.
            auto st = db_.prepare("SELECT CASE WHEN email_confirmed=1 THEN COALESCE(email,'') ELSE '' END, display_name FROM members WHERE id=?");
            st.bind(1, member_id);
            if (!st.step()) return false;
            email = st.col_text(0); name = st.col_text(1);
        }
        if (!v.count("name")) v["name"] = name;
        auto deliver = [this, member_id, kind, key, v, email, name, ics, about]() {
            if (chat_ && chat_->can_dm(member_id)) {
                const int64_t row = record_dm(member_id, kind, about.ref, key, v);
                if (chat_->direct_message(member_id, key, v, buttons(kind, row, about))) return true;
                forget_dm(row);
            }
            NotificationPrefs p(db_);
            if (email.empty() || !email_enabled() || !p.wants(member_id, "email")) return false;
            chat::TemplateStore t(db_);
            send_email(member_id, email, name, kind, t.render_subject(key, v), t.render_body(key, v), ics);
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
                    const std::string& subject, const std::string& text, const std::string& ics = "") {
        if (!mailer_ || !mailer_->enabled()) return;
        const std::string base = url();
        std::string unsub = base.empty() ? "" : base + "/unsubscribe/" + email_token(db_, member_id) +
                                                         (kind.empty() ? "" : "?kind=" + kind);
        std::string body = "Hi " + name + ",\n\n" + strip_markdown(text) + "\n";
        if (!unsub.empty())
            body += "\n--\nYou're getting this because you're a member of the LUG. "
                    "Stop these emails (no login needed): " + unsub + "\n";
        mailer_->send({email, subject, body, unsub, ics});
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

    // Kinds whose DMs offer "Remind me later" (the rest: digest, waitlist).
    static bool snoozable(const std::string& kind) {
        return kind == "event_reminder" || kind == "meeting_reminder" || kind == "shift_reminder" ||
               kind == "dues_reminder" || kind == "loan_reminder";
    }

    // Buttons for a reminder DM (row = its reminder_dms id, 0 = links only).
    std::vector<chat::Button> buttons(const std::string& kind, int64_t row, const About& about) const {
        std::vector<chat::Button> b;
        const std::string base = url();
        if (!about.link.empty() && !base.empty())
            b.push_back({about.link_label.empty() ? "Open" : about.link_label, base + about.link, "", ""});
        if (row <= 0 || !actions_available()) return b;
        const std::string id = std::to_string(row);
        if (kind == "event_reminder") b.push_back({"Can't make it", "", "lm:rsvp_off:" + id, "danger"});
        if (kind == "meeting_reminder") b.push_back({"Can't make it", "", "lm:mrsvp_off:" + id, "danger"});
        if (kind == "waitlist")       b.push_back({"Give up my spot", "", "lm:rsvp_off:" + id, "danger"});
        if (kind == "shift_reminder") b.push_back({"Can't make my shift", "", "lm:shift_off:" + id, "danger"});
        if (snoozable(kind))          b.push_back({"Remind me later", "", "lm:snooze:" + id, "secondary"});
        b.push_back({"Don't remind me", "", "lm:mute:" + id, "secondary"});
        return b;
    }

    // Whether clicks on DM buttons reach us (Discord's interactions endpoint
    // is set up); set at start-up. Without it DMs get link buttons only.
    void set_actions_available(std::function<bool()> f) { actions_available_ = std::move(f); }
    bool actions_available() const { return actions_available_ && actions_available_(); }

    // "Remind me later": reminders snoozed until now or earlier, sent again
    // (once each). Called from the reminder loop.
    int send_snoozed(std::time_t now = std::time(nullptr)) {
        struct Due { int64_t id, member; std::string kind, ref, key, values; };
        std::vector<Due> due;
        {
            auto st = db_.prepare("SELECT id, member_id, kind, ref, template_key, values_json FROM reminder_dms "
                                  "WHERE snooze_until IS NOT NULL AND resent = 0 AND snooze_until <= ?");
            st.bind(1, static_cast<int64_t>(now));
            while (st.step()) due.push_back({st.col_int(0), st.col_int(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_text(5)});
        }
        int n = 0;
        for (const auto& d : due) {
            auto up = db_.prepare("UPDATE reminder_dms SET resent = 1 WHERE id = ? AND resent = 0 RETURNING id");
            up.bind(1, d.id);
            if (!up.step()) continue;   // claimed elsewhere
            up.reset();
            chat::Values v;
            if (auto j = crow::json::load(d.values); j && j.t() == crow::json::type::Object)
                for (const auto& k : j) v[k.key()] = k.t() == crow::json::type::String ? std::string(k.s()) : "";
            if (notify(d.member, d.kind, d.key, v, false, "", about_for(d.kind, d.ref))) ++n;
        }
        return n;
    }

    // The page a reminder kind links to, from its ref.
    static About about_for(const std::string& kind, const std::string& ref) {
        if ((kind == "event_reminder" || kind == "waitlist" || kind == "shift_reminder") && !ref.empty()) {
            const auto colon = ref.find(':');   // shift refs are "<signup>:<event>"
            return {ref, "/events/" + (colon == std::string::npos ? ref : ref.substr(colon + 1)), "View event"};
        }
        if (kind == "meeting_reminder" && !ref.empty()) return {ref, "/meetings/" + ref, "View meeting"};
        if (kind == "dues_reminder") return {ref, "/account", "My dues"};
        if (kind == "loan_reminder") return {ref, "/inventory", "LUG inventory"};
        if (kind == "digest") return {ref, "/schedule", "Schedule"};
        return {ref, "", ""};
    }

    Mailer* mailer() const { return mailer_.get(); }
    std::string timezone() const { return discord_.get_timezone(); }

private:
    int64_t record_dm(int64_t member_id, const std::string& kind, const std::string& ref, const std::string& key,
                      const chat::Values& v) {
        crow::json::wvalue j;
        for (const auto& [k, val] : v) j[k] = val;
        auto st = db_.prepare("INSERT INTO reminder_dms (member_id, kind, ref, template_key, values_json) VALUES (?,?,?,?,?) RETURNING id");
        st.bind(1, member_id); st.bind(2, kind); st.bind(3, ref); st.bind(4, key); st.bind(5, j.dump());
        int64_t id = st.step() ? st.col_int(0) : 0;
        st.reset();
        return id;
    }
    void forget_dm(int64_t id) {
        if (id <= 0) return;
        auto st = db_.prepare("DELETE FROM reminder_dms WHERE id=?");
        st.bind(1, id);
        st.step();
    }
    std::function<bool()> actions_available_;
    SqliteDatabase& db_;
    DiscordClient& discord_;
    std::shared_ptr<Mailer> mailer_;
    std::string public_url_;   // given (tests); else the site's (Settings > Email & address)
    std::string url() const { return public_url_.empty() ? site::public_url() : public_url_; }
    std::shared_ptr<chat::ChatHub> chat_owner_;
    chat::ChatHub* chat_ = nullptr;
};
