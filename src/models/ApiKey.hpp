#pragma once
#include <string>
#include <cstdint>

struct ApiKey {
    int64_t     id           = 0;
    std::string key_hash;                 // SHA-256 hex of the raw key; unique, indexed
    std::string label;                    // admin-supplied name, e.g. "Zapier automation"
    std::string scope         = "read";   // "read" | "write" | "admin"
    int64_t     created_by    = 0;        // FK to members.id (the admin who issued it)
    std::string created_by_name;          // denormalized: issuing admin's display_name
    std::string created_at;
    std::string last_used_at;             // empty if never used
    std::string revoked_at;               // empty if active; set = revoked (soft delete)
};
