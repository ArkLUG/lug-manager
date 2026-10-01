#pragma once
#include "integrations/DiscordClient.hpp"
#include "repositories/ChapterRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/RsvpRepository.hpp"
#include "repositories/SettingsRepository.hpp"

// Posts a Discord reminder ahead of each meeting/event, once.
//
// Settings (lug_settings):
//   discord_reminders_enabled   "1" to turn on (off by default)
//   discord_reminder_hours      lead time in hours (default 24)
//   discord_reminder_dm_rsvps   "1" to also DM members who RSVP'd "going"
//
// Meetings remind in the channel they were announced in; events in their
// Discord thread (falling back to the LUG channel). Cancelled, suppressed
// and already-started items are skipped.
class ReminderService {
public:
    ReminderService(SqliteDatabase& db, MeetingRepository& meetings, EventRepository& events,
                    ChapterRepository& chapters, MemberRepository& members,
                    SettingsRepository& settings, DiscordClient& discord);

    struct Result { int meetings = 0; int events = 0; int dms = 0; };
    // Sends whatever is due now. Safe to call repeatedly.
    Result run_once(std::time_t now = std::time(nullptr));

private:
    SqliteDatabase&     db_;
    MeetingRepository&  meetings_;
    EventRepository&    events_;
    ChapterRepository&  chapters_;
    MemberRepository&   members_;
    SettingsRepository& settings_;
    DiscordClient&      discord_;
    RsvpRepository      rsvps_;

    bool claim(const char* table, int64_t id); // marks sent; false if already sent
};
