#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/SettingsRepository.hpp"
#include "services/AuditService.hpp"

// Public "upcoming shows" page (/shows, /shows?embed=1 for an iframe) and
// JSON feed (/shows.json) listing upcoming non-private, non-cancelled events.
// Off until an admin enables it under Settings.
void register_shows_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuditService& audit);
