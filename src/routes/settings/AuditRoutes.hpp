#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"

void register_audit_routes(LugApp& app, AuditService& audit);
