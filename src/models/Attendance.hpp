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
};
