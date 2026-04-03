#include "services/AttendanceService.hpp"

AttendanceService::AttendanceService(AttendanceRepository& repo, MemberRepository& member_repo)
    : repo_(repo), member_repo_(member_repo) {}

bool AttendanceService::check_in(int64_t member_id, const std::string& entity_type,
                                  int64_t entity_id, const std::string& notes,
                                  bool is_virtual) {
    return repo_.check_in(member_id, entity_type, entity_id, notes, is_virtual);
}

bool AttendanceService::check_out(int64_t member_id, const std::string& entity_type,
                                   int64_t entity_id) {
    return repo_.check_out(member_id, entity_type, entity_id);
}

std::vector<Attendance> AttendanceService::get_attendees(const std::string& entity_type,
                                                          int64_t entity_id) {
    return repo_.find_by_entity(entity_type, entity_id);
}

int AttendanceService::get_count(const std::string& entity_type, int64_t entity_id) {
    return repo_.count_by_entity(entity_type, entity_id);
}

bool AttendanceService::is_checked_in(int64_t member_id, const std::string& entity_type,
                                       int64_t entity_id) {
    return repo_.is_checked_in(member_id, entity_type, entity_id);
}

bool AttendanceService::set_virtual(int64_t attendance_id, bool is_virtual) {
    return repo_.set_virtual(attendance_id, is_virtual);
}

bool AttendanceService::remove_by_id(int64_t attendance_id) {
    return repo_.remove_by_id(attendance_id);
}

std::vector<Attendance> AttendanceService::get_member_history(int64_t member_id) {
    return repo_.find_by_member(member_id);
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
