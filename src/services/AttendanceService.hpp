#pragma once
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "models/Attendance.hpp"
#include "models/Member.hpp"
#include <vector>
#include <optional>

class AttendanceService {
public:
    AttendanceService(AttendanceRepository& repo, MemberRepository& member_repo);

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

private:
    AttendanceRepository& repo_;
    MemberRepository&     member_repo_;
};
