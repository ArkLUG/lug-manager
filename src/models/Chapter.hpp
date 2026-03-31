#pragma once

#include <cstdint>
#include <string>

struct Chapter {
    int64_t id = 0;
    std::string name;
    std::string description;
    std::string discord_announcement_channel_id;
    std::string discord_lead_role_id;
    std::string discord_member_role_id;
    int64_t created_by = 0;
    std::string created_at;
};
