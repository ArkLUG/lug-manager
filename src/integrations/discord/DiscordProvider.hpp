#pragma once
// Discord as a chat::Provider, on top of DiscordClient. Channels and roles
// come from Settings > Discord (DiscordClient's configuration) and from each
// chapter's Discord channel/role.
#include "chat/Provider.hpp"
#include "db/SqliteDatabase.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/Features.hpp"

namespace chat {

class DiscordProvider : public Provider {
public:
    DiscordProvider(DiscordClient& discord, SqliteDatabase& db) : discord_(discord), db_(db) {}

    std::string id() const override { return "discord"; }
    std::string name() const override { return "Discord"; }
    bool ready() const override { return Features::on("discord") && !discord_.get_guild_id().empty(); }
    Caps caps() const override { return Caps{true, true, true, true, true}; }
    std::optional<bool> own_switch(const std::string& name) const override {
        if (name == "pings") return !discord_.get_suppress_pings();
        if (name == "update_notes") return !discord_.get_suppress_updates();
        return std::nullopt;
    }

    std::string place(Place p) const override {
        switch (p) {
            case Place::Announcements:  return discord_.get_lug_channel_id();
            case Place::EventsForum:    return discord_.get_events_forum_channel_id();
            case Place::EventReports:   return discord_.get_event_reports_forum_id();
            case Place::MeetingReports: return discord_.get_meeting_reports_forum_id();
            case Place::Reviews:        return setting("discord_matches_notification_channel_id");
        }
        return "";
    }
    std::string chapter_channel(int64_t chapter_id) const override {
        return chapter_col(chapter_id, "discord_announcement_channel_id");
    }
    std::string chapter_role(int64_t chapter_id) const override {
        return chapter_col(chapter_id, "discord_member_role_id");
    }
    std::string announcement_role(bool non_lug) const override {
        return non_lug ? discord_.get_non_lug_event_role_id() : discord_.get_announcement_role_id();
    }

    std::string role_mention(const std::string& role) const override { return role.empty() ? "" : "<@&" + role + ">"; }
    std::string user_mention(const std::string& user) const override { return user.empty() ? "" : "<@" + user + ">"; }
    std::string thread_url(const std::string& thread) const override {
        if (thread.empty() || discord_.get_guild_id().empty()) return "";
        return "https://discord.com/channels/" + discord_.get_guild_id() + "/" + thread;
    }
    std::string inert(const std::string& text) const override {
        // A zero-width space after "<" / "@" breaks <@id>, <@&id>, <#id>, @everyone and @here.
        static const std::string zw = "​";
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            out += text[i];
            if (text[i] == '<' && i + 1 < text.size() && (text[i + 1] == '@' || text[i + 1] == '#')) out += zw;
            else if (text[i] == '@' && (text.compare(i + 1, 8, "everyone") == 0 || text.compare(i + 1, 4, "here") == 0)) out += zw;
        }
        return out;
    }

    Result post(const std::string& channel, const Message& m) override {
        return conv(discord_.send_message(channel, m.text, m.roles, m.users));
    }
    Result edit(const std::string& channel, const std::string& message, const Message& m) override {
        return conv(discord_.edit_message(channel, message, m.text, m.roles, m.users));
    }
    Result remove(const std::string& channel, const std::string& message) override {
        return conv(discord_.remove_message(channel, message));
    }
    Result start_forum_thread(const std::string& forum, const std::string& title, const Message& first) override {
        return conv(discord_.start_forum_thread(forum, title, first.text, first.roles, first.users));
    }
    Result start_thread(const std::string& channel, const std::string& message, const std::string& title) override {
        return conv(discord_.start_thread_from_message(channel, message, title));
    }
    Result rename_thread(const std::string& thread, const std::string& title) override {
        return conv(discord_.rename_thread(thread, title));
    }
    // A forum thread's first message has the thread's id.
    Result edit_thread_starter(const std::string& thread, const Message& m) override {
        return conv(discord_.edit_message(thread, thread, m.text, m.roles, m.users));
    }
    Result remove_thread(const std::string& thread) override { return conv(discord_.remove_channel(thread)); }

    Result create_event(const ScheduledEvent& e) override { return conv(discord_.create_scheduled(to(e))); }
    Result update_event(const std::string& id, const ScheduledEvent& e) override { return conv(discord_.update_scheduled(id, to(e))); }
    Result remove_event(const std::string& id) override { return conv(discord_.remove_scheduled(id)); }

    std::string member_account(int64_t member_id) const override {
        auto st = db_.prepare("SELECT COALESCE(discord_user_id,'') FROM members WHERE id=?");
        st.bind(1, member_id);
        return st.step() ? st.col_text(0) : "";
    }
    Result direct_message(const std::string& account, const Message& m) override {
        return conv(discord_.direct_message(account, m.text));
    }

    DiscordClient& client() { return discord_; }

private:
    static Result conv(const DiscordClient::Result& r) { return Result{r.ok, r.id, r.error}; }
    static DiscordClient::ScheduledEvent to(const ScheduledEvent& e) {
        return DiscordClient::ScheduledEvent{e.name, e.description, e.location, e.start_local, e.end_local};
    }
    std::string setting(const char* key) const {
        auto st = db_.prepare("SELECT value FROM lug_settings WHERE key=?");
        st.bind(1, std::string(key));
        return st.step() ? st.col_text(0) : "";
    }
    std::string chapter_col(int64_t chapter_id, const char* col) const {
        if (chapter_id <= 0) return "";
        auto st = db_.prepare(std::string("SELECT COALESCE(") + col + ",'') FROM chapters WHERE id=?");
        st.bind(1, chapter_id);
        return st.step() ? st.col_text(0) : "";
    }

    DiscordClient& discord_;
    SqliteDatabase& db_;
};

} // namespace chat
