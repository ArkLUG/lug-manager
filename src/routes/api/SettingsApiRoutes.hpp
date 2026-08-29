#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/SettingsRepository.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "integrations/GoogleCalendarClient.hpp"
#include "services/AuditService.hpp"

void register_settings_api_routes(LugApp& app, SettingsRepository& settings,
                                   EventService& events, MeetingService& meetings,
                                   GoogleCalendarClient& gcal, AuditService& audit);
