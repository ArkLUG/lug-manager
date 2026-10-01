#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Settings > Features (admin): switch optional parts of the app on/off.
void register_feature_routes(LugApp& app, SettingsRepository& settings, AuditService& audit);
