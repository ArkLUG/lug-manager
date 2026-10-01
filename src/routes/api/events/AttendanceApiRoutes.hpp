#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/events/EventDayAttendanceRepository.hpp"
#include "services/AuditService.hpp"

void register_attendance_api_routes(LugApp& app, AttendanceRepository& attendance,
                                     EventDayAttendanceRepository& event_day_attendance,
                                     AuditService& audit);
