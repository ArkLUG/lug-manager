#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/BackupService.hpp"
#include "services/AuditService.hpp"
#include "repositories/SettingsRepository.hpp"
#include <memory>

void register_backup_routes(LugApp& app, std::shared_ptr<BackupService> backups,
                            SettingsRepository& settings, AuditService& audit);
