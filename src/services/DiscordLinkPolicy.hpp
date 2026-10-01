#pragma once
#include "models/Member.hpp"
#include <string>

// Linking a Discord account to a member record decides who can log in AS that
// member - so a non-admin resolver (chapter lead, an allowlisted Discord user
// via the interaction buttons, a write-scope API key) may only link to a plain
// member record that has no Discord account yet. Otherwise e.g. a chapter lead
// could link their own alt account to an admin's record and log in as admin.
// Returns "" when allowed, else a human-readable reason.
inline std::string discord_link_block_reason(const Member& target, bool caller_is_admin) {
    if (caller_is_admin) return "";
    if (!target.discord_user_id.empty())
        return "That member already has a Discord account linked - only an admin can change it.";
    if (!target.role.empty() && target.role != "member")
        return "Only an admin can link a Discord account to a " + target.role + ".";
    return "";
}
