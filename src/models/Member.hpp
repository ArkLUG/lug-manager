#pragma once
#include <string>
#include <cstdint>

struct Member {
    int64_t     id               = 0;
    std::string discord_user_id;
    std::string discord_username;
    std::string display_name;
    std::string email;
    bool        is_paid          = false;
    std::string paid_until;       // ISO 8601 date "2026-12-31", may be empty
    std::string role              = "member"; // "admin"|"chapter_admin"|"member"|"readonly"
    int64_t     chapter_id        = 0;       // FK to chapters, 0 if not in a chapter
    std::string chapter_name;               // denormalized for reads (may be empty)
    std::string created_at;
    std::string updated_at;
};
