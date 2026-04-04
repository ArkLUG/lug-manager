#pragma once
#include "repositories/EventRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "models/LugEvent.hpp"
#include <vector>
#include <optional>
#include <string>

class EventService {
public:
    EventService(EventRepository& repo, DiscordClient& discord, CalendarGenerator& cal,
                 ChapterRepository* chapter_repo = nullptr, GoogleCalendarClient* gcal = nullptr);

    std::vector<LugEvent>   list_upcoming();
    std::vector<LugEvent>   list_all();
    std::vector<LugEvent>   list_by_chapter(int64_t chapter_id);
    std::vector<LugEvent>   list_paginated(const std::string& search, int limit, int offset,
                                            bool upcoming_only = true,
                                            const std::string& sort_col = "start_time",
                                            const std::string& sort_dir = "ASC");
    int                     count_filtered(const std::string& search, bool upcoming_only = true);
    int                     count_all();
    std::optional<LugEvent> get(int64_t id);

    LugEvent create(const LugEvent& e);
    LugEvent create_imported(const LugEvent& e);  // Creates without Discord/Google Calendar integration
    bool     exists_by_google_calendar_id(const std::string& gcal_event_id);
    LugEvent update(int64_t id, const LugEvent& updates);
    void     cancel(int64_t id);
    void     update_status(int64_t id, const std::string& status);

    struct SyncResult { int synced = 0; int created = 0; int errors = 0; };
    SyncResult sync_all_to_google_calendar();
    SyncResult sync_all_to_discord();

private:
    EventRepository&        repo_;
    DiscordClient&          discord_;
    CalendarGenerator&      cal_;
    ChapterRepository*      chapter_repo_;
    GoogleCalendarClient*   gcal_;

    static std::string generate_uuid();

public:
    EventRepository& repo() { return repo_; }
    LugEvent with_calendar_title(const LugEvent& e) const;
};
