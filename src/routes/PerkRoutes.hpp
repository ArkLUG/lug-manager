#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_perk_routes(LugApp& app, PerkLevelRepository& perks,
                           AttendanceRepository& attendance,
                           MemberRepository& members,
                           DiscordClient& discord,
                           AuditService& audit);
