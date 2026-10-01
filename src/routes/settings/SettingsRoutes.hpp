#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "integrations/ical/CalendarGenerator.hpp"
#include "integrations/google_calendar/GoogleCalendarClient.hpp"
#include "integrations/discord/sync/MemberSyncService.hpp"
#include "services/events/EventService.hpp"
#include "services/events/MeetingService.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"
#include "integrations/discord/matches/PendingDiscordMatchRepository.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_settings_routes(LugApp& app, SettingsRepository& settings,
                               DiscordClient& discord, MemberSyncService& member_sync,
                               CalendarGenerator& calendar, GoogleCalendarClient& gcal,
                               EventService& events, MeetingService& meetings,
                               MemberService& members, AuditService& audit,
                               PendingDiscordMatchRepository& pending_discord_matches);
