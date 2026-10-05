#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Settings > Roles and permissions (admin): what moderators, chapter leads
// and members may do. See auth/Permissions.hpp.
void register_permission_routes(LugApp& app, SettingsRepository& settings, AuditService& audit);
