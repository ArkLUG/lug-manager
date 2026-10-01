#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MeetingService.hpp"
#include "services/AuditService.hpp"

void register_meetings_api_routes(LugApp& app, MeetingService& meetings, AuditService& audit);
