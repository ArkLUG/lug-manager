#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/events/EventService.hpp"
#include "services/events/MeetingService.hpp"
#include "integrations/google_calendar/GoogleCalendarClient.hpp"
#include "services/AuditService.hpp"

void register_settings_api_routes(LugApp& app, SettingsRepository& settings,
                                   EventService& events, MeetingService& meetings,
                                   GoogleCalendarClient& gcal, AuditService& audit);
