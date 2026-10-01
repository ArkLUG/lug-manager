#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "integrations/discord/matches/PendingDiscordMatchRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "services/AuditService.hpp"

void register_pending_discord_matches_api_routes(LugApp& app,
                                                   PendingDiscordMatchRepository& pending_matches,
                                                   MemberRepository& member_repo,
                                                   AuditService& audit);
