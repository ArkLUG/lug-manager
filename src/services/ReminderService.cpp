#include "services/ReminderService.hpp"
#include "repositories/NotificationPrefs.hpp"
#include <iostream>

ReminderService::ReminderService(SqliteDatabase& db, MeetingRepository& meetings,
                                 EventRepository& events, ChapterRepository& chapters,
                                 MemberRepository& members, SettingsRepository& settings,
                                 DiscordClient& discord)
    : db_(db), meetings_(meetings), events_(events), chapters_(chapters), members_(members),
      settings_(settings), discord_(discord), rsvps_(db), shifts_(db) {}

Notifier& ReminderService::notifier() {
    if (!notifier_) notifier_ = std::make_shared<Notifier>(db_, discord_, nullptr, "");  // DMs only
    return *notifier_;
}

bool ReminderService::claim(const char* table, int64_t id) {
    // Compare-and-set so two runs (or instances) can never both send.
    auto stmt = db_.prepare(std::string("UPDATE ") + table +
        " SET reminder_sent_at=strftime('%Y-%m-%dT%H:%M:%S','now') "
        "WHERE id=? AND reminder_sent_at IS NULL RETURNING id");
    stmt.bind(1, id);
    return stmt.step();
}

ReminderService::Result ReminderService::run_once(std::time_t now) {
    Result r;
    if (settings_.get("discord_reminders_enabled", "") != "1") return r;
    int hours = 24;
    try { hours = std::stoi(settings_.get("discord_reminder_hours", "24")); } catch (...) {}
    if (hours < 1 || hours > 24 * 14) hours = 24;
    bool dm_rsvps = settings_.get("discord_reminder_dm_rsvps", "") == "1";
    std::string tz = discord_.get_timezone();

    // Due = starts within the lead time and hasn't started yet.
    auto due = [&](const std::string& start) {
        std::time_t t = DiscordClient::local_to_epoch(start, tz);
        return t > now && t - now <= static_cast<std::time_t>(hours) * 3600;
    };

    for (const auto& m : meetings_.find_upcoming()) {
        if (m.status == "cancelled" || m.suppress_discord || !due(m.start_time)) continue;
        std::string channel;
        if (m.scope == "chapter" && m.chapter_id > 0) {
            if (auto ch = chapters_.find_by_id(m.chapter_id)) channel = ch->discord_announcement_channel_id;
        } else {
            channel = discord_.get_lug_channel_id();
        }
        if (channel.empty() || !claim("meetings", m.id)) continue;
        std::string msg = "⏰ **Reminder:** " + m.title + " - " +
                          DiscordClient::friendly_time(m.start_time, tz) +
                          (m.location.empty() ? "" : " at " + m.location);
        if (discord_.sync_post_message(channel, msg)) ++r.meetings;
    }

    for (const auto& e : events_.find_upcoming()) {
        if (e.status == "cancelled" || e.suppress_discord || !due(e.start_time)) continue;
        std::string channel = !e.discord_thread_id.empty() ? e.discord_thread_id : discord_.get_lug_channel_id();
        if (channel.empty() || !claim("lug_events", e.id)) continue;
        std::string when = DiscordClient::friendly_time(e.start_time, tz);
        std::string msg = "⏰ **Reminder:** " + e.title + " starts " + when +
                          (e.location.empty() ? "" : " at " + e.location);
        if (discord_.sync_post_message(channel, msg)) ++r.events;

        if (dm_rsvps) {
            for (const auto& rs : rsvps_.list(e.id)) {
                if (rs.status != "going") continue;
                if (notifier().notify(rs.member_id, "event_reminder", "Reminder: " + e.title + " - " + when,
                        "⏰ You're going to **" + e.title + "** - " + when +
                        (e.location.empty() ? "" : " at " + e.location) + ". See you there!"))
                    ++r.dms;
            }
        }
    }
    // Volunteers get a DM before their shift (same lead time, same opt-in).
    if (dm_rsvps) {
        auto fmt = [&](std::time_t t) {
            std::tm tm{}; localtime_r(&t, &tm);
            char b[32]; std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M", &tm); return std::string(b);
        };
        // Window is in LUG-local time; the server clock may differ, so allow a day either side
        // and filter precisely with local_to_epoch.
        for (const auto& d : shifts_.due_reminders(fmt(now - 86400), fmt(now + (hours + 24) * 3600L))) {
            std::time_t t = DiscordClient::local_to_epoch(d.starts_at, tz);
            if (t <= now || t - now > static_cast<std::time_t>(hours) * 3600) continue;
            if (!shifts_.claim_reminder(d.signup_id)) continue;
            std::string at = DiscordClient::friendly_time(d.starts_at, tz);
            if (notifier().notify(d.member_id, "shift_reminder", "Volunteer shift: " + d.shift_title + " - " + at,
                                  "\u23F0 Reminder: you're volunteering for **" + d.shift_title +
                                  "** at " + d.event_title + " - " + at + ". Thank you!"))
                ++r.dms;
        }
    }
    return r;
}
