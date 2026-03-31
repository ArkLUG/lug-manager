#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "repositories/SettingsRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "services/MemberSyncService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware>;

void register_settings_routes(LugApp& app, SettingsRepository& settings,
                               DiscordClient& discord, MemberSyncService& member_sync,
                               CalendarGenerator& calendar);
