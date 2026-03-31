#include "repositories/MeetingRepository.hpp"

static const char* kSelectAllCols =
    "SELECT id, title, description, location, start_time, end_time, "
    "status, discord_event_id, ical_uid, COALESCE(scope,'chapter'), chapter_id, "
    "created_at, updated_at, "
    "COALESCE(discord_lug_message_id,''), COALESCE(discord_chapter_message_id,'') "
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

std::vector<Meeting> MeetingRepository::find_paginated(const std::string& search, int limit, int offset) {
    std::vector<Meeting> result;
    if (search.empty()) {
        auto stmt = db_.prepare(
            std::string(kSelectAllCols) + " ORDER BY start_time DESC LIMIT ? OFFSET ?");
        stmt.bind(1, static_cast<int64_t>(limit));
        stmt.bind(2, static_cast<int64_t>(offset));
        while (stmt.step()) result.push_back(row_to_meeting(stmt));
    } else {
        std::string pat = "%" + search + "%";
        auto stmt = db_.prepare(
            std::string(kSelectAllCols) +
            " WHERE title LIKE ? OR description LIKE ? OR location LIKE ?"
            " ORDER BY start_time DESC LIMIT ? OFFSET ?");
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
        "status, discord_event_id, ical_uid, scope, chapter_id) VALUES (?,?,?,?,?,?,?,?,?,?)");
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
    stmt.bind(10, m.chapter_id);
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
        "status=?, discord_event_id=?, scope=?, updated_at=datetime('now') WHERE id=?");
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
    stmt.bind(9, m.id);
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
