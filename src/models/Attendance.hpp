#pragma once
#include <string>
#include <cstdint>

struct Attendance {
    int64_t     id            = 0;
    int64_t     member_id     = 0;
    std::string entity_type;  // "meeting" or "event"
    int64_t     entity_id     = 0;
    std::string checked_in_at;
    std::string notes;
    bool        is_virtual    = false;
    // Denormalized fields (joined from members table)
    std::string member_display_name;
    std::string member_discord_username;
    // Only meaningful for entity_type=="event" rows produced by
    // AttendanceService::get_member_history(), which collapses a multi-day
    // event's per-day event_day_attendance rows into one history row. 1 for
    // every other row (meetings, or event rows from any other source).
    int         days_attended = 1;
};
