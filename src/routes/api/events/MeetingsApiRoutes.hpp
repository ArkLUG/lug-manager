#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/MeetingService.hpp"
#include "services/AuditService.hpp"

void register_meetings_api_routes(LugApp& app, MeetingService& meetings, AuditService& audit);
