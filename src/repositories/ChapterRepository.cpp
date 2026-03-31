#include "repositories/ChapterRepository.hpp"
#include <iostream>

static const char* kSelectAllCols =
    "SELECT id, name, description, discord_announcement_channel_id, "
    "       COALESCE(discord_lead_role_id, ''), created_by, created_at, "
    "       COALESCE(discord_member_role_id, '') "
    "FROM chapters";

ChapterRepository::ChapterRepository(SqliteDatabase& db) : db_(db) {}

Chapter ChapterRepository::row_to_chapter(Statement& stmt) {
    Chapter ch;
    ch.id                               = stmt.col_int(0);
    ch.name                             = stmt.col_text(1);
    ch.description                      = stmt.col_is_null(2) ? "" : stmt.col_text(2);
    ch.discord_announcement_channel_id  = stmt.col_text(3);
    ch.discord_lead_role_id             = stmt.col_text(4);
    ch.created_by                       = stmt.col_is_null(5) ? 0 : stmt.col_int(5);
    ch.created_at                       = stmt.col_text(6);
    ch.discord_member_role_id           = stmt.col_text(7);
    return ch;
}

std::optional<Chapter> ChapterRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        return row_to_chapter(stmt);
    }
    return std::nullopt;
}

std::vector<Chapter> ChapterRepository::find_all() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " ORDER BY name ASC");
    std::vector<Chapter> result;
    while (stmt.step()) {
        result.push_back(row_to_chapter(stmt));
    }
    return result;
}

Chapter ChapterRepository::create(const Chapter& ch) {
    auto stmt = db_.prepare(
        "INSERT INTO chapters (name, description, discord_announcement_channel_id, "
        "                      discord_lead_role_id, discord_member_role_id, created_by) "
        "VALUES (?,?,?,?,?,?)");
    stmt.bind(1, ch.name);
    if (ch.description.empty()) {
        stmt.bind_null(2);
    } else {
        stmt.bind(2, ch.description);
    }
    stmt.bind(3, ch.discord_announcement_channel_id);
    if (ch.discord_lead_role_id.empty()) {
        stmt.bind_null(4);
    } else {
        stmt.bind(4, ch.discord_lead_role_id);
    }
    if (ch.discord_member_role_id.empty()) {
        stmt.bind_null(5);
    } else {
        stmt.bind(5, ch.discord_member_role_id);
    }
    if (ch.created_by == 0) {
        stmt.bind_null(6);
    } else {
        stmt.bind(6, ch.created_by);
    }
    stmt.step();

    int64_t new_id = db_.last_insert_rowid();
    auto result = find_by_id(new_id);
    if (!result) {
        throw DbError("Failed to retrieve inserted chapter with id=" + std::to_string(new_id));
    }
    return *result;
}

bool ChapterRepository::update(const Chapter& ch) {
    auto stmt = db_.prepare(
        "UPDATE chapters SET name=?, description=?, discord_announcement_channel_id=?, "
        "                    discord_lead_role_id=?, discord_member_role_id=? "
        "WHERE id=?");
    stmt.bind(1, ch.name);
    if (ch.description.empty()) {
        stmt.bind_null(2);
    } else {
        stmt.bind(2, ch.description);
    }
    stmt.bind(3, ch.discord_announcement_channel_id);
    if (ch.discord_lead_role_id.empty()) {
        stmt.bind_null(4);
    } else {
        stmt.bind(4, ch.discord_lead_role_id);
    }
    if (ch.discord_member_role_id.empty()) {
        stmt.bind_null(5);
    } else {
        stmt.bind(5, ch.discord_member_role_id);
    }
    stmt.bind(6, ch.id);
    stmt.step();

    auto existing = find_by_id(ch.id);
    return existing.has_value();
}

bool ChapterRepository::delete_by_id(int64_t id) {
    auto stmt = db_.prepare("DELETE FROM chapters WHERE id=?");
    stmt.bind(1, id);
    stmt.step();

    auto existing = find_by_id(id);
    return !existing.has_value();
}
