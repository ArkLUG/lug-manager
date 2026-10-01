#pragma once
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "models/Attendance.hpp"
#include "models/Member.hpp"
#include "models/Meeting.hpp"
#include <vector>
#include <unordered_map>
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
    // Attendance counts for many meetings/events in one query (list pages).
    // Events count distinct members across days, like get_count().
    std::unordered_map<int64_t, int> counts_for(const std::string& entity_type,
                                                const std::vector<int64_t>& ids);
    // Whether members may check themselves in to this meeting right now (QR
    // link or the meeting page): not cancelled/virtual-QR rules aside, only
    // on the meeting's date, +/-1 day since meeting times are LUG-local and
    // the server clock may be UTC. Admin/lead check-ins are not limited.
    static bool meeting_self_checkin_open(const Meeting& m);

private:
    AttendanceRepository&          repo_;
    MemberRepository&              member_repo_;
    EventRepository&               event_repo_;
    EventDayRepository&            event_day_repo_;
    EventDayAttendanceRepository&  event_day_attendance_repo_;
};
