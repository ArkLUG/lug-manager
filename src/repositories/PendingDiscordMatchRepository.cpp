#include "repositories/PendingDiscordMatchRepository.hpp"

static const char* kSelectAllCols =
    "SELECT id, discord_user_id, discord_username, discord_display_name, discord_role_ids, "
    "suggested_member_id, created_at, resolved_at, resolved_action, resolved_member_id "
    "FROM pending_discord_matches";

PendingDiscordMatchRepository::PendingDiscordMatchRepository(SqliteDatabase& db) : db_(db) {}

// static
PendingDiscordMatch PendingDiscordMatchRepository::row_to_match(Statement& stmt) {
    PendingDiscordMatch p;
    p.id                   = stmt.col_int(0);
    p.discord_user_id      = stmt.col_text(1);
    p.discord_username     = stmt.col_text(2);
    p.discord_display_name = stmt.col_text(3);
    p.discord_role_ids     = stmt.col_text(4);
    p.suggested_member_id  = stmt.col_is_null(5) ? 0 : stmt.col_int(5);
    p.created_at            = stmt.col_text(6);
    p.resolved_at            = stmt.col_is_null(7) ? "" : stmt.col_text(7);
    p.resolved_action        = stmt.col_is_null(8) ? "" : stmt.col_text(8);
    p.resolved_member_id     = stmt.col_is_null(9) ? 0 : stmt.col_int(9);
    return p;
}

std::vector<PendingDiscordMatch> PendingDiscordMatchRepository::find_all_unresolved() const {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE resolved_at IS NULL ORDER BY created_at ASC");
    std::vector<PendingDiscordMatch> result;
    while (stmt.step()) result.push_back(row_to_match(stmt));
    return result;
}

std::optional<PendingDiscordMatch> PendingDiscordMatchRepository::find_by_id(int64_t id) const {
    auto stmt = db_.prepare(std::string(kSelectAllCols) + " WHERE id = ?");
    stmt.bind(1, id);
    if (stmt.step()) return row_to_match(stmt);
    return std::nullopt;
}

std::optional<PendingDiscordMatch> PendingDiscordMatchRepository::find_unresolved_by_discord_id(
        const std::string& discord_user_id) const {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE discord_user_id = ? AND resolved_at IS NULL");
    stmt.bind(1, discord_user_id);
    if (stmt.step()) return row_to_match(stmt);
    return std::nullopt;
}

PendingDiscordMatch PendingDiscordMatchRepository::create(const PendingDiscordMatch& p) {
    auto stmt = db_.prepare(
        "INSERT INTO pending_discord_matches "
        "(discord_user_id, discord_username, discord_display_name, discord_role_ids, suggested_member_id) "
        "VALUES (?,?,?,?,?)");
    stmt.bind(1, p.discord_user_id);
    stmt.bind(2, p.discord_username);
    stmt.bind(3, p.discord_display_name);
    stmt.bind(4, p.discord_role_ids);
    if (p.suggested_member_id == 0) stmt.bind_null(5);
    else                            stmt.bind(5, p.suggested_member_id);
    stmt.step();

    int64_t id = db_.last_insert_rowid();
    auto created = find_by_id(id);
    return created ? *created : PendingDiscordMatch{};
}

bool PendingDiscordMatchRepository::mark_resolved(int64_t id, const std::string& action,
                                                   int64_t resolved_member_id) {
    auto stmt = db_.prepare(
        "UPDATE pending_discord_matches "
        "SET resolved_at = datetime('now'), resolved_action = ?, resolved_member_id = ? "
        "WHERE id = ?");
    stmt.bind(1, action);
    stmt.bind(2, resolved_member_id);
    stmt.bind(3, id);
    stmt.step();
    return true;
}
