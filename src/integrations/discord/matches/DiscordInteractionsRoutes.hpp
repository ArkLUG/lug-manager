#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "integrations/discord/matches/PendingDiscordMatchRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include "services/notifications/ReminderActions.hpp"
#include <crow.h>
#include <string>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

// Registers POST /discord/interactions — the inbound Discord Interactions webhook
// used to resolve pending Discord member matches via an in-Discord button + modal,
// and for the buttons on reminder DMs ("lm:..." custom ids, ReminderActions).
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
                                           AuditService& audit,
                                           std::shared_ptr<ReminderActions> reminder_actions = nullptr);

// The application's public key that Discord signs interactions with: the
// DISCORD_PUBLIC_KEY environment variable if set (read once at start-up),
// else Settings > Discord matches. Not a secret - Discord shows it openly.
std::string discord_interactions_key(SettingsRepository& settings);
bool discord_interactions_key_locked();


// Tests: forget the failed-signature counts (per client address, per minute).
void reset_discord_interaction_limits();
