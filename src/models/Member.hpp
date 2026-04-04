#pragma once
#include <string>
#include <cstdint>

struct Member {
    int64_t     id               = 0;
    std::string discord_user_id;
    std::string discord_username;
    std::string first_name;
    std::string last_name;
    std::string display_name;       // auto-generated: "Aaron K." style nickname
    std::string email;
    bool        is_paid          = false;
    std::string paid_until;       // ISO 8601 date "2026-12-31", may be empty
    std::string role              = "member"; // "admin"|"chapter_lead"|"member"
    std::string phone;
    std::string address_line1;
    std::string address_line2;
    std::string city;
    std::string state;
    std::string zip;
    // Per-field PII sharing: "none"|"verified"|"all" (empty = use DB default "none")
    std::string sharing_email;
    std::string sharing_phone;
    std::string sharing_address;
    std::string sharing_birthday;
    std::string sharing_discord;
    std::string birthday;                   // "YYYY-MM-DD", empty if unknown
    std::string fol_status       = "afol";  // "kfol"|"tfol"|"afol"
    int64_t     chapter_id        = 0;       // FK to chapters, 0 if not in a chapter
    std::string chapter_name;               // denormalized for reads (may be empty)
    std::string created_at;
    std::string updated_at;
};
