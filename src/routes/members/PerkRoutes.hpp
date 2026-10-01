#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_perk_routes(LugApp& app, PerkLevelRepository& perks,
                           AttendanceRepository& attendance,
                           MemberRepository& members,
                           DiscordClient& discord,
                           AuditService& audit);
