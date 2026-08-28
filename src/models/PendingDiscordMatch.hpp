#pragma once
#include <string>
#include <cstdint>

struct PendingDiscordMatch {
    int64_t     id                   = 0;
    std::string discord_user_id;
    std::string discord_username;
    std::string discord_display_name;
    std::string discord_role_ids;         // comma-separated Discord role IDs
    int64_t     suggested_member_id  = 0; // FK members.id, 0 = no suggestion
    std::string created_at;
    std::string resolved_at;              // empty if unresolved
    std::string resolved_action;          // "linked"|"created_new"|"" if unresolved
    int64_t     resolved_member_id   = 0; // 0 if unresolved
};
