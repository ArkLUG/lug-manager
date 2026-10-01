#pragma once
#include "integrations/DiscordClient.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "services/PerkProgress.hpp"

// Gives each linked member the Discord role of every perk tier they meet for
// `year` and removes the roles of tiers they don't. Returns members synced.
// Shared by the Settings button and POST /api/v1/perk-levels/sync-roles.
inline int sync_perk_roles(PerkLevelRepository& perks, MemberRepository& members,
                           AttendanceRepository& attendance, DiscordClient& discord, int year) {
    auto levels = perks.find_by_year(year);
    bool any_role = false;
    for (const auto& l : levels) any_role = any_role || !l.discord_role_id.empty();
    if (!any_role) return 0;

    int synced = 0;
    for (const auto& m : members.find_all()) {
        if (m.discord_user_id.empty()) continue; // nothing to sync on Discord
        int meetings = attendance.count_member_by_year(m.id, year, "meeting");
        int events   = attendance.count_member_by_year(m.id, year, "event");
        for (const auto& lvl : levels) {
            if (lvl.discord_role_id.empty()) continue;
            try {
                if (meets_perk_level(lvl, meetings, events, m.is_paid, m.fol_status))
                    discord.add_member_role(m.discord_user_id, lvl.discord_role_id);
                else
                    discord.remove_member_role(m.discord_user_id, lvl.discord_role_id);
            } catch (...) {}
        }
        ++synced;
    }
    return synced;
}
