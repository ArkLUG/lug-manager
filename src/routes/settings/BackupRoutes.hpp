#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/BackupService.hpp"
#include "services/AuditService.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include <memory>

void register_backup_routes(LugApp& app, std::shared_ptr<BackupService> backups,
                            SettingsRepository& settings, AuditService& audit);
