#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "repositories/SettingsRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "services/MemberSyncService.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "services/MemberService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware>;

void register_settings_routes(LugApp& app, SettingsRepository& settings,
                               DiscordClient& discord, MemberSyncService& member_sync,
                               CalendarGenerator& calendar, GoogleCalendarClient& gcal,
                               EventService& events, MeetingService& meetings,
                               MemberService& members);
