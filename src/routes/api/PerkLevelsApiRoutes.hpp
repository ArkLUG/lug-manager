#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "services/AuditService.hpp"

void register_perk_levels_api_routes(LugApp& app, PerkLevelRepository& perks, AuditService& audit);
