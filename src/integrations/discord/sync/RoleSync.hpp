#pragma once
#include <optional>
#include <string>

// Decides a member's LUG role after a Discord role sync (login or guild sync).
//   current     - the member's role now
//   source      - "manual" | "discord" (members.role_source)
//   mapped      - role resolved from their Discord roles via the admin's role
//                 mappings; nullopt when none of their Discord roles is mapped
// Discord-sourced roles follow the mapping exactly (no mapped role -> member).
// Manual roles are never lowered by sync, only raised by a higher mapped role
// (which then becomes Discord-sourced - see sync_role_source()).
namespace role_sync {

inline int rank(const std::string& role) {
    if (role == "admin")                                  return 3;
    if (role == "chapter_lead" || role == "moderator")    return 2; // same tier
    if (role == "member")                                 return 1;
    return 0;
}

inline std::string next_role(const std::string& current, const std::string& source,
                             const std::optional<std::string>& mapped) {
    if (source == "manual") {
        return (mapped && rank(*mapped) > rank(current)) ? *mapped : current;
    }
    return mapped ? *mapped : std::string("member");
}

// role_source to store alongside next_role()'s result.
inline std::string next_source(const std::string& current, const std::string& source,
                               const std::optional<std::string>& mapped) {
    if (source == "manual" && !(mapped && rank(*mapped) > rank(current))) return "manual";
    return "discord";
}

} // namespace role_sync
