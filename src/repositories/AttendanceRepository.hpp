#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Attendance.hpp"
#include <vector>
#include <optional>
#include <string>

class AttendanceRepository {
public:
    explicit AttendanceRepository(SqliteDatabase& db);

    bool check_in(int64_t member_id, const std::string& entity_type,
                  int64_t entity_id, const std::string& notes = "",
                  bool is_virtual = false);

    bool check_out(int64_t member_id, const std::string& entity_type, int64_t entity_id);

    // Returns attendance records with member display_name joined
    std::vector<Attendance> find_by_entity(const std::string& entity_type, int64_t entity_id);
    std::vector<Attendance> find_by_member(int64_t member_id);

    int  count_by_entity(const std::string& entity_type, int64_t entity_id);
    bool is_checked_in(int64_t member_id, const std::string& entity_type, int64_t entity_id);
    bool set_virtual(int64_t attendance_id, bool is_virtual);
    bool remove_by_id(int64_t attendance_id);
    void delete_by_entity(const std::string& entity_type, int64_t entity_id);

    struct MemberAttendanceSummary {
        int64_t     member_id = 0;
        std::string display_name;
        std::string discord_username;
        int         meeting_count = 0;
        int         meeting_virtual_count = 0;
        int         event_count = 0;
        std::string last_attendance;  // ISO datetime of most recent check-in
        bool        is_paid = false;
        std::string fol_status;
    };
    std::vector<MemberAttendanceSummary> get_all_member_summaries();
    std::vector<MemberAttendanceSummary> get_all_member_summaries_by_year(int year);

    // Paginated, searchable, sortable year-filtered summaries
    struct OverviewParams {
        int         year = 0;
        std::string search;
        std::string sort_col  = "display_name"; // display_name|meeting_count|event_count|total|last_attendance
        std::string sort_dir  = "asc";
        int         limit     = 25;
        int         offset    = 0;
        bool        hide_inactive = false;       // hide members with no attendance and no dues
    };
    std::vector<MemberAttendanceSummary> get_overview_paginated(const OverviewParams& p);
    int count_overview(const OverviewParams& p);

    // Returns the distinct years that have attendance records
    std::vector<int> get_attendance_years();

    // Count attendance for a member in a calendar year, by entity type
    int count_member_by_year(int64_t member_id, int year, const std::string& entity_type);

private:
    SqliteDatabase& db_;
};
