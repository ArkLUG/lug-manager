#pragma once
#include <string>
#include <cstdint>

struct ChapterMember {
    int64_t     member_id    = 0;
    int64_t     chapter_id   = 0;
    std::string chapter_role;       // "lead" | "event_manager" | "member"
    int64_t     granted_by   = 0;
    std::string granted_at;

    // Populated by joined queries
    std::string display_name;
    std::string discord_username;
    std::string chapter_name;       // populated when querying by member
};
