#pragma once
#include <string>

struct RoleMapping {
    std::string discord_role_id;
    std::string discord_role_name;
    std::string lug_role;  // "admin" | "chapter_lead" | "member"
};
