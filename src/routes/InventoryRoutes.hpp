#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// LUG inventory: items with quantities, checked out to members and returned.
// Everyone can see it; chapter leads and admins manage it.
void register_inventory_routes(LugApp& app, SqliteDatabase& db, AuditService& audit);
