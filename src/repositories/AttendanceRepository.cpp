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

std::vector<AttendanceRepository::MemberAttendanceSummary>
AttendanceRepository::get_all_member_summaries() {
    auto stmt = db_.prepare(
        "SELECT m.id, m.display_name, m.discord_username, "
        "SUM(CASE WHEN a.entity_type='meeting' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN a.entity_type='meeting' AND a.is_virtual=1 THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN a.entity_type='event' THEN 1 ELSE 0 END) "
        "FROM members m "
        "LEFT JOIN attendance a ON a.member_id = m.id "
        "GROUP BY m.id "
        "ORDER BY m.display_name ASC");

    std::vector<MemberAttendanceSummary> result;
    while (stmt.step()) {
        MemberAttendanceSummary s;
        s.member_id             = stmt.col_int(0);
        s.display_name          = stmt.col_text(1);
        s.discord_username      = stmt.col_text(2);
        s.meeting_count         = static_cast<int>(stmt.col_int(3));
        s.meeting_virtual_count = static_cast<int>(stmt.col_int(4));
        s.event_count           = static_cast<int>(stmt.col_int(5));
        result.push_back(s);
    }
    return result;
}

std::vector<AttendanceRepository::MemberAttendanceSummary>
AttendanceRepository::get_all_member_summaries_by_year(int year) {
    std::string year_start = std::to_string(year) + "-01-01";
    std::string year_end   = std::to_string(year + 1) + "-01-01";
    auto stmt = db_.prepare(
        "SELECT m.id, m.display_name, m.discord_username, "
        "SUM(CASE WHEN a.entity_type='meeting' THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN a.entity_type='meeting' AND a.is_virtual=1 THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN a.entity_type='event' THEN 1 ELSE 0 END) "
        "FROM members m "
        "LEFT JOIN attendance a ON a.member_id = m.id "
        "AND a.checked_in_at >= ? AND a.checked_in_at < ? "
        "GROUP BY m.id "
        "ORDER BY m.display_name ASC");
    stmt.bind(1, year_start);
    stmt.bind(2, year_end);

    std::vector<MemberAttendanceSummary> result;
    while (stmt.step()) {
        MemberAttendanceSummary s;
        s.member_id             = stmt.col_int(0);
        s.display_name          = stmt.col_text(1);
        s.discord_username      = stmt.col_text(2);
        s.meeting_count         = static_cast<int>(stmt.col_int(3));
        s.meeting_virtual_count = static_cast<int>(stmt.col_int(4));
        s.event_count           = static_cast<int>(stmt.col_int(5));
        result.push_back(s);
    }
    return result;
}

std::vector<int> AttendanceRepository::get_attendance_years() {
    auto stmt = db_.prepare(
        "SELECT DISTINCT CAST(strftime('%Y', checked_in_at) AS INTEGER) AS yr "
        "FROM attendance ORDER BY yr DESC");
    std::vector<int> result;
    while (stmt.step()) {
        result.push_back(static_cast<int>(stmt.col_int(0)));
    }
    return result;
}

// Build the common CTE + WHERE for overview queries
static std::string build_overview_sql(const AttendanceRepository::OverviewParams& p,
                                       bool count_only) {
    std::string year_start = std::to_string(p.year) + "-01-01";
    std::string year_end   = std::to_string(p.year + 1) + "-01-01";

    // CTE computes per-member attendance stats for the year
    std::string sql =
        "WITH stats AS ("
        "  SELECT m.id AS member_id, m.display_name, "
        "         COALESCE(m.first_name,'') AS first_name, COALESCE(m.last_name,'') AS last_name, "
        "         m.discord_username, m.is_paid, "
        "         COALESCE(m.fol_status,'afol') AS fol_status, "
        "         SUM(CASE WHEN a.entity_type='meeting' THEN 1 ELSE 0 END) AS meeting_count, "
        "         SUM(CASE WHEN a.entity_type='meeting' AND a.is_virtual=1 THEN 1 ELSE 0 END) AS meeting_virtual_count, "
        "         SUM(CASE WHEN a.entity_type='event' THEN 1 ELSE 0 END) AS event_count, "
        "         MAX(SUBSTR(CASE WHEN a.entity_type='meeting' THEN COALESCE(mt.start_time,'') "
        "                         ELSE COALESCE(ev.start_time,'') END, 1, 10)) AS last_attendance "
        "  FROM members m "
        "  LEFT JOIN attendance a ON a.member_id = m.id "
        "    AND a.checked_in_at >= '" + year_start + "' AND a.checked_in_at < '" + year_end + "' "
        "  LEFT JOIN meetings mt ON a.entity_type='meeting' AND mt.id = a.entity_id "
        "  LEFT JOIN lug_events ev ON a.entity_type='event' AND ev.id = a.entity_id "
        "  GROUP BY m.id"
        ") ";

    if (count_only) {
        sql += "SELECT COUNT(*) FROM stats WHERE 1=1";
    } else {
        sql += "SELECT member_id, display_name, first_name, last_name, discord_username, "
               "meeting_count, meeting_virtual_count, event_count, COALESCE(last_attendance,''), "
               "is_paid, fol_status FROM stats WHERE 1=1";
    }

    // Search filter
    if (!p.search.empty()) {
        sql += " AND (display_name LIKE '%" + p.search + "%' OR discord_username LIKE '%" + p.search + "%'"
               " OR first_name LIKE '%" + p.search + "%' OR last_name LIKE '%" + p.search + "%')";
    }

    // Hide inactive: no attendance AND not paid
    if (p.hide_inactive) {
        sql += " AND (meeting_count + event_count > 0 OR is_paid = 1)";
    }

    if (!count_only) {
        // Sort — whitelist columns
        std::string col = "display_name";
        if (p.sort_col == "meeting_count") col = "meeting_count";
        else if (p.sort_col == "event_count") col = "event_count";
        else if (p.sort_col == "total") col = "(meeting_count + event_count)";
        else if (p.sort_col == "last_attendance") col = "last_attendance";

        std::string dir = (p.sort_dir == "desc") ? "DESC" : "ASC";
        sql += " ORDER BY " + col + " " + dir;
        sql += " LIMIT " + std::to_string(p.limit) + " OFFSET " + std::to_string(p.offset);
    }

    return sql;
}

std::vector<AttendanceRepository::MemberAttendanceSummary>
AttendanceRepository::get_overview_paginated(const OverviewParams& p) {
    auto stmt = db_.prepare(build_overview_sql(p, false));
    std::vector<MemberAttendanceSummary> result;
    while (stmt.step()) {
        MemberAttendanceSummary s;
        s.member_id             = stmt.col_int(0);
        s.display_name          = stmt.col_text(1);
        s.first_name            = stmt.col_text(2);
        s.last_name             = stmt.col_text(3);
        s.discord_username      = stmt.col_text(4);
        s.meeting_count         = static_cast<int>(stmt.col_int(5));
        s.meeting_virtual_count = static_cast<int>(stmt.col_int(6));
        s.event_count           = static_cast<int>(stmt.col_int(7));
        s.last_attendance       = stmt.col_text(8);
        s.is_paid               = stmt.col_bool(9);
        s.fol_status            = stmt.col_text(10);
        result.push_back(s);
    }
    return result;
}

int AttendanceRepository::count_overview(const OverviewParams& p) {
    auto stmt = db_.prepare(build_overview_sql(p, true));
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

int AttendanceRepository::count_member_by_year(int64_t member_id, int year,
                                                const std::string& entity_type) {
    std::string year_start = std::to_string(year) + "-01-01";
    std::string year_end   = std::to_string(year + 1) + "-01-01";
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM attendance "
        "WHERE member_id=? AND entity_type=? AND checked_in_at >= ? AND checked_in_at < ?");
    stmt.bind(1, member_id);
    stmt.bind(2, entity_type);
    stmt.bind(3, year_start);
    stmt.bind(4, year_end);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

std::vector<AttendanceRepository::AttendanceDetail>
AttendanceRepository::get_member_attendance_detail(int64_t member_id, int year,
                                                    int limit, int offset) {
    std::string year_start = std::to_string(year) + "-01-01";
    std::string year_end   = std::to_string(year + 1) + "-01-01";
    auto stmt = db_.prepare(
        "SELECT a.entity_type, a.entity_id, "
        "  CASE WHEN a.entity_type='meeting' THEN COALESCE(mt.title,'') ELSE COALESCE(ev.title,'') END, "
        "  CASE WHEN a.entity_type='meeting' THEN SUBSTR(COALESCE(mt.start_time,''),1,10) ELSE SUBSTR(COALESCE(ev.start_time,''),1,10) END, "
        "  SUBSTR(a.checked_in_at,1,10), a.is_virtual "
        "FROM attendance a "
        "LEFT JOIN meetings mt ON a.entity_type='meeting' AND mt.id=a.entity_id "
        "LEFT JOIN lug_events ev ON a.entity_type='event' AND ev.id=a.entity_id "
        "WHERE a.member_id=? AND a.checked_in_at >= ? AND a.checked_in_at < ? "
        "ORDER BY a.checked_in_at DESC LIMIT ? OFFSET ?");
    stmt.bind(1, member_id);
    stmt.bind(2, year_start);
    stmt.bind(3, year_end);
    stmt.bind(4, static_cast<int64_t>(limit));
    stmt.bind(5, static_cast<int64_t>(offset));

    std::vector<AttendanceDetail> result;
    while (stmt.step()) {
        AttendanceDetail d;
        d.entity_type  = stmt.col_text(0);
        d.entity_id    = stmt.col_int(1);
        d.title        = stmt.col_text(2);
        d.date         = stmt.col_text(3);
        d.checked_in_at = stmt.col_text(4);
        d.is_virtual   = stmt.col_bool(5);
        result.push_back(d);
    }
    return result;
}

int AttendanceRepository::count_member_attendance_detail(int64_t member_id, int year) {
    std::string year_start = std::to_string(year) + "-01-01";
    std::string year_end   = std::to_string(year + 1) + "-01-01";
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM attendance WHERE member_id=? AND checked_in_at >= ? AND checked_in_at < ?");
    stmt.bind(1, member_id);
    stmt.bind(2, year_start);
    stmt.bind(3, year_end);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
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
