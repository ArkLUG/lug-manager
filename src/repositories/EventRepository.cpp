#include "repositories/EventRepository.hpp"

static const char* kSelectAllCols =
    "SELECT e.id, e.title, e.description, e.location, e.start_time, e.end_time, "
    "e.status, e.discord_thread_id, e.discord_event_id, e.ical_uid, e.signup_deadline, "
    "e.max_attendees, COALESCE(e.scope,'chapter'), e.chapter_id, e.created_at, e.updated_at, "
    "e.event_lead_id, COALESCE(m.display_name,''), "
    "COALESCE(e.discord_chapter_message_id,''), "
    "COALESCE(e.discord_lug_message_id,''), "
    "COALESCE(e.discord_ping_role_ids,''), "
    "COALESCE(m.discord_user_id,''), "
    "COALESCE(e.google_calendar_event_id,''), "
    "e.suppress_discord, e.suppress_calendar, "
    "COALESCE(e.notes,''), COALESCE(e.notes_discord_post_id,''), "
    "COALESCE(e.entrance_fee,''), e.public_kids, e.public_teens, e.public_adults, "
    "COALESCE(e.social_media_links,''), COALESCE(e.event_feedback,'') "
    "FROM lug_events e LEFT JOIN members m ON m.id = e.event_lead_id";

EventRepository::EventRepository(SqliteDatabase& db) : db_(db) {}

LugEvent EventRepository::row_to_event(Statement& stmt) {
    LugEvent e;
    e.id               = stmt.col_int(0);
    e.title            = stmt.col_text(1);
    e.description      = stmt.col_text(2);
    e.location         = stmt.col_text(3);
    e.start_time       = stmt.col_text(4);
    e.end_time         = stmt.col_text(5);
    e.status           = stmt.col_text(6);
    e.discord_thread_id = stmt.col_is_null(7) ? "" : stmt.col_text(7);
    e.discord_event_id  = stmt.col_is_null(8) ? "" : stmt.col_text(8);
    e.ical_uid          = stmt.col_text(9);
    e.signup_deadline   = stmt.col_is_null(10) ? "" : stmt.col_text(10);
    e.max_attendees     = static_cast<int>(stmt.col_int(11));
    e.scope             = stmt.col_text(12);
    e.chapter_id        = stmt.col_is_null(13) ? 0 : stmt.col_int(13);
    e.created_at        = stmt.col_text(14);
    e.updated_at        = stmt.col_text(15);
    e.event_lead_id              = stmt.col_is_null(16) ? 0 : stmt.col_int(16);
    e.event_lead_name            = stmt.col_text(17);
    e.discord_chapter_message_id = stmt.col_text(18);
    e.discord_lug_message_id     = stmt.col_text(19);
    e.discord_ping_role_ids      = stmt.col_text(20);
    e.event_lead_discord_id      = stmt.col_text(21);
    e.google_calendar_event_id   = stmt.col_text(22);
    e.suppress_discord           = stmt.col_bool(23);
    e.suppress_calendar          = stmt.col_bool(24);
    e.notes                      = stmt.col_text(25);
    e.notes_discord_post_id      = stmt.col_text(26);
    e.entrance_fee               = stmt.col_text(27);
    e.public_kids                = static_cast<int>(stmt.col_int(28));
    e.public_teens               = static_cast<int>(stmt.col_int(29));
    e.public_adults              = static_cast<int>(stmt.col_int(30));
    e.social_media_links         = stmt.col_text(31);
    e.event_feedback             = stmt.col_text(32);
    return e;
}

std::optional<LugEvent> EventRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE e.id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        return row_to_event(stmt);
    }
    return std::nullopt;
}

bool EventRepository::exists_by_google_calendar_id(const std::string& gcal_event_id) {
    if (gcal_event_id.empty()) return false;
    auto stmt = db_.prepare("SELECT COUNT(*) FROM lug_events WHERE google_calendar_event_id=?");
    stmt.bind(1, gcal_event_id);
    if (stmt.step()) return stmt.col_int(0) > 0;
    return false;
}

std::vector<LugEvent> EventRepository::find_all() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " ORDER BY e.start_time ASC");
    std::vector<LugEvent> result;
    while (stmt.step()) {
        result.push_back(row_to_event(stmt));
    }
    return result;
}

std::vector<LugEvent> EventRepository::find_upcoming() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) +
        " WHERE e.start_time >= datetime('now', '-1 hour') ORDER BY e.start_time ASC");
    std::vector<LugEvent> result;
    while (stmt.step()) {
        result.push_back(row_to_event(stmt));
    }
    return result;
}

std::vector<LugEvent> EventRepository::find_by_status(const std::string& status) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE e.status=? ORDER BY e.start_time ASC");
    stmt.bind(1, status);
    std::vector<LugEvent> result;
    while (stmt.step()) {
        result.push_back(row_to_event(stmt));
    }
    return result;
}

std::vector<LugEvent> EventRepository::find_by_chapter(int64_t chapter_id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE e.chapter_id=? ORDER BY e.start_time ASC");
    stmt.bind(1, chapter_id);
    std::vector<LugEvent> result;
    while (stmt.step()) {
        result.push_back(row_to_event(stmt));
    }
    return result;
}

std::vector<LugEvent> EventRepository::find_upcoming_by_chapter(int64_t chapter_id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) +
        " WHERE e.chapter_id=? AND e.start_time >= datetime('now', '-1 hour') ORDER BY e.start_time ASC");
    stmt.bind(1, chapter_id);
    std::vector<LugEvent> result;
    while (stmt.step()) {
        result.push_back(row_to_event(stmt));
    }
    return result;
}

std::vector<LugEvent> EventRepository::find_paginated(const std::string& search, int limit, int offset, bool upcoming_only) {
    std::string where;
    if (upcoming_only) where += " WHERE e.start_time >= datetime('now', '-1 hour')";
    if (!search.empty()) {
        where += upcoming_only ? " AND" : " WHERE";
        where += " (e.title LIKE ? OR e.description LIKE ? OR e.location LIKE ?)";
    }
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + where + " ORDER BY e.start_time ASC LIMIT ? OFFSET ?");
    int idx = 1;
    std::string pat;
    if (!search.empty()) {
        pat = "%" + search + "%";
        stmt.bind(idx++, pat); stmt.bind(idx++, pat); stmt.bind(idx++, pat);
    }
    stmt.bind(idx++, static_cast<int64_t>(limit));
    stmt.bind(idx, static_cast<int64_t>(offset));
    std::vector<LugEvent> result;
    while (stmt.step()) result.push_back(row_to_event(stmt));
    return result;
}

int EventRepository::count_filtered(const std::string& search, bool upcoming_only) {
    std::string sql = "SELECT COUNT(*) FROM lug_events e";
    if (upcoming_only) sql += " WHERE e.start_time >= datetime('now', '-1 hour')";
    if (!search.empty()) {
        sql += upcoming_only ? " AND" : " WHERE";
        sql += " (e.title LIKE ? OR e.description LIKE ? OR e.location LIKE ?)";
    }
    auto stmt = db_.prepare(sql);
    if (!search.empty()) {
        std::string pat = "%" + search + "%";
        stmt.bind(1, pat); stmt.bind(2, pat); stmt.bind(3, pat);
    }
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

int EventRepository::count_all() {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM lug_events");
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

LugEvent EventRepository::create(const LugEvent& e) {
    auto stmt = db_.prepare(
        "INSERT INTO lug_events (title, description, location, "
        "start_time, end_time, status, discord_thread_id, discord_event_id, "
        "ical_uid, signup_deadline, max_attendees, scope, chapter_id, event_lead_id, "
        "discord_ping_role_ids, suppress_discord, suppress_calendar, notes, "
        "entrance_fee, public_kids, public_teens, public_adults, social_media_links, event_feedback) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    stmt.bind(1, e.title);
    stmt.bind(2, e.description);
    stmt.bind(3, e.location);
    stmt.bind(4, e.start_time);
    stmt.bind(5, e.end_time);
    stmt.bind(6, e.status.empty() ? std::string("confirmed") : e.status);
    if (e.discord_thread_id.empty()) {
        stmt.bind_null(7);
    } else {
        stmt.bind(7, e.discord_thread_id);
    }
    if (e.discord_event_id.empty()) {
        stmt.bind_null(8);
    } else {
        stmt.bind(8, e.discord_event_id);
    }
    stmt.bind(9, e.ical_uid);
    if (e.signup_deadline.empty()) {
        stmt.bind_null(10);
    } else {
        stmt.bind(10, e.signup_deadline);
    }
    stmt.bind(11, static_cast<int64_t>(e.max_attendees));
    stmt.bind(12, e.scope.empty() ? std::string("chapter") : e.scope);
    if (e.chapter_id <= 0) {
        stmt.bind_null(13);
    } else {
        stmt.bind(13, e.chapter_id);
    }
    if (e.event_lead_id <= 0) {
        stmt.bind_null(14);
    } else {
        stmt.bind(14, e.event_lead_id);
    }
    if (e.discord_ping_role_ids.empty()) {
        stmt.bind_null(15);
    } else {
        stmt.bind(15, e.discord_ping_role_ids);
    }
    stmt.bind(16, e.suppress_discord);
    stmt.bind(17, e.suppress_calendar);
    stmt.bind(18, e.notes);
    stmt.bind(19, e.entrance_fee);
    stmt.bind(20, static_cast<int64_t>(e.public_kids));
    stmt.bind(21, static_cast<int64_t>(e.public_teens));
    stmt.bind(22, static_cast<int64_t>(e.public_adults));
    stmt.bind(23, e.social_media_links);
    stmt.bind(24, e.event_feedback);
    stmt.step();

    int64_t new_id = db_.last_insert_rowid();
    auto result = find_by_id(new_id);
    if (!result) {
        throw DbError("Failed to retrieve inserted lug_event with id=" + std::to_string(new_id));
    }
    return *result;
}

bool EventRepository::update(const LugEvent& e) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET title=?, description=?, location=?, "
        "start_time=?, end_time=?, status=?, discord_thread_id=?, discord_event_id=?, "
        "signup_deadline=?, max_attendees=?, scope=?, chapter_id=?, event_lead_id=?, "
        "discord_ping_role_ids=?, suppress_discord=?, suppress_calendar=?, notes=?, "
        "entrance_fee=?, public_kids=?, public_teens=?, public_adults=?, "
        "social_media_links=?, event_feedback=?, "
        "updated_at=datetime('now') WHERE id=?");
    stmt.bind(1, e.title);
    stmt.bind(2, e.description);
    stmt.bind(3, e.location);
    stmt.bind(4, e.start_time);
    stmt.bind(5, e.end_time);
    stmt.bind(6, e.status);
    if (e.discord_thread_id.empty()) {
        stmt.bind_null(7);
    } else {
        stmt.bind(7, e.discord_thread_id);
    }
    if (e.discord_event_id.empty()) {
        stmt.bind_null(8);
    } else {
        stmt.bind(8, e.discord_event_id);
    }
    if (e.signup_deadline.empty()) {
        stmt.bind_null(9);
    } else {
        stmt.bind(9, e.signup_deadline);
    }
    stmt.bind(10, static_cast<int64_t>(e.max_attendees));
    stmt.bind(11, e.scope.empty() ? std::string("chapter") : e.scope);
    if (e.chapter_id <= 0) {
        stmt.bind_null(12);
    } else {
        stmt.bind(12, e.chapter_id);
    }
    if (e.event_lead_id <= 0) {
        stmt.bind_null(13);
    } else {
        stmt.bind(13, e.event_lead_id);
    }
    if (e.discord_ping_role_ids.empty()) {
        stmt.bind_null(14);
    } else {
        stmt.bind(14, e.discord_ping_role_ids);
    }
    stmt.bind(15, e.suppress_discord);
    stmt.bind(16, e.suppress_calendar);
    stmt.bind(17, e.notes);
    stmt.bind(18, e.entrance_fee);
    stmt.bind(19, static_cast<int64_t>(e.public_kids));
    stmt.bind(20, static_cast<int64_t>(e.public_teens));
    stmt.bind(21, static_cast<int64_t>(e.public_adults));
    stmt.bind(22, e.social_media_links);
    stmt.bind(23, e.event_feedback);
    stmt.bind(24, e.id);
    stmt.step();

    auto existing = find_by_id(e.id);
    return existing.has_value();
}

bool EventRepository::delete_by_id(int64_t id) {
    { auto s = db_.prepare("DELETE FROM attendance WHERE entity_type='event' AND entity_id=?");
      s.bind(1, id); s.step(); }
    auto stmt = db_.prepare("DELETE FROM lug_events WHERE id=?");
    stmt.bind(1, id);
    stmt.step();

    auto existing = find_by_id(id);
    return !existing.has_value();
}

bool EventRepository::update_lug_message_id(int64_t id, const std::string& message_id) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET discord_lug_message_id=? WHERE id=?");
    if (message_id.empty()) stmt.bind_null(1);
    else                    stmt.bind(1, message_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool EventRepository::update_chapter_message_id(int64_t id,
                                                  const std::string& message_id) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET discord_chapter_message_id=? WHERE id=?");
    if (message_id.empty()) stmt.bind_null(1);
    else                    stmt.bind(1, message_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool EventRepository::update_google_calendar_event_id(int64_t id, const std::string& gcal_event_id) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET google_calendar_event_id=? WHERE id=?");
    if (gcal_event_id.empty()) stmt.bind_null(1);
    else                       stmt.bind(1, gcal_event_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}

bool EventRepository::update_discord_ids(int64_t id,
                                          const std::string& discord_thread_id,
                                          const std::string& discord_event_id) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET discord_thread_id=?, discord_event_id=? WHERE id=?");
    if (discord_thread_id.empty()) {
        stmt.bind_null(1);
    } else {
        stmt.bind(1, discord_thread_id);
    }
    if (discord_event_id.empty()) {
        stmt.bind_null(2);
    } else {
        stmt.bind(2, discord_event_id);
    }
    stmt.bind(3, id);
    stmt.step();

    auto existing = find_by_id(id);
    return existing.has_value();
}

bool EventRepository::update_notes_discord_post_id(int64_t id, const std::string& post_id) {
    auto stmt = db_.prepare(
        "UPDATE lug_events SET notes_discord_post_id=? WHERE id=?");
    if (post_id.empty()) stmt.bind_null(1);
    else                 stmt.bind(1, post_id);
    stmt.bind(2, id);
    stmt.step();
    return true;
}
