#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include <crow.h>
#include <string>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

// Registers POST /discord/interactions — the inbound Discord Interactions webhook
// used to resolve pending Discord member matches via an in-Discord button + modal.
//
// This is the only route in the app deliberately NOT gated by AuthMiddleware /
// ApiKeyMiddleware: Discord calls it directly, with no session or API key. Its sole
// trust boundary is Ed25519 signature verification (every request, including PING)
// followed by an explicit Discord role-ID allowlist check pulled from `settings`.
// Never mistake the missing middleware here for an oversight — see the .cpp for
// the full security rationale.
void register_discord_interactions_routes(LugApp& app,
                                           const std::string& discord_public_key,
                                           PendingDiscordMatchRepository& pending_matches,
                                           MemberRepository& member_repo,
                                           SettingsRepository& settings,
                                           AuditService& audit);
