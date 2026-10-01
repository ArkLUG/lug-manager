#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Admin: merge a duplicate member record into another (preview, then confirm).
void register_member_merge_routes(LugApp& app, SqliteDatabase& db, AuditService& audit);
