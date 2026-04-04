#pragma once
#include <string>
#include <cstdint>

struct AuditLog {
    int64_t     id         = 0;
    int64_t     actor_id   = 0;     // member who performed the action (0 = system/public)
    std::string actor_name;          // display name at time of action
    std::string action;              // e.g. "member.create", "meeting.delete", "settings.update"
    std::string entity_type;         // "member"|"meeting"|"event"|"chapter"|"perk"|"settings"|"attendance"
    int64_t     entity_id  = 0;      // ID of the affected entity (0 if N/A)
    std::string entity_name;         // display label at time of action
    std::string details;             // freeform description of what changed
    std::string ip_address;
    std::string created_at;
};
