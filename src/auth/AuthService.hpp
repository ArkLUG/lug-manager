#pragma once
#include "auth/SessionStore.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "integrations/DiscordOAuth.hpp"
#include "integrations/DiscordClient.hpp"
#include "models/Session.hpp"
#include "models/Member.hpp"
#include <optional>
#include <string>

class AuthService {
public:
    // discord and role_mappings are optional (nullptr disables role-based provisioning)
    AuthService(SessionStore& sessions, MemberRepository& members, DiscordOAuth& oauth,
                const std::string& bootstrap_admin_discord_id = "",
                DiscordClient* discord = nullptr,
                RoleMappingRepository* role_mappings = nullptr);

    // Complete Discord OAuth2 login flow.
    // Returns session token on success.
    // Throws std::runtime_error("not_authorized") if the user has no access.
    std::string login_with_discord(const std::string& code, const std::string& redirect_uri = "");

    // Validate session token. Returns Session if valid, nullopt if expired/missing.
    std::optional<Session> validate_session(const std::string& token);

    // Destroy session
    void logout(const std::string& token);

private:
    SessionStore&          sessions_;
    MemberRepository&      members_;
    DiscordOAuth&          oauth_;
    std::string            bootstrap_admin_discord_id_;
    DiscordClient*         discord_;        // may be nullptr
    RoleMappingRepository* role_mappings_;  // may be nullptr

    // Resolve LUG role from Discord guild roles. Returns "" if no mapping found.
    std::string resolve_role_from_discord(const std::string& discord_user_id) const;
};
