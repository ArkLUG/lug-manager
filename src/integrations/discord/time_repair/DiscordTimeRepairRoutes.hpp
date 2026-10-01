#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

// Fixes Discord copies of times written by images without time-zone data
// (see services/DiscordTimeRepair.hpp). Edits only, so nobody is notified.
//   GET  /settings/discord-times            admin page
//   POST /settings/discord-times/check      read-only check (htmx table)
//   POST /settings/discord-times/fix        fix what the check finds
//   GET  /api/v1/maintenance/discord-times  same check, JSON (admin API key)
//   POST /api/v1/maintenance/discord-times  same fix, JSON (?include_past=1)
void register_discord_time_repair_routes(LugApp& app, SqliteDatabase& db, DiscordClient& discord, AuditService& audit);
