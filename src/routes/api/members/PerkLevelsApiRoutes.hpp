#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_perk_levels_api_routes(LugApp& app, PerkLevelRepository& perks,
                                      MemberRepository& members, AttendanceRepository& attendance,
                                      DiscordClient& discord, AuditService& audit);
