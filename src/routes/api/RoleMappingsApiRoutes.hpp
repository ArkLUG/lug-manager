#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "services/AuditService.hpp"

void register_role_mappings_api_routes(LugApp& app, RoleMappingRepository& role_mappings,
                                        AuditService& audit);
