#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/LugEvent.hpp"
#include <vector>
#include <optional>
#include <string>

class EventRepository {
public:
    explicit EventRepository(SqliteDatabase& db);

    std::optional<LugEvent> find_by_id(int64_t id);
    bool                    exists_by_google_calendar_id(const std::string& gcal_event_id);
    std::vector<LugEvent>   find_all();
    std::vector<LugEvent>   find_upcoming();  // start_time >= now - 1 hour, sorted ASC
    std::vector<LugEvent>   find_by_status(const std::string& status);
    std::vector<LugEvent>   find_by_chapter(int64_t chapter_id);
    std::vector<LugEvent>   find_upcoming_by_chapter(int64_t chapter_id);

    std::vector<LugEvent> find_paginated(const std::string& search, int limit, int offset,
                                          bool upcoming_only = true,
                                          const std::string& sort_col = "start_time",
                                          const std::string& sort_dir = "ASC");
    int                   count_filtered(const std::string& search, bool upcoming_only = true);
    int                   count_all();

    LugEvent create(const LugEvent& e);
    bool     update(const LugEvent& e);
    bool     delete_by_id(int64_t id);
    bool     update_discord_ids(int64_t id,
                                const std::string& discord_thread_id,
                                const std::string& discord_event_id);
    bool     update_lug_message_id(int64_t id, const std::string& message_id);
    bool     update_chapter_message_id(int64_t id, const std::string& message_id);
    bool     update_google_calendar_event_id(int64_t id, const std::string& gcal_event_id);
    bool     update_notes_discord_post_id(int64_t id, const std::string& post_id);

private:
    SqliteDatabase& db_;
    static LugEvent row_to_event(Statement& stmt);
};
