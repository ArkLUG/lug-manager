#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/AuditService.hpp"
#include "services/PhotoStore.hpp"
#include <memory>

// LEGO Fan CoLab extras (admin), feature "fancolab":
//   /fancolab                 recognition, Community Ambassador + history, yearly to-do list
//   /fancolab/summary[.csv]   the year's activity summary, printable / CSV
//   /fancolab/events.csv      the year's public events, one row each
// Public About page, feature "about_page":
//   /about                    no-login page about the group
//   /about/photos/<file>      photos placed on it (only those)
//   /settings/about           admin editor (WYSIWYG, stored as Markdown)
void register_fan_colab_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings,
                               std::shared_ptr<PhotoStore> photos, AuditService& audit);
