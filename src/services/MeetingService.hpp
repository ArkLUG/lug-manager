#pragma once
#include "chat/ChatHub.hpp"
#include <iostream>
#include <functional>
#include "async/ThreadPool.hpp"
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
    std::vector<Meeting>   list_paginated(const std::string& search, int limit, int offset,
                                           const std::string& sort_col = "start_time",
                                           const std::string& sort_dir = "DESC");
    int                    count_filtered(const std::string& search);
    int                    count_all();
    bool                   exists_by_google_calendar_id(const std::string& gcal_event_id);
    std::optional<Meeting> get(int64_t id);

    Meeting create(const Meeting& m);           // Generates ical_uid, posts to Discord
    Meeting create_imported(const Meeting& m);  // Creates without Discord/Google Calendar integration
    // Propagates to Discord + calendar. Empty text fields in `updates` mean
    // "keep" unless replace_text_fields is set, in which case description and
    // location are taken verbatim (so a caller passing a full record - edit
    // form, API - can clear them).
    Meeting update(int64_t id, const Meeting& updates, bool replace_text_fields = false);
    void    cancel(int64_t id);
    void    complete(int64_t id);

    struct SyncResult { int synced = 0; int created = 0; int errors = 0; };
    SyncResult sync_all_to_google_calendar();
    SyncResult sync_all_to_discord();

    void set_chat(std::shared_ptr<chat::ChatHub> hub) { chat_owner_ = hub; chat_ = hub.get(); }
    chat::ChatHub* chat() const { return chat_; }
    MeetingRepository& repo() { return repo_; }

    static std::string generate_uuid();

    // Public to match EventService::with_calendar_title (also public) - both
    // are pure title/description/location formatting with no side effects,
    // and both are unit-tested directly against their redaction behavior.
    Meeting with_calendar_title(const Meeting& m) const;

    // Production: publish to Discord / Google Calendar on a background
    // thread so saving doesn't wait on several external API calls. Tests leave
    // it unset (synchronous).
    void set_async_pool(ThreadPool* pool) { async_pool_ = pool; }

private:
    MeetingRepository&      repo_;
    DiscordClient&          discord_;
    CalendarGenerator&      cal_;
    ChapterRepository*      chapter_repo_;
    GoogleCalendarClient*   gcal_;

    std::shared_ptr<chat::ChatHub> chat_owner_;   // chat services (Discord, ...); nullptr = none
    chat::ChatHub*          chat_ = nullptr;
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
