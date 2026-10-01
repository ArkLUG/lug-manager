#pragma once
// Decides what LUG Manager posts to chat services and sends it through each
// registered chat::Provider: event and meeting announcements, threads and
// scheduled events (kept in step as they change), reminders, reports,
// challenge winners and direct messages. What gets posted is controlled by
// per-provider switches, per-meeting/event "don't post" options and "quiet
// mode"; the wording comes from the message templates. Everything sent,
// edited, deleted or refused is written to the chat activity log.
#include "chat/Provider.hpp"
#include "chat/Templates.hpp"
#include "db/SqliteDatabase.hpp"
#include "models/LugEvent.hpp"
#include "models/Meeting.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace chat {

class ChatHub {
public:
    ChatHub(SqliteDatabase& db, std::string public_url) : db_(db), public_url_(std::move(public_url)) {}

    void add(std::shared_ptr<Provider> p) { providers_.push_back(std::move(p)); }
    const std::vector<std::shared_ptr<Provider>>& providers() const { return providers_; }
    Provider* provider(const std::string& id) const;
    TemplateStore templates() { return TemplateStore(db_); }

    // A provider's switch (see migration 069), e.g. "event_thread". Pings and
    // update notes keep Discord's own settings.
    bool switch_on(const Provider& p, const std::string& name) const;
    void set_switch(const std::string& provider, const std::string& name, bool on);
    bool quiet() const;

    // Per-item options: parts not to post (announce, chapter_announce, thread,
    // scheduled, update_note). "\x01" = leave as is.
    std::set<std::string> skipped(const std::string& entity_type, int64_t id) const;
    void set_skipped(const std::string& entity_type, int64_t id, const std::string& csv);

    // ── Events ──
    void event_published(const LugEvent& e);
    void event_changed(const LugEvent& before, const LugEvent& after, bool notify);
    // `thread_owned`: whether the app made the event's thread (read it before
    // deleting the row - removal may run after the row is gone).
    void event_removed(const LugEvent& e, bool thread_owned = true);

    // ── Meetings ──
    void meeting_published(const Meeting& m);
    void meeting_changed(const Meeting& before, const Meeting& after);
    void meeting_removed(const Meeting& m);

    // ── Standalone posts ── (return how many providers it went to)
    int post_to(Place place, int64_t chapter_id, const std::string& key, const Values& v,
                const std::string& entity_type = "", int64_t entity_id = 0, const std::string& switch_name = "");
    // Post into a specific channel/thread of one provider (event reminders go to the event's thread).
    Result post_in(Provider& p, const std::string& channel, const std::string& key, const Values& v,
                   const std::string& entity_type = "", int64_t entity_id = 0);
    // Reports: a forum thread, or its first post edited when it already exists.
    Result publish_report(Provider& p, Place forum, const std::string& existing_thread, const std::string& key,
                          const Values& v, const std::string& entity_type, int64_t entity_id);
    // An event's or meeting's report, posted to (or updated in) each
    // provider's reports forum. `sent` = providers it went to.
    struct ReportOutcome { int sent = 0; std::string error; };
    ReportOutcome publish_event_report(const LugEvent& e);
    ReportOutcome publish_meeting_report(const Meeting& m);
    Values event_report_values(const LugEvent& e) const;
    Values meeting_report_values(const Meeting& m) const;
    // Sends a failed standalone post again (from the activity log).
    bool retry(int64_t activity_id);

    // Reminders: a meeting's goes to its chapter's channel or the announcements
    // channel, an event's into its thread (else the announcements channel).
    // Return how many providers it went to.
    int remind_meeting(const Meeting& m);
    int remind_event(const LugEvent& e);

    // ── Direct messages ── true if a provider delivered it
    bool direct_message(int64_t member_id, const std::string& key, const Values& v);

    // ── Placeholder values ──
    Values event_values(const LugEvent& e, const Provider& p) const;
    Values meeting_values(const Meeting& m, const Provider& p) const;

    // ── Activity log ──
    void log(const Provider& p, const std::string& action, const std::string& what, const std::string& entity_type,
             int64_t entity_id, const Result& r, const std::string& channel = "", const std::string& payload = "");

    // Where the LUG's time zone comes from (the live setting, kept by DiscordClient).
    void set_timezone_source(std::function<std::string()> f) { tz_source_ = std::move(f); }
    // Whether some chat service would take a DM for this member right now.
    bool can_dm(int64_t member_id) const;
    std::string public_url() const { return public_url_; }
    std::string lug_name() const;
    std::string timezone() const;

private:
    struct Refs { std::string announce, chapter_announce, thread, scheduled; bool thread_owned = true; };
    Refs refs(const Provider& p, const std::string& entity_type, int64_t id) const;
    void save_ref(const Provider& p, const std::string& entity_type, int64_t id, const std::string& purpose, const std::string& value);
    void save_owned(const Provider& p, const std::string& entity_type, int64_t id, bool owned);

    Message message(const Provider& p, const std::string& key, const Values& v,
                    std::vector<std::string> roles = {}, std::vector<std::string> users = {});
    bool may_create(const Provider& p, const std::string& what, const std::string& entity_type, int64_t id);

    void publish_event(Provider& p, const LugEvent& e);
    void update_event(Provider& p, const LugEvent& before, const LugEvent& after, bool notify);
    void remove_event(Provider& p, const LugEvent& e, const Refs& r);
    void publish_meeting(Provider& p, const Meeting& m);
    void update_meeting(Provider& p, const Meeting& before, const Meeting& after);
    void remove_meeting(Provider& p, const Meeting& m, const Refs& r);

    std::vector<std::string> event_ping_roles(const Provider& p, const LugEvent& e, const std::string& main_role) const;

    std::string setting(const std::string& key, const std::string& def = "") const;

    SqliteDatabase& db_;
    std::string public_url_;
    std::vector<std::shared_ptr<Provider>> providers_;
    std::function<std::string()> tz_source_;
};

} // namespace chat
