#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/AuditService.hpp"

// Settings > Reminders (admin): reminders before meetings and events, to
// volunteers and RSVPs, and before dues run out. They work with or without
// Discord: posts go to Discord when it's on, messages to members go by
// Discord DM or email (services/notifications/ReminderService, DuesService).
void register_reminder_settings_routes(LugApp& app, SettingsRepository& settings, AuditService& audit);
