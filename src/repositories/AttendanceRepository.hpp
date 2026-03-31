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

private:
    SqliteDatabase& db_;
};
