#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Signed-in devices: list your own and sign out the others; admins can sign
// a member out everywhere.
void register_session_routes(LugApp& app, AuthService& auth, AuditService& audit);
