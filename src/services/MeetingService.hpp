#pragma once
#include "repositories/MeetingRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "models/Meeting.hpp"
#include <vector>
#include <optional>
#include <string>
#include <openssl/rand.h>

class MeetingService {
public:
    MeetingService(MeetingRepository& repo, DiscordClient& discord, CalendarGenerator& cal,
                   ChapterRepository* chapter_repo = nullptr, GoogleCalendarClient* gcal = nullptr);

    std::vector<Meeting>   list_upcoming();
    std::vector<Meeting>   list_all();
    std::vector<Meeting>   list_by_chapter(int64_t chapter_id);
    std::vector<Meeting>   list_paginated(const std::string& search, int limit, int offset);
    int                    count_filtered(const std::string& search);
    int                    count_all();
    bool                   exists_by_google_calendar_id(const std::string& gcal_event_id);
    std::optional<Meeting> get(int64_t id);

    Meeting create(const Meeting& m);           // Generates ical_uid, posts to Discord
    Meeting create_imported(const Meeting& m);  // Creates without Discord/Google Calendar integration
    Meeting update(int64_t id, const Meeting& updates); // Propagates to Discord + calendar
    void    cancel(int64_t id);
    void    complete(int64_t id);

    struct SyncResult { int synced = 0; int created = 0; int errors = 0; };
    SyncResult sync_all_to_google_calendar();
    SyncResult sync_all_to_discord();

private:
    MeetingRepository&      repo_;
    DiscordClient&          discord_;
    CalendarGenerator&      cal_;
    ChapterRepository*      chapter_repo_;
    GoogleCalendarClient*   gcal_;

    static std::string generate_uuid();
    Meeting with_calendar_title(const Meeting& m) const;
};
