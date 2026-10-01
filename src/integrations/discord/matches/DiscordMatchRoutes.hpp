#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "integrations/discord/matches/PendingDiscordMatchRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_discord_match_routes(LugApp& app,
                                    PendingDiscordMatchRepository& pending_matches,
                                    MemberRepository& member_repo,
                                    AuditService& audit,
                                    SettingsRepository& settings,
                                    DiscordClient& discord);
