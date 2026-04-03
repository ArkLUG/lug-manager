#include "repositories/PerkLevelRepository.hpp"

static const char* kSelectAllCols =
    "SELECT id, name, COALESCE(discord_role_id,''), meeting_attendance_required, "
    "event_attendance_required, requires_paid_dues, COALESCE(min_fol_status,'kfol'), "
    "sort_order, created_at, updated_at "
    "FROM perk_levels";

// fol_rank() defined in header

PerkLevelRepository::PerkLevelRepository(SqliteDatabase& db) : db_(db) {}

PerkLevel PerkLevelRepository::row_to_perk(Statement& stmt) {
    PerkLevel p;
    p.id                          = stmt.col_int(0);
    p.name                        = stmt.col_text(1);
    p.discord_role_id             = stmt.col_text(2);
    p.meeting_attendance_required = static_cast<int>(stmt.col_int(3));
    p.event_attendance_required   = static_cast<int>(stmt.col_int(4));
    p.requires_paid_dues          = stmt.col_bool(5);
    p.min_fol_status              = stmt.col_text(6);
    p.sort_order                  = static_cast<int>(stmt.col_int(7));
    p.created_at                  = stmt.col_text(8);
    p.updated_at                  = stmt.col_text(9);
    return p;
}

std::vector<PerkLevel> PerkLevelRepository::find_all() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " ORDER BY sort_order ASC");
    std::vector<PerkLevel> result;
    while (stmt.step()) {
        result.push_back(row_to_perk(stmt));
    }
    return result;
}

std::optional<PerkLevel> PerkLevelRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        return row_to_perk(stmt);
    }
    return std::nullopt;
}

PerkLevel PerkLevelRepository::create(const PerkLevel& p) {
    auto stmt = db_.prepare(
        "INSERT INTO perk_levels (name, discord_role_id, meeting_attendance_required, "
        "event_attendance_required, requires_paid_dues, min_fol_status, sort_order) VALUES (?,?,?,?,?,?,?)");
    stmt.bind(1, p.name);
    stmt.bind(2, p.discord_role_id);
    stmt.bind(3, static_cast<int64_t>(p.meeting_attendance_required));
    stmt.bind(4, static_cast<int64_t>(p.event_attendance_required));
    stmt.bind(5, p.requires_paid_dues);
    stmt.bind(6, p.min_fol_status.empty() ? std::string("kfol") : p.min_fol_status);
    stmt.bind(7, static_cast<int64_t>(p.sort_order));
    stmt.step();

    int64_t new_id = db_.last_insert_rowid();
    auto result = find_by_id(new_id);
    if (!result) {
        throw DbError("Failed to retrieve inserted perk_level with id=" + std::to_string(new_id));
    }
    return *result;
}

bool PerkLevelRepository::update(const PerkLevel& p) {
    auto stmt = db_.prepare(
        "UPDATE perk_levels SET name=?, discord_role_id=?, meeting_attendance_required=?, "
        "event_attendance_required=?, requires_paid_dues=?, min_fol_status=?, sort_order=?, "
        "updated_at=datetime('now') WHERE id=?");
    stmt.bind(1, p.name);
    stmt.bind(2, p.discord_role_id);
    stmt.bind(3, static_cast<int64_t>(p.meeting_attendance_required));
    stmt.bind(4, static_cast<int64_t>(p.event_attendance_required));
    stmt.bind(5, p.requires_paid_dues);
    stmt.bind(6, p.min_fol_status.empty() ? std::string("kfol") : p.min_fol_status);
    stmt.bind(7, static_cast<int64_t>(p.sort_order));
    stmt.bind(8, p.id);
    stmt.step();

    return find_by_id(p.id).has_value();
}

bool PerkLevelRepository::remove(int64_t id) {
    auto stmt = db_.prepare("DELETE FROM perk_levels WHERE id=?");
    stmt.bind(1, id);
    stmt.step();
    return !find_by_id(id).has_value();
}
