#pragma once
#include <iostream>
#include <functional>
#include "async/ThreadPool.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/EventDayRepository.hpp"
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
                 ChapterRepository* chapter_repo = nullptr, GoogleCalendarClient* gcal = nullptr,
                 EventDayRepository* event_day_repo = nullptr);

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
    // See MeetingService::update for replace_text_fields.
    // notify=false skips the "Event Updated" post in the event's thread (bulk
    // re-syncs and status-only changes shouldn't spam every thread).
    LugEvent update(int64_t id, const LugEvent& updates, bool replace_text_fields = false,
                    bool notify = true);
    void     cancel(int64_t id);
    void     update_status(int64_t id, const std::string& status);

    struct SyncResult { int synced = 0; int created = 0; int errors = 0; };
    SyncResult sync_all_to_google_calendar();
    SyncResult sync_all_to_discord();

    static std::string generate_uuid();

    // Production: publish to Discord / Google Calendar on a background
    // thread so saving doesn't wait on several external API calls. Tests leave
    // it unset (synchronous).
    void set_async_pool(ThreadPool* pool) { async_pool_ = pool; }

private:
    EventRepository&        repo_;
    DiscordClient&          discord_;
    CalendarGenerator&      cal_;
    ChapterRepository*      chapter_repo_;
    GoogleCalendarClient*   gcal_;
    EventDayRepository*     event_day_repo_;

    // Creates thread/announcements/scheduled event and stores their ids.
    void publish_to_discord(LugEvent& e);
    // Removes them (thread only if the app created it). Never throws.
    void remove_from_discord(const LugEvent& e);

public:
    EventRepository& repo() { return repo_; }
    LugEvent with_calendar_title(const LugEvent& e) const;
    ThreadPool* async_pool_ = nullptr;
    void run_external(std::function<void()> job) {
        if (async_pool_) async_pool_->enqueue([job = std::move(job)] {
            try { job(); } catch (const std::exception& e) {
                std::cerr << "[external sync] " << e.what() << "\n";
            } catch (...) {}
        });
        else job();
    }
};
