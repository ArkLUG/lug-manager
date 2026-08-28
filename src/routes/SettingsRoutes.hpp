#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/SettingsRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "services/MemberSyncService.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "services/MemberService.hpp"
#include "services/AuditService.hpp"
#include "repositories/PendingDiscordMatchRepository.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_settings_routes(LugApp& app, SettingsRepository& settings,
                               DiscordClient& discord, MemberSyncService& member_sync,
                               CalendarGenerator& calendar, GoogleCalendarClient& gcal,
                               EventService& events, MeetingService& meetings,
                               MemberService& members, AuditService& audit,
                               PendingDiscordMatchRepository& pending_discord_matches);
