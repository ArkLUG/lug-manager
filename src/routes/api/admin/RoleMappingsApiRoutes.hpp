#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "integrations/discord/sync/RoleMappingRepository.hpp"
#include "services/AuditService.hpp"

void register_role_mappings_api_routes(LugApp& app, RoleMappingRepository& role_mappings,
                                        AuditService& audit);
