#include "repositories/AttendanceRepository.hpp"

AttendanceRepository::AttendanceRepository(SqliteDatabase& db) : db_(db) {}

bool AttendanceRepository::check_in(int64_t member_id, const std::string& entity_type,
                                     int64_t entity_id, const std::string& notes,
                                     bool is_virtual) {
    // Use INSERT OR IGNORE to silently skip duplicate check-ins
    auto stmt = db_.prepare(
        "INSERT OR IGNORE INTO attendance (member_id, entity_type, entity_id, notes, is_virtual) "
        "VALUES (?,?,?,?,?)");
    stmt.bind(1, member_id);
    stmt.bind(2, entity_type);
    stmt.bind(3, entity_id);
    stmt.bind(4, notes);
    stmt.bind(5, static_cast<int64_t>(is_virtual ? 1 : 0));
    stmt.step();

    // If a new row was inserted, last_insert_rowid() will be non-zero and
    // the count query will find the record. We detect success by checking
    // whether a row now exists (was just inserted vs already existed).
    // A simpler approach: check rowid change — if rowid > 0 after insert, row was new.
    // Since INSERT OR IGNORE sets rowid=0 on conflict, we check last_insert_rowid().
    return db_.last_insert_rowid() != 0;
}

bool AttendanceRepository::check_out(int64_t member_id, const std::string& entity_type,
                                      int64_t entity_id) {
    auto stmt = db_.prepare(
        "DELETE FROM attendance WHERE member_id=? AND entity_type=? AND entity_id=?");
    stmt.bind(1, member_id);
    stmt.bind(2, entity_type);
    stmt.bind(3, entity_id);
    stmt.step();

    // Return true if the row no longer exists (successfully deleted)
    return !is_checked_in(member_id, entity_type, entity_id);
}

std::vector<Attendance> AttendanceRepository::find_by_entity(const std::string& entity_type,
                                                               int64_t entity_id) {
    auto stmt = db_.prepare(
        "SELECT a.id, a.member_id, a.entity_type, a.entity_id, a.checked_in_at, a.notes, "
        "m.display_name, m.discord_username, a.is_virtual "
        "FROM attendance a "
        "JOIN members m ON m.id = a.member_id "
        "WHERE a.entity_type=? AND a.entity_id=? "
        "ORDER BY a.checked_in_at ASC");
    stmt.bind(1, entity_type);
    stmt.bind(2, entity_id);

    std::vector<Attendance> result;
    while (stmt.step()) {
        Attendance a;
        a.id                      = stmt.col_int(0);
        a.member_id               = stmt.col_int(1);
        a.entity_type             = stmt.col_text(2);
        a.entity_id               = stmt.col_int(3);
        a.checked_in_at           = stmt.col_text(4);
        a.notes                   = stmt.col_text(5);
        a.member_display_name     = stmt.col_text(6);
        a.member_discord_username = stmt.col_text(7);
        a.is_virtual              = stmt.col_int(8) != 0;
        result.push_back(a);
    }
    return result;
}

std::vector<Attendance> AttendanceRepository::find_by_member(int64_t member_id) {
    auto stmt = db_.prepare(
        "SELECT id, member_id, entity_type, entity_id, checked_in_at, notes, is_virtual "
        "FROM attendance WHERE member_id=? ORDER BY checked_in_at DESC");
    stmt.bind(1, member_id);

    std::vector<Attendance> result;
    while (stmt.step()) {
        Attendance a;
        a.id            = stmt.col_int(0);
        a.member_id     = stmt.col_int(1);
        a.entity_type   = stmt.col_text(2);
        a.entity_id     = stmt.col_int(3);
        a.checked_in_at = stmt.col_text(4);
        a.notes         = stmt.col_text(5);
        a.is_virtual    = stmt.col_int(6) != 0;
        result.push_back(a);
    }
    return result;
}

int AttendanceRepository::count_by_entity(const std::string& entity_type, int64_t entity_id) {
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM attendance WHERE entity_type=? AND entity_id=?");
    stmt.bind(1, entity_type);
    stmt.bind(2, entity_id);
    if (stmt.step()) {
        return static_cast<int>(stmt.col_int(0));
    }
    return 0;
}

bool AttendanceRepository::set_virtual(int64_t attendance_id, bool is_virtual) {
    auto stmt = db_.prepare("UPDATE attendance SET is_virtual=? WHERE id=?");
    stmt.bind(1, static_cast<int64_t>(is_virtual ? 1 : 0));
    stmt.bind(2, attendance_id);
    stmt.step();
    return true;
}

bool AttendanceRepository::remove_by_id(int64_t attendance_id) {
    auto stmt = db_.prepare("DELETE FROM attendance WHERE id=?");
    stmt.bind(1, attendance_id);
    stmt.step();
    return true;
}

void AttendanceRepository::delete_by_entity(const std::string& entity_type, int64_t entity_id) {
    auto stmt = db_.prepare("DELETE FROM attendance WHERE entity_type=? AND entity_id=?");
    stmt.bind(1, entity_type);
    stmt.bind(2, entity_id);
    stmt.step();
}

bool AttendanceRepository::is_checked_in(int64_t member_id, const std::string& entity_type,
                                          int64_t entity_id) {
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM attendance WHERE member_id=? AND entity_type=? AND entity_id=?");
    stmt.bind(1, member_id);
    stmt.bind(2, entity_type);
    stmt.bind(3, entity_id);
    if (stmt.step()) {
        return stmt.col_int(0) > 0;
    }
    return false;
}
