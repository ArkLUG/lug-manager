#include "services/AttendanceService.hpp"
#include <ctime>
#include <cstdio>

AttendanceService::AttendanceService(AttendanceRepository& repo, MemberRepository& member_repo,
                                       EventRepository& event_repo,
                                       EventDayRepository& event_day_repo,
                                       EventDayAttendanceRepository& event_day_attendance_repo)
    : repo_(repo), member_repo_(member_repo), event_repo_(event_repo),
      event_day_repo_(event_day_repo), event_day_attendance_repo_(event_day_attendance_repo) {}

// static
std::string AttendanceService::today_ymd() {
    std::time_t now = std::time(nullptr);
    std::tm* tm_now = std::localtime(&now);
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday);
    return buf;
}

bool AttendanceService::check_in(int64_t member_id, const std::string& entity_type,
                                  int64_t entity_id, const std::string& notes,
                                  bool is_virtual) {
    if (entity_type == "event") {
        // Only check in if today matches one of the event's days.
        std::string today = today_ymd();
        auto day = event_day_repo_.find_by_event_and_date(entity_id, today);
        if (!day) return false;
        return event_day_attendance_repo_.check_in(day->id, member_id, notes);
    }
    return repo_.check_in(member_id, entity_type, entity_id, notes, is_virtual);
}

bool AttendanceService::check_out(int64_t member_id, const std::string& entity_type,
                                   int64_t entity_id) {
    if (entity_type == "event") {
        // Remove all day-attendance rows for this member on this event.
        auto days = event_day_repo_.find_by_event(entity_id);
        for (const auto& d : days) {
            auto rows = event_day_attendance_repo_.find_by_day(d.id);
            for (const auto& r : rows) {
                if (r.member_id == member_id) {
                    event_day_attendance_repo_.remove_by_id(r.id);
                }
            }
        }
        return true;
    }
    return repo_.check_out(member_id, entity_type, entity_id);
}

std::vector<Attendance> AttendanceService::get_attendees(const std::string& entity_type,
                                                          int64_t entity_id) {
    if (entity_type == "event") {
        // Project event-day attendance into the legacy Attendance shape so
        // existing callers (and templates) still work. Each row represents a
        // single day of attendance.
        auto rows = event_day_attendance_repo_.find_by_event(entity_id);
        std::vector<Attendance> out;
        out.reserve(rows.size());
        for (const auto& r : rows) {
            Attendance a;
            a.id                      = r.id;
            a.member_id               = r.member_id;
            a.entity_type             = "event";
            a.entity_id               = entity_id;
            a.checked_in_at           = r.checked_in_at;
            a.notes                   = r.notes;
            a.is_virtual              = false;
            a.member_display_name     = r.member_display_name;
            a.member_discord_username = r.member_discord_username;
            out.push_back(a);
        }
        return out;
    }
    return repo_.find_by_entity(entity_type, entity_id);
}

int AttendanceService::get_count(const std::string& entity_type, int64_t entity_id) {
    if (entity_type == "event") {
        return event_day_attendance_repo_.count_distinct_members_by_event(entity_id);
    }
    return repo_.count_by_entity(entity_type, entity_id);
}

bool AttendanceService::is_checked_in(int64_t member_id, const std::string& entity_type,
                                       int64_t entity_id) {
    if (entity_type == "event") {
        auto days = event_day_repo_.find_by_event(entity_id);
        for (const auto& d : days) {
            if (event_day_attendance_repo_.is_checked_in(d.id, member_id)) return true;
        }
        return false;
    }
    return repo_.is_checked_in(member_id, entity_type, entity_id);
}

bool AttendanceService::set_virtual(int64_t attendance_id, bool is_virtual) {
    return repo_.set_virtual(attendance_id, is_virtual);
}

bool AttendanceService::remove_by_id(int64_t attendance_id) {
    return repo_.remove_by_id(attendance_id);
}

std::vector<Attendance> AttendanceService::get_member_history(int64_t member_id) {
    // Combine meeting history from the attendance table with event-day
    // attendance. Each event day becomes its own row in the history.
    auto meetings = repo_.find_by_member(member_id);
    // Fetch event day rows for this member.
    // We do a separate query via the repo to keep this focused.
    std::vector<Attendance> combined = meetings;
    // Pull all event day rows by scanning event_day_attendance joined with event_days.
    // Reuse member_attendance_detail is heavier; instead, query directly.
    // Simpler: leverage find_by_event across events the member touched — but that
    // requires listing events. For history display, a small ad-hoc query is fine.
    auto& db = repo_.db();
    auto stmt = db.prepare(
        "SELECT eda.id, eda.member_id, ed.event_id, eda.checked_in_at, eda.notes "
        "FROM event_day_attendance eda "
        "JOIN event_days ed ON ed.id = eda.event_day_id "
        "WHERE eda.member_id=? "
        "ORDER BY eda.checked_in_at DESC");
    stmt.bind(1, member_id);
    while (stmt.step()) {
        Attendance a;
        a.id            = stmt.col_int(0);
        a.member_id     = stmt.col_int(1);
        a.entity_type   = "event";
        a.entity_id     = stmt.col_int(2);
        a.checked_in_at = stmt.col_text(3);
        a.notes         = stmt.col_text(4);
        a.is_virtual    = false;
        combined.push_back(a);
    }
    return combined;
}

std::vector<AttendanceRepository::MemberAttendanceSummary> AttendanceService::get_all_member_summaries() {
    return repo_.get_all_member_summaries();
}

std::vector<AttendanceRepository::MemberAttendanceSummary> AttendanceService::get_all_member_summaries_by_year(int year) {
    return repo_.get_all_member_summaries_by_year(year);
}

std::vector<AttendanceRepository::MemberAttendanceSummary> AttendanceService::get_overview_paginated(const AttendanceRepository::OverviewParams& p) {
    return repo_.get_overview_paginated(p);
}

int AttendanceService::count_overview(const AttendanceRepository::OverviewParams& p) {
    return repo_.count_overview(p);
}

std::vector<int> AttendanceService::get_attendance_years() {
    return repo_.get_attendance_years();
}

bool AttendanceService::check_in_to_day(int64_t member_id, int64_t event_day_id) {
    return event_day_attendance_repo_.check_in(event_day_id, member_id);
}
