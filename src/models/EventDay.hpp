#pragma once
#include <string>
#include <cstdint>

struct EventDay {
    int64_t     id         = 0;
    int64_t     event_id   = 0;
    std::string day_date;   // YYYY-MM-DD
    int         day_number = 0;
};

struct EventDayAttendance {
    int64_t     id             = 0;
    int64_t     event_day_id   = 0;
    int64_t     member_id      = 0;
    std::string checked_in_at;
    std::string notes;
    bool        qualifies      = true;
    std::string member_display_name;
    std::string member_discord_username;
    std::string day_date;
    int         day_number     = 0;
};
