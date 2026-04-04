#include "services/MemberSyncService.hpp"
#include "models/Member.hpp"
#include <iostream>
#include <unordered_map>
#include <unordered_set>

MemberSyncService::MemberSyncService(DiscordClient& discord,
                                     MemberRepository& member_repo,
                                     RoleMappingRepository& role_mappings,
                                     ChapterRepository& chapter_repo,
                                     ChapterMemberRepository& chapter_member_repo)
    : discord_(discord), member_repo_(member_repo), role_mappings_(role_mappings),
      chapter_repo_(chapter_repo), chapter_member_repo_(chapter_member_repo) {}

SyncResult MemberSyncService::sync_from_guild() {
    SyncResult result;

    std::vector<DiscordGuildMember> guild_members;
    try {
        guild_members = discord_.fetch_guild_members();
    } catch (const std::exception& e) {
        result.errors = 1;
        result.error_message = std::string("Failed to fetch guild members: ") + e.what();
        return result;
    }

    // Track discord_user_id → member_id for chapter lead sync later
    std::unordered_map<std::string, int64_t> discord_to_member_id;
    // Track discord_user_id → set of Discord role IDs (for chapter lead checks)
    std::unordered_map<std::string, std::unordered_set<std::string>> member_discord_roles;

    // --- Phase 1: sync LUG member records ---
    for (const auto& gm : guild_members) {
        try {
            auto lug_role_opt = role_mappings_.resolve_lug_role(gm.role_ids);

            std::string display = !gm.nick.empty()        ? gm.nick
                                : !gm.global_name.empty() ? gm.global_name
                                :                           gm.username;

            // Role mappings elevate to admin; everyone else defaults to "member"
            std::string new_role = lug_role_opt ? *lug_role_opt : "member";

            auto existing = member_repo_.find_by_discord_id(gm.discord_user_id);
            if (existing) {
                bool changed = (existing->discord_username != gm.username)
                            || (existing->display_name != display)
                            || (existing->role != new_role);
                if (changed) {
                    // Track each field change
                    if (existing->discord_username != gm.username) {
                        result.changes.push_back({existing->id, display, "updated",
                            "discord_username", existing->discord_username, gm.username});
                    }
                    if (existing->display_name != display) {
                        result.changes.push_back({existing->id, display, "updated",
                            "display_name", existing->display_name, display});
                    }
                    if (existing->role != new_role) {
                        result.changes.push_back({existing->id, display, "updated",
                            "role", existing->role, new_role});
                    }

                    Member updated = *existing;
                    updated.discord_username = gm.username;
                    updated.display_name     = display;
                    updated.role             = new_role;
                    member_repo_.update(updated);
                    ++result.updated;
                } else {
                    ++result.skipped;
                }
                discord_to_member_id[gm.discord_user_id] = existing->id;
            } else {
                // Import all guild members; role mappings only affect role level
                Member m;
                m.discord_user_id  = gm.discord_user_id;
                m.discord_username = gm.username;
                m.display_name     = display;
                m.role             = new_role;
                Member created = member_repo_.create(m);
                discord_to_member_id[gm.discord_user_id] = created.id;
                result.changes.push_back({created.id, display, "created", "", "", ""});
                ++result.imported;
            }

            // Store role IDs for chapter lead sync
            member_discord_roles[gm.discord_user_id] =
                std::unordered_set<std::string>(gm.role_ids.begin(), gm.role_ids.end());

        } catch (const std::exception& e) {
            std::cerr << "[MemberSyncService] Error processing member "
                      << gm.discord_user_id << ": " << e.what() << "\n";
            ++result.errors;
        }
    }

    // --- Phase 2: sync chapter lead roles (bidirectional) ---
    // Web leads without Discord role → assign Discord role
    // Discord role holders not yet web leads → promote in web
    // No demotions: removing a lead must be done explicitly in the web UI
    auto chapters = chapter_repo_.find_all();
    for (const auto& ch : chapters) {
        if (ch.discord_lead_role_id.empty()) continue;

        try {
            // Members who currently have the lead Discord role
            std::unordered_set<int64_t> has_discord_role;
            for (auto& [discord_id, role_set] : member_discord_roles) {
                if (role_set.count(ch.discord_lead_role_id)) {
                    auto it = discord_to_member_id.find(discord_id);
                    if (it != discord_to_member_id.end())
                        has_discord_role.insert(it->second);
                }
            }

            // Current web leads for this chapter
            auto ch_members = chapter_member_repo_.find_by_chapter(ch.id);
            std::unordered_set<int64_t> current_lead_ids;
            for (auto& cm : ch_members) {
                if (cm.chapter_role == "lead")
                    current_lead_ids.insert(cm.member_id);
            }

            // Discord role holder but not a web lead → promote in web
            for (int64_t mid : has_discord_role) {
                if (!current_lead_ids.count(mid)) {
                    auto member = member_repo_.find_by_id(mid);
                    std::string name = member ? member->display_name : "ID:" + std::to_string(mid);
                    chapter_member_repo_.upsert(mid, ch.id, "lead", 0);
                    result.changes.push_back({mid, name, "chapter_lead_added",
                        "chapter_role", "member", "lead (ch: " + ch.name + ")"});
                    ++result.updated;
                }
            }

            // Web lead but missing Discord role → assign Discord role
            for (int64_t mid : current_lead_ids) {
                if (!has_discord_role.count(mid)) {
                    auto member = member_repo_.find_by_id(mid);
                    if (member && !member->discord_user_id.empty()) {
                        try {
                            discord_.add_member_role(member->discord_user_id, ch.discord_lead_role_id);
                            ++result.updated;
                        } catch (const std::exception& e) {
                            std::cerr << "[MemberSyncService] Failed to assign lead Discord role"
                                      << " (member=" << mid << " chapter=" << ch.id
                                      << "): " << e.what() << "\n";
                            ++result.errors;
                        }
                    }
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "[MemberSyncService] Chapter lead sync error (chapter "
                      << ch.id << "): " << e.what() << "\n";
            ++result.errors;
        }
    }

    return result;
}
