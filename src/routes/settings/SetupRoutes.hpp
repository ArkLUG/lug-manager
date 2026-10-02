#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/ical/CalendarGenerator.hpp"

// First-run setup (/setup):
//  - With no admin yet, a one-time setup token (printed to the server log at
//    start-up) lets whoever runs the server create the first admin.
//  - Afterwards admins get a short checklist: LUG name and time zone,
//    features, Discord server, roles - then "Finish" hides the dashboard
//    banner (setting setup_completed=1).
void register_setup_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings,
                           DiscordClient& discord, CalendarGenerator& calendar, AuditService& audit);

// The token, or "" once an admin exists. For the start-up log line and tests.
std::string setup_token();
// Creates the token if there is no admin and none exists yet; returns it ("" if an admin exists).
std::string ensure_setup_token(SqliteDatabase& db);
