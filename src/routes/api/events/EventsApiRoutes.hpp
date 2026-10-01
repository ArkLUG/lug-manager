#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/EventService.hpp"
#include "services/events/MeetingService.hpp"
#include "repositories/events/EventDayRepository.hpp"
#include "repositories/events/EventDayAttendanceRepository.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "services/AuditService.hpp"

void register_events_api_routes(LugApp& app, EventService& events, MeetingService& meetings,
                                 EventDayRepository& event_days,
                                 EventDayAttendanceRepository& event_day_attendance,
                                 AttendanceRepository& attendance_flat,
                                 AuditService& audit);
