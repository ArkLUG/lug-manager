#pragma once
#include <string>
#include <cstdint>

struct Meeting {
    int64_t     id               = 0;
    std::string title;
    std::string description;
    std::string location;
    std::string start_time;       // ISO 8601 "2026-04-15T19:00:00"
    std::string end_time;
    std::string status           = "scheduled"; // "scheduled"|"cancelled"|"completed"
    std::string discord_event_id;            // Discord scheduled event snowflake
    std::string discord_lug_message_id;      // message ID in lug announcement channel
    std::string discord_chapter_message_id;  // message ID in chapter announcement channel
    std::string google_calendar_event_id;    // Google Calendar event ID
    std::string ical_uid;         // UUID v4, generated on creation, NEVER changes
    std::string scope             = "chapter"; // "lug_wide" | "chapter" | "non_lug"
    int64_t     chapter_id        = 0;         // FK to chapters
    std::string created_at;
    std::string updated_at;
};
