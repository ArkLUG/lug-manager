#include "repositories/EventDayAttendanceRepository.hpp"

EventDayAttendanceRepository::EventDayAttendanceRepository(SqliteDatabase& db) : db_(db) {}

bool EventDayAttendanceRepository::check_in(int64_t event_day_id, int64_t member_id,
                                              const std::string& notes) {
    auto stmt = db_.prepare(
        "INSERT OR IGNORE INTO event_day_attendance (event_day_id, member_id, notes) "
        "VALUES (?,?,?)");
    stmt.bind(1, event_day_id);
    stmt.bind(2, member_id);
    stmt.bind(3, notes);
    stmt.step();
    return db_.last_insert_rowid() != 0;
}

bool EventDayAttendanceRepository::is_checked_in(int64_t event_day_id, int64_t member_id) {
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM event_day_attendance WHERE event_day_id=? AND member_id=?");
    stmt.bind(1, event_day_id);
    stmt.bind(2, member_id);
    if (stmt.step()) return stmt.col_int(0) > 0;
    return false;
}

std::vector<EventDayAttendance> EventDayAttendanceRepository::find_by_event(int64_t event_id) {
    auto stmt = db_.prepare(
        "SELECT eda.id, eda.event_day_id, eda.member_id, eda.checked_in_at, eda.notes, "
        "       eda.qualifies, m.display_name, m.discord_username, ed.day_date, ed.day_number "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "JOIN members m ON m.id = eda.member_id "
        "WHERE ed.event_id = ? "
        "ORDER BY ed.day_number ASC, eda.checked_in_at ASC");
    stmt.bind(1, event_id);
    std::vector<EventDayAttendance> result;
    while (stmt.step()) {
        EventDayAttendance a;
        a.id                      = stmt.col_int(0);
        a.event_day_id            = stmt.col_int(1);
        a.member_id               = stmt.col_int(2);
        a.checked_in_at           = stmt.col_text(3);
        a.notes                   = stmt.col_text(4);
        a.qualifies               = stmt.col_int(5) != 0;
        a.member_display_name     = stmt.col_text(6);
        a.member_discord_username = stmt.col_text(7);
        a.day_date                = stmt.col_text(8);
        a.day_number              = static_cast<int>(stmt.col_int(9));
        result.push_back(a);
    }
    return result;
}

std::vector<EventDayAttendance> EventDayAttendanceRepository::find_by_day(int64_t event_day_id) {
    auto stmt = db_.prepare(
        "SELECT eda.id, eda.event_day_id, eda.member_id, eda.checked_in_at, eda.notes, "
        "       eda.qualifies, m.display_name, m.discord_username, ed.day_date, ed.day_number "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "JOIN members m ON m.id = eda.member_id "
        "WHERE eda.event_day_id = ? "
        "ORDER BY eda.checked_in_at ASC");
    stmt.bind(1, event_day_id);
    std::vector<EventDayAttendance> result;
    while (stmt.step()) {
        EventDayAttendance a;
        a.id                      = stmt.col_int(0);
        a.event_day_id            = stmt.col_int(1);
        a.member_id               = stmt.col_int(2);
        a.checked_in_at           = stmt.col_text(3);
        a.notes                   = stmt.col_text(4);
        a.qualifies               = stmt.col_int(5) != 0;
        a.member_display_name     = stmt.col_text(6);
        a.member_discord_username = stmt.col_text(7);
        a.day_date                = stmt.col_text(8);
        a.day_number              = static_cast<int>(stmt.col_int(9));
        result.push_back(a);
    }
    return result;
}

int EventDayAttendanceRepository::count_distinct_members_by_event(int64_t event_id) {
    auto stmt = db_.prepare(
        "SELECT COUNT(DISTINCT eda.member_id) "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "WHERE ed.event_id = ?");
    stmt.bind(1, event_id);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

bool EventDayAttendanceRepository::member_qualifies_for_event(int64_t member_id, int64_t event_id) {
    auto stmt = db_.prepare(
        "SELECT EXISTS("
        "  SELECT 1 FROM event_day_attendance eda "
        "  JOIN event_days ed ON ed.id = eda.event_day_id "
        "  WHERE eda.member_id=? AND ed.event_id=? AND eda.qualifies=1"
        ")");
    stmt.bind(1, member_id);
    stmt.bind(2, event_id);
    if (stmt.step()) return stmt.col_int(0) != 0;
    return false;
}

bool EventDayAttendanceRepository::set_qualifies(int64_t attendance_id, bool qualifies) {
    auto stmt = db_.prepare("UPDATE event_day_attendance SET qualifies=? WHERE id=?");
    stmt.bind(1, static_cast<int64_t>(qualifies ? 1 : 0));
    stmt.bind(2, attendance_id);
    stmt.step();
    return true;
}

bool EventDayAttendanceRepository::remove_by_id(int64_t attendance_id) {
    auto stmt = db_.prepare("DELETE FROM event_day_attendance WHERE id=?");
    stmt.bind(1, attendance_id);
    stmt.step();
    return true;
}

std::optional<EventDayAttendance> EventDayAttendanceRepository::find_by_id(int64_t attendance_id) {
    auto stmt = db_.prepare(
        "SELECT eda.id, eda.event_day_id, eda.member_id, eda.checked_in_at, eda.notes, "
        "       eda.qualifies, COALESCE(m.display_name,''), COALESCE(m.discord_username,''), "
        "       ed.day_date, ed.day_number "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "LEFT JOIN members m ON m.id = eda.member_id "
        "WHERE eda.id = ?");
    stmt.bind(1, attendance_id);
    if (stmt.step()) {
        EventDayAttendance a;
        a.id                      = stmt.col_int(0);
        a.event_day_id            = stmt.col_int(1);
        a.member_id               = stmt.col_int(2);
        a.checked_in_at           = stmt.col_text(3);
        a.notes                   = stmt.col_text(4);
        a.qualifies               = stmt.col_int(5) != 0;
        a.member_display_name     = stmt.col_text(6);
        a.member_discord_username = stmt.col_text(7);
        a.day_date                = stmt.col_text(8);
        a.day_number              = static_cast<int>(stmt.col_int(9));
        return a;
    }
    return std::nullopt;
}

int EventDayAttendanceRepository::count_events_credited_for_member(int64_t member_id, int year) {
    std::string year_start = std::to_string(year) + "-01-01";
    std::string year_end   = std::to_string(year + 1) + "-01-01";
    auto stmt = db_.prepare(
        "SELECT COUNT(DISTINCT ed.event_id) "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "WHERE eda.member_id=? AND eda.qualifies=1 "
        "  AND ed.day_date >= ? AND ed.day_date < ?");
    stmt.bind(1, member_id);
    stmt.bind(2, year_start);
    stmt.bind(3, year_end);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}
