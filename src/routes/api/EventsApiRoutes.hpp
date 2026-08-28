#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/EventService.hpp"
#include "repositories/EventDayRepository.hpp"
#include "services/AuditService.hpp"

void register_events_api_routes(LugApp& app, EventService& events,
                                 EventDayRepository& event_days, AuditService& audit);
