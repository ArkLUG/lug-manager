#include "auth/AuthService.hpp"
#include <stdexcept>
#include <iostream>

AuthService::AuthService(SessionStore& sessions, MemberRepository& members, DiscordOAuth& oauth,
                         const std::string& bootstrap_admin_discord_id,
                         DiscordClient* discord, RoleMappingRepository* role_mappings)
    : sessions_(sessions), members_(members), oauth_(oauth),
      bootstrap_admin_discord_id_(bootstrap_admin_discord_id),
      discord_(discord), role_mappings_(role_mappings) {}

std::string AuthService::resolve_role_from_discord(const std::string& discord_user_id) const {
    if (!discord_ || !role_mappings_) return "";
    try {
        auto role_ids = discord_->fetch_member_role_ids(discord_user_id);
        if (role_ids.empty()) return "";
        auto lug_role = role_mappings_->resolve_lug_role(role_ids);
        return lug_role.value_or("");
    } catch (const std::exception& e) {
        std::cerr << "[AuthService] Could not fetch Discord roles for " << discord_user_id
                  << ": " << e.what() << "\n";
        return "";
    }
}

std::string AuthService::login_with_discord(const std::string& code) {
    // 1. Exchange code for access token
    std::string access_token = oauth_.exchange_code(code);

    // 2. Get Discord user info
    auto user_info = oauth_.get_user_info(access_token);
    if (user_info.id.empty()) {
        throw std::runtime_error("Failed to get Discord user info");
    }

    // 3. Look up member by Discord user ID
    auto member_opt = members_.find_by_discord_id(user_info.id);

    // 4a. Bootstrap: if not found but matches BOOTSTRAP_ADMIN_DISCORD_ID, auto-create as admin
    std::cerr << "[AuthService] Login attempt: discord_id='" << user_info.id
              << "' bootstrap_id='" << bootstrap_admin_discord_id_ << "'\n";
    if (!member_opt && !bootstrap_admin_discord_id_.empty() &&
        user_info.id == bootstrap_admin_discord_id_) {
        Member bootstrap;
        bootstrap.discord_user_id  = user_info.id;
        bootstrap.discord_username = user_info.username;
        bootstrap.display_name     = user_info.global_name.empty() ? user_info.username : user_info.global_name;
        bootstrap.role             = "admin";
        std::cerr << "[AuthService] Bootstrap: creating admin member for Discord ID "
                  << user_info.id << " (" << bootstrap.discord_username << ")\n";
        member_opt = members_.create(bootstrap);
    }

    // 4b. Role-mapping auto-provision: if still not found, check Discord guild roles
    if (!member_opt) {
        std::string lug_role = resolve_role_from_discord(user_info.id);
        if (!lug_role.empty()) {
            Member provisioned;
            provisioned.discord_user_id  = user_info.id;
            provisioned.discord_username = user_info.username;
            provisioned.display_name     = user_info.global_name.empty() ? user_info.username : user_info.global_name;
            provisioned.role             = lug_role;
            std::cerr << "[AuthService] Auto-provisioning member " << user_info.username
                      << " with role=" << lug_role << "\n";
            member_opt = members_.create(provisioned);
        }
    }

    if (!member_opt) {
        throw std::runtime_error("not_authorized");
    }
    Member member = *member_opt;

    // 5. Sync name changes
    bool needs_update = false;
    if (member.discord_username != user_info.username) {
        member.discord_username = user_info.username;
        needs_update = true;
    }
    if (!user_info.global_name.empty() && member.display_name != user_info.global_name) {
        member.display_name = user_info.global_name;
        needs_update = true;
    }

    // 6. Sync LUG role from Discord on every login (if role mappings are configured)
    //    Bootstrap admin always stays admin regardless of Discord roles.
    if (member.discord_user_id != bootstrap_admin_discord_id_) {
        std::string synced_role = resolve_role_from_discord(user_info.id);
        if (!synced_role.empty() && synced_role != member.role) {
            std::cerr << "[AuthService] Role sync: " << user_info.username
                      << " " << member.role << " -> " << synced_role << "\n";
            member.role = synced_role;
            needs_update = true;
        }
    }

    if (needs_update) {
        members_.update(member);
    }

    // 7. Create and return session token (24 hour lifetime)
    return sessions_.create(member.id, member.role, 24);
}

std::optional<Session> AuthService::validate_session(const std::string& token) {
    return sessions_.find(token);
}

void AuthService::logout(const std::string& token) {
    sessions_.remove(token);
}
