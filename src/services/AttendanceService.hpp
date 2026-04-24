#pragma once
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "models/Attendance.hpp"
#include "models/Member.hpp"
#include <vector>
#include <optional>

class AttendanceService {
public:
    AttendanceService(AttendanceRepository& repo, MemberRepository& member_repo,
                      EventRepository& event_repo,
                      EventDayRepository& event_day_repo,
                      EventDayAttendanceRepository& event_day_attendance_repo);

    // When entity_type == "event", writes go to the event_day_attendance table
    // for the event day matching today's date (or the event's start date if
    // today is outside the event range). Returns false if no matching day row
    // exists. Reads aggregate across all days of the event.
    bool   check_in(int64_t member_id, const std::string& entity_type,
                    int64_t entity_id, const std::string& notes = "",
                    bool is_virtual = false);
    bool   check_out(int64_t member_id, const std::string& entity_type, int64_t entity_id);

    std::vector<Attendance> get_attendees(const std::string& entity_type, int64_t entity_id);
    int    get_count(const std::string& entity_type, int64_t entity_id);
    bool   is_checked_in(int64_t member_id, const std::string& entity_type, int64_t entity_id);
    bool   set_virtual(int64_t attendance_id, bool is_virtual);
    bool   remove_by_id(int64_t attendance_id);
    std::vector<Attendance> get_member_history(int64_t member_id);
    std::vector<AttendanceRepository::MemberAttendanceSummary> get_all_member_summaries();
    std::vector<AttendanceRepository::MemberAttendanceSummary> get_all_member_summaries_by_year(int year);
    std::vector<AttendanceRepository::MemberAttendanceSummary> get_overview_paginated(const AttendanceRepository::OverviewParams& p);
    int count_overview(const AttendanceRepository::OverviewParams& p);
    std::vector<int> get_attendance_years();
    AttendanceRepository& repo() { return repo_; }

    // Event-day specific APIs
    EventDayRepository& event_day_repo() { return event_day_repo_; }
    EventDayAttendanceRepository& event_day_attendance_repo() { return event_day_attendance_repo_; }

    // Check a member in to a specific day of an event (admin-driven).
    bool check_in_to_day(int64_t member_id, int64_t event_day_id);

    // Returns today's YYYY-MM-DD in local time.
    static std::string today_ymd();

private:
    AttendanceRepository&          repo_;
    MemberRepository&              member_repo_;
    EventRepository&               event_repo_;
    EventDayRepository&            event_day_repo_;
    EventDayAttendanceRepository&  event_day_attendance_repo_;
};
