#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"
#include "services/PhotoStore.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include <memory>

// Treasury: other income and expenses alongside recorded dues, with yearly
// totals, categories, per-event totals, monthly breakdown, CSV, receipts
// (photo/PDF) and dues recording. Admins and treasurers edit; who else can see
// each part is set under Settings > Treasury (services/TreasuryAccess.hpp).
void register_treasury_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuditService& audit,
                              std::shared_ptr<PhotoStore> receipts);
