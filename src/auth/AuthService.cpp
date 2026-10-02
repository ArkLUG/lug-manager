#include "auth/AccountSecurity.hpp"
#include "auth/AuthService.hpp"
#include "integrations/discord/sync/RoleSync.hpp"
#include <stdexcept>
#include <iostream>

AuthService::AuthService(SessionStore& sessions, MemberRepository& members, DiscordOAuth& oauth,
                         const std::string& bootstrap_admin_discord_id,
                         DiscordClient* discord, RoleMappingRepository* role_mappings)
    : sessions_(sessions), members_(members), oauth_(oauth),
      bootstrap_admin_discord_id_(bootstrap_admin_discord_id),
      discord_(discord), role_mappings_(role_mappings) {}

std::string AuthService::login_with_discord(const std::string& code, const std::string& redirect_uri,
                                            const std::string& user_agent) {
    Member member = discord_member(code, redirect_uri);
    return sessions_.create(member.id, member.role, member.display_name, SessionStore::kSessionHours, user_agent);
}

static std::string auth_setting(SqliteDatabase& db, const char* key, const std::string& def) {
    auto st = db.prepare("SELECT value FROM lug_settings WHERE key=?");
    st.bind(1, std::string(key));
    return st.step() ? st.col_text(0) : def;
}

Member AuthService::discord_member(const std::string& code, const std::string& redirect_uri) {
    // 1. Exchange code for access token
    std::string access_token = oauth_.exchange_code(code, redirect_uri);

    // 2. Get Discord user info
    auto user_info = oauth_.get_user_info(access_token);
    if (user_info.id.empty()) {
        throw std::runtime_error("Failed to get Discord user info");
    }

    // 3. Look up member by Discord user ID
    auto member_opt = members_.find_by_discord_id(user_info.id);

    // 4a. Bootstrap: if not found but matches BOOTSTRAP_ADMIN_DISCORD_ID, auto-create as admin
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
        if (member_opt) members_.set_role_source(member_opt->id, "manual");
    }

    // 4b. Auto-provision: if still not found, create with mapped role or default "member" -
    //     but only for a confirmed member of the configured Discord guild. The OAuth
    //     "identify" scope proves who the user is, not that they belong to this LUG,
    //     so without this check any Discord account could self-register.
    if (!member_opt) {
        std::optional<std::vector<std::string>> guild_roles;
        if (discord_) {
            try {
                guild_roles = discord_->fetch_guild_member_role_ids(user_info.id);
            } catch (const std::exception& e) {
                std::cerr << "[AuthService] Could not verify guild membership: " << e.what() << "\n";
            }
        }
        if (!guild_roles) {
            std::cerr << "[AuthService] Refusing login for non-guild Discord user "
                      << user_info.username << "\n";
            throw std::runtime_error("not_authorized");
        }
        std::string lug_role;
        if (role_mappings_ && !guild_roles->empty())
            lug_role = role_mappings_->resolve_lug_role(*guild_roles).value_or("");
        if (lug_role.empty()) lug_role = "member";
        Member provisioned;
        provisioned.discord_user_id  = user_info.id;
        provisioned.discord_username = user_info.username;
        provisioned.display_name     = user_info.global_name.empty() ? user_info.username : user_info.global_name;
        provisioned.role             = lug_role;
        std::cerr << "[AuthService] Auto-provisioning member " << user_info.username
                  << " with role=" << lug_role << "\n";
        member_opt = members_.create(provisioned);
    }

    if (!member_opt) {
        throw std::runtime_error("not_authorized");
    }
    Member member = *member_opt;

    // 5. Sync name changes
    bool needs_update = false;
    // A member with no email gets their Discord account's verified one, so they
    // can also sign in with a password or email link and get emails. Never
    // overwrites an email they have, and never takes one another member uses.
    if (member.email.empty() && user_info.verified && user_info.email.find('@') != std::string::npos &&
        auth_setting(members_.db(), "auth_discord_email", "1") == "1" &&
        !AccountSecurity(members_.db()).email_taken(user_info.email, member.id)) {
        member.email = AccountSecurity::lower(user_info.email);
        needs_update = true;
    }
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
    //    Same rules as the periodic guild sync (services/RoleSync.hpp): roles
    //    that came from Discord follow the mapping, including demotion when the
    //    mapped Discord role is gone; manually granted roles are never lowered.
    //    Skipped when membership can't be confirmed, so a Discord outage never
    //    demotes anyone.
    std::string new_source;
    if (member.discord_user_id != bootstrap_admin_discord_id_ && discord_ && role_mappings_) {
        std::optional<std::vector<std::string>> guild_roles;
        try {
            guild_roles = discord_->fetch_guild_member_role_ids(user_info.id);
        } catch (const std::exception& e) {
            std::cerr << "[AuthService] Could not fetch Discord roles: " << e.what() << "\n";
        }
        if (guild_roles) {
            auto mapped = role_mappings_->resolve_lug_role(*guild_roles);
            std::string source = members_.get_role_source(member.id);
            std::string next   = role_sync::next_role(member.role, source, mapped);
            new_source         = role_sync::next_source(member.role, source, mapped);
            if (next != member.role) {
                std::cerr << "[AuthService] Role sync: " << user_info.username
                          << " " << member.role << " -> " << next << "\n";
                member.role = next;
                needs_update = true;
            }
        }
    }

    if (needs_update) {
        members_.update(member);
    }
    if (!new_source.empty()) members_.set_role_source(member.id, new_source);

    return member;
}

std::optional<Session> AuthService::validate_session(const std::string& token) {
    return sessions_.find(token);
}

void AuthService::logout(const std::string& token) {
    sessions_.remove(token);
}
