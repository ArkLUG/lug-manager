#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/SettingsRepository.hpp"
#include "services/AuditService.hpp"

void register_settings_api_routes(LugApp& app, SettingsRepository& settings, AuditService& audit);
