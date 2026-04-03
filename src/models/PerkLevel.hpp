#pragma once
#include <string>
#include <cstdint>

struct PerkLevel {
    int64_t     id                          = 0;
    std::string name;
    std::string discord_role_id;
    int         meeting_attendance_required = 0;
    int         event_attendance_required   = 0;
    bool        requires_paid_dues         = false;
    std::string min_fol_status             = "afol"; // "kfol"|"tfol"|"afol"
    int         sort_order                  = 0;
    int         year                        = 0; // calendar year this tier applies to
    std::string created_at;
    std::string updated_at;
};
