#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Meeting.hpp"
#include <vector>
#include <optional>
#include <string>

class MeetingRepository {
public:
    explicit MeetingRepository(SqliteDatabase& db);

    std::optional<Meeting> find_by_id(int64_t id);
    std::vector<Meeting>   find_all();
    std::vector<Meeting>   find_upcoming();  // start_time >= now - 1 hour, sorted ASC
    std::vector<Meeting>   find_by_status(const std::string& status);
    std::vector<Meeting>   find_by_chapter(int64_t chapter_id);
    std::vector<Meeting>   find_upcoming_by_chapter(int64_t chapter_id);

    std::vector<Meeting> find_paginated(const std::string& search, int limit, int offset,
                                        const std::string& sort_col = "start_time",
                                        const std::string& sort_dir = "DESC");
    int                  count_filtered(const std::string& search);
    int                  count_all();

    Meeting create(const Meeting& m);
    bool    update(const Meeting& m);
    bool    delete_by_id(int64_t id);
    bool    update_discord_event_id(int64_t id, const std::string& discord_event_id);
    bool    update_lug_message_id(int64_t id, const std::string& message_id);
    bool    update_chapter_message_id(int64_t id, const std::string& message_id);
    bool    update_google_calendar_event_id(int64_t id, const std::string& gcal_event_id);
    bool    exists_by_google_calendar_id(const std::string& gcal_event_id);
    bool    update_notes_discord_post_id(int64_t id, const std::string& post_id);

private:
    SqliteDatabase& db_;
    static Meeting row_to_meeting(Statement& stmt);
};
