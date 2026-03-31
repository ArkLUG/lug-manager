#pragma once
#include "routes/AuthRoutes.hpp"
#include "integrations/CalendarGenerator.hpp"

void register_calendar_routes(LugApp& app, CalendarGenerator& cal);
