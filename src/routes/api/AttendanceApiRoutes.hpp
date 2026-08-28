#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "services/AuditService.hpp"

void register_attendance_api_routes(LugApp& app, AttendanceRepository& attendance,
                                     EventDayAttendanceRepository& event_day_attendance,
                                     AuditService& audit);
