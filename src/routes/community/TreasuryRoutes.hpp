#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"
#include "services/PhotoStore.hpp"
#include <memory>

// Treasury (admins and members marked treasurer): other income and expenses
// alongside recorded dues, with yearly totals, categories, per-event totals,
// monthly breakdown, CSV, receipts (photo/PDF) and dues recording.
void register_treasury_routes(LugApp& app, SqliteDatabase& db, AuditService& audit,
                              std::shared_ptr<PhotoStore> receipts);
