#include "repositories/MeetingRepository.hpp"

static const char* kSelectAllCols =
    "SELECT id, title, description, location, start_time, end_time, "
    "status, discord_event_id, ical_uid, COALESCE(scope,'chapter'), chapter_id, "
    "created_at, updated_at, "
    "COALESCE(discord_lug_message_id,''), COALESCE(discord_chapter_message_id,''), "
    "COALESCE(google_calendar_event_id,''), "
    "suppress_discord, suppress_calendar, "
    "COALESCE(notes,''), COALESCE(notes_discord_post_id,''), "
    "is_virtual, COALESCE(discord_voice_channel_id,''), "
    "COALESCE(checkin_token,'') "
    "FROM meetings";

MeetingRepository::MeetingRepository(SqliteDatabase& db) : db_(db) {}

Meeting MeetingRepository::row_to_meeting(Statement& stmt) {
    Meeting m;
    m.id               = stmt.col_int(0);
    m.title            = stmt.col_text(1);
    m.description      = stmt.col_text(2);
    m.location         = stmt.col_text(3);
    m.start_time       = stmt.col_text(4);
    m.end_time         = stmt.col_text(5);
    m.status           = stmt.col_text(6);
    m.discord_event_id = stmt.col_is_null(7) ? "" : stmt.col_text(7);
    m.ical_uid         = stmt.col_text(8);
    m.scope            = stmt.col_text(9);
    m.chapter_id       = stmt.col_is_null(10) ? 0 : stmt.col_int(10);
    m.created_at                  = stmt.col_text(11);
    m.updated_at                  = stmt.col_text(12);
    m.discord_lug_message_id      = stmt.col_text(13);
    m.discord_chapter_message_id  = stmt.col_text(14);
    m.google_calendar_event_id    = stmt.col_text(15);
    m.suppress_discord            = stmt.col_bool(16);
    m.suppress_calendar           = stmt.col_bool(17);
    m.notes                       = stmt.col_text(18);
    m.notes_discord_post_id       = stmt.col_text(19);
    m.is_virtual                  = stmt.col_bool(20);
    m.discord_voice_channel_id    = stmt.col_text(21);
    m.checkin_token               = stmt.col_text(22);
    return m;
}

std::optional<Meeting> MeetingRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        return row_to_meeting(stmt);
    }
    return std::nullopt;
}

std::vector<Meeting> MeetingRepository::find_all() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " ORDER BY start_time ASC");
    std::vector<Meeting> result;
    while (stmt.step()) {
        result.push_back(row_to_meeting(stmt));
    }
    return result;
}

std::vector<Meeting> MeetingRepository::find_upcoming() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) +
        " WHERE start_time >= datetime('now', '-1 hour') ORDER BY start_time ASC");
    std::vector<Meeting> result;
    while (stmt.step()) {
        result.push_back(row_to_meeting(stmt));
    }
    return result;
}

std::vector<Meeting> MeetingRepository::find_by_status(const std::string& status) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE status=? ORDER BY start_time ASC");
    stmt.bind(1, status);
    std::vector<Meeting> result;
    while (stmt.step()) {
        result.push_back(row_to_meeting(stmt));
    }
    return result;
}

std::vector<Meeting> MeetingRepository::find_by_chapter(int64_t chapter_id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE chapter_id=? ORDER BY start_time ASC");
    stmt.bind(1, chapter_id);
    std::vector<Meeting> result;
    while (stmt.step()) {
        result.push_back(row_to_meeting(stmt));
    }
    return result;
}

std::vector<Meeting> MeetingRepository::find_upcoming_by_chapter(int64_t chapter_id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) +
        " WHERE chapter_id=? AND start_time >= datetime('now', '-1 hour') ORDER BY start_time ASC");
    stmt.bind(1, chapter_id);
    std::vector<Meeting> result;
    while (stmt.step()) {
        result.push_back(row_to_meeting(stmt));
    }
    return result;
}

std::vector<Meeting> MeetingRepository::find_paginated(const std::string& search, int limit, int offset,
                                                       const std::string& sort_col,
                                                       const std::string& sort_dir) {
    std::vector<Meeting> result;
    std::string dir = (sort_dir == "ASC") ? "ASC" : "DESC";
    std::string order = "start_time";
    if (sort_col == "title") order = "title";
    else if (sort_col == "status") order = "status";
    else if (sort_col == "location") order = "location";
    else if (sort_col == "scope") order = "scope";
    std::string order_clause = " ORDER BY " + order + " " + dir + " LIMIT ? OFFSET ?";

    if (search.empty()) {
        auto stmt = db_.prepare(std::string(kSelectAllCols) + order_clause);
        stmt.bind(1, static_cast<int64_t>(limit));
        stmt.bind(2, static_cast<int64_t>(offset));
        while (stmt.step()) result.push_back(row_to_meeting(stmt));
    } else {
        std::string pat = "%" + search + "%";
        auto stmt = db_.prepare(
            std::string(kSelectAllCols) +
            " WHERE title LIKE ? OR description LIKE ? OR location LIKE ?"
            + order_clause);
        stmt.bind(1, pat); stmt.bind(2, pat); stmt.bind(3, pat);
        stmt.bind(4, static_cast<int64_t>(limit));
        stmt.bind(5, static_cast<int64_t>(offset));
        while (stmt.step()) result.push_back(row_to_meeting(stmt));
    }
    return result;
}

int MeetingRepository::count_filtered(const std::string& search) {
    if (search.empty()) {
        auto stmt = db_.prepare("SELECT COUNT(*) FROM meetings");
        if (stmt.step()) return static_cast<int>(stmt.col_int(0));
        return 0;
    }
    std::string pat = "%" + search + "%";
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM meetings WHERE title LIKE ? OR description LIKE ? OR location LIKE ?");
    stmt.bind(1, pat); stmt.bind(2, pat); stmt.bind(3, pat);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

int MeetingRepository::count_all() {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM meetings");
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

Meeting MeetingRepository::create(const Meeting& m) {
    auto stmt = db_.prepare(
        "INSERT INTO meetings (title, description, location, start_time, end_time, "
        "status, discord_event_id, ical_uid, scope, chapter_id, "
        "suppress_discord, suppress_calendar, notes, is_virtual, discord_voice_channel_id) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    stmt.bind(1, m.title);
    stmt.bind(2, m.description);
    stmt.bind(3, m.location);
    stmt.bind(4, m.start_time);
    stmt.bind(5, m.end_time);
    stmt.bind(6, m.status.empty() ? std::string("scheduled") : m.status);
    if (m.discord_event_id.empty()) {
        stmt.bind_null(7);
    } else {
        stmt.bind(7, m.discord_event_id);
    }
    stmt.bind(8, m.ical_uid);
    stmt.bind(9, m.scope.empty() ? std::string("chapter") : m.scope);
    if (m.chapter_id <= 0) stmt.bind_null(10);
    else                   stmt.bind(10, m.chapter_id);
    stmt.bind(11, m.suppress_discord);
    stmt.bind(12, m.suppress_calendar);
    stmt.bind(13, m.notes);
    stmt.bind(14, m.is_virtual);
    stmt.bind(15, m.discord_voice_channel_id);
    stmt.step();

    int64_t new_id = db_.last_insert_rowid();
    auto result = find_by_id(new_id);
    if (!result) {
        throw DbError("Failed to retrieve inserted meeting with id=" + std::to_string(new_id));
    }
    return *result;
}

bool MeetingRepository::update(const Meeting& m) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET title=?, description=?, location=?, start_time=?, end_time=?, "
        "status=?, discord_event_id=?, scope=?, chapter_id=?, "
        "suppress_discord=?, suppress_calendar=?, notes=?, "
        "is_virtual=?, discord_voice_channel_id=?, "
        "updated_at=datetime('now') WHERE id=?");
    stmt.bind(1, m.title);
    stmt.bind(2, m.description);
    stmt.bind(3, m.location);
    stmt.bind(4, m.start_time);
    stmt.bind(5, m.end_time);
    stmt.bind(6, m.status);
    if (m.discord_event_id.empty()) {
        stmt.bind_null(7);
    } else {
        stmt.bind(7, m.discord_event_id);
    }
    stmt.bind(8, m.scope.empty() ? std::string("chapter") : m.scope);
    if (m.chapter_id <= 0) stmt.bind_null(9);
    else                   stmt.bind(9, m.chapter_id);
    stmt.bind(10, m.suppress_discord);
    stmt.bind(11, m.suppress_calendar);
    stmt.bind(12, m.notes);
    stmt.bind(13, m.is_virtual);
    stmt.bind(14, m.discord_voice_channel_id);
    stmt.bind(15, m.id);
    stmt.step();

    auto existing = find_by_id(m.id);
    return existing.has_value();
}

bool MeetingRepository::delete_by_id(int64_t id) {
    { auto s = db_.prepare("DELETE FROM attendance WHERE entity_type='meeting' AND entity_id=?");
      s.bind(1, id); s.step(); }
    auto stmt = db_.prepare("DELETE FROM meetings WHERE id=?");
    stmt.bind(1, id);
    stmt.step();

    auto existing = find_by_id(id);
    return !existing.has_value();
}

bool MeetingRepository::update_discord_event_id(int64_t id, const std::string& discord_event_id) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET discord_event_id=? WHERE id=?");
    if (discord_event_id.empty()) {
        stmt.bind_null(1);
    } else {
        stmt.bind(1, discord_event_id);
    }
    stmt.bind(2, id);
    stmt.step();

    auto existing = find_by_id(id);
    return existing.has_value();
}

bool MeetingRepository::update_lug_message_id(int64_t id, const std::string& message_id) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET discord_lug_message_id=? WHERE id=?");
    if (message_id.empty()) stmt.bind_null(1);
    else                    stmt.bind(1, message_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool MeetingRepository::update_chapter_message_id(int64_t id, const std::string& message_id) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET discord_chapter_message_id=? WHERE id=?");
    if (message_id.empty()) stmt.bind_null(1);
    else                    stmt.bind(1, message_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool MeetingRepository::exists_by_google_calendar_id(const std::string& gcal_event_id) {
    if (gcal_event_id.empty()) return false;
    auto stmt = db_.prepare("SELECT COUNT(*) FROM meetings WHERE google_calendar_event_id=?");
    stmt.bind(1, gcal_event_id);
    if (stmt.step()) return stmt.col_int(0) > 0;
    return false;
}

bool MeetingRepository::update_google_calendar_event_id(int64_t id, const std::string& gcal_event_id) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET google_calendar_event_id=? WHERE id=?");
    if (gcal_event_id.empty()) stmt.bind_null(1);
    else                       stmt.bind(1, gcal_event_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool MeetingRepository::update_notes_discord_post_id(int64_t id, const std::string& post_id) {
    auto stmt = db_.prepare(
        "UPDATE meetings SET notes_discord_post_id=? WHERE id=?");
    if (post_id.empty()) stmt.bind_null(1);
    else                 stmt.bind(1, post_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool MeetingRepository::update_checkin_token(int64_t id, const std::string& token) {
    auto stmt = db_.prepare("UPDATE meetings SET checkin_token=? WHERE id=?");
    stmt.bind(1, token);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

std::optional<Meeting> MeetingRepository::find_by_checkin_token(const std::string& token) {
    if (token.empty()) return std::nullopt;
    auto stmt = db_.prepare(std::string(kSelectAllCols) + " WHERE checkin_token=?");
    stmt.bind(1, token);
    if (stmt.step()) return row_to_meeting(stmt);
    return std::nullopt;
}
