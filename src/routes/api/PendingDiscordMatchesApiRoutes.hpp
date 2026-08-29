#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "services/AuditService.hpp"

void register_pending_discord_matches_api_routes(LugApp& app,
                                                   PendingDiscordMatchRepository& pending_matches,
                                                   MemberRepository& member_repo,
                                                   AuditService& audit);
