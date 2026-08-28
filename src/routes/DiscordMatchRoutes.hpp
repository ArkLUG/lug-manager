#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "services/AuditService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_discord_match_routes(LugApp& app,
                                    PendingDiscordMatchRepository& pending_matches,
                                    MemberRepository& member_repo,
                                    AuditService& audit);
