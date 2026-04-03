#pragma once
#include <string>
#include <cstdint>

struct LugEvent {
    int64_t     id               = 0;
    std::string title;
    std::string description;
    std::string location;
    std::string start_time;
    std::string end_time;
    std::string status           = "confirmed"; // "tentative"|"confirmed"|"open"|"closed"|"cancelled"
    std::string discord_thread_id;
    std::string discord_event_id;
    std::string google_calendar_event_id;    // Google Calendar event ID
    std::string discord_chapter_message_id; // message ID in chapter announcement channel
    std::string discord_lug_message_id;     // message ID in lug_channel announcement
    std::string discord_ping_role_ids = "\x01"; // comma-separated extra role IDs to ping; "\x01" = not-set sentinel
    std::string ical_uid;         // UUID v4, never changes
    std::string signup_deadline;  // May be empty
    int         max_attendees    = 0; // 0 = unlimited
    std::string scope             = "chapter"; // "lug_wide" | "chapter" | "non_lug"
    int64_t     chapter_id        = 0; // FK to chapters
    int64_t     event_lead_id          = 0; // FK to members (nullable)
    std::string event_lead_name;            // display name of the lead (denormalized for reads)
    std::string event_lead_discord_id;      // discord_user_id of the lead (denormalized for reads)
    bool        suppress_discord  = false; // skip all Discord operations
    bool        suppress_calendar = false; // skip Google Calendar operations
    std::string notes;                     // markdown notes/report
    std::string notes_discord_post_id;     // Discord forum thread ID for published report
    std::string entrance_fee;              // e.g. "$5", "Free"
    int         public_kids     = 0;       // public attendee counts for report
    int         public_teens    = 0;
    int         public_adults   = 0;
    std::string social_media_links;        // links to social media posts about the event
    std::string event_feedback;            // "what you liked best"
    std::string created_at;
    std::string updated_at;
};
