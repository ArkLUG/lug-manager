#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Treasury (admin): other income and expenses alongside recorded dues, with
// yearly totals, categories, per-event totals, monthly breakdown and CSV.
void register_treasury_routes(LugApp& app, SqliteDatabase& db, AuditService& audit);
