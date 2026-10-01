#pragma once
#include "integrations/discord/DiscordClient.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "services/members/PerkProgress.hpp"
#include <unordered_map>
#include <unordered_set>

// Gives each linked member the Discord role of every perk tier they meet for
// `year` and removes the roles of tiers they don't. Returns members synced.
// Shared by the Settings button and POST /api/v1/perk-levels/sync-roles.
//
// Fetches the server's member list once and only calls Discord where a role
// actually has to change (previously: an add/remove call per member per tier
// on every sync, i.e. hundreds of rate-limited requests inside one HTTP
// request). Members not in the server are skipped.
inline int sync_perk_roles(PerkLevelRepository& perks, MemberRepository& members,
                           AttendanceRepository& attendance, DiscordClient& discord, int year) {
    auto levels = perks.find_by_year(year);
    bool any_role = false;
    for (const auto& l : levels) any_role = any_role || !l.discord_role_id.empty();
    if (!any_role) return 0;

    std::unordered_map<std::string, std::unordered_set<std::string>> current; // discord id -> role ids
    for (const auto& gm : discord.fetch_guild_members()) // throws on failure: no partial syncs
        current[gm.discord_user_id] = {gm.role_ids.begin(), gm.role_ids.end()};

    int synced = 0;
    for (const auto& m : members.find_all()) {
        if (m.discord_user_id.empty()) continue;
        auto it = current.find(m.discord_user_id);
        if (it == current.end()) continue; // not in the server
        int meetings = attendance.count_member_by_year(m.id, year, "meeting");
        int events   = attendance.count_member_by_year(m.id, year, "event");
        for (const auto& lvl : levels) {
            if (lvl.discord_role_id.empty()) continue;
            bool has  = it->second.count(lvl.discord_role_id) > 0;
            bool want = meets_perk_level(lvl, meetings, events, m.is_paid, m.fol_status);
            try {
                if (want && !has)      discord.add_member_role(m.discord_user_id, lvl.discord_role_id);
                else if (!want && has) discord.remove_member_role(m.discord_user_id, lvl.discord_role_id);
            } catch (...) {}
        }
        ++synced;
    }
    return synced;
}
