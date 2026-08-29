#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_perk_levels_api_routes(LugApp& app, PerkLevelRepository& perks,
                                      MemberRepository& members, AttendanceRepository& attendance,
                                      DiscordClient& discord, AuditService& audit);
