#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "repositories/EventDayRepository.hpp"
#include "repositories/EventDayAttendanceRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_events_api_routes(LugApp& app, EventService& events, MeetingService& meetings,
                                 EventDayRepository& event_days,
                                 EventDayAttendanceRepository& event_day_attendance,
                                 AttendanceRepository& attendance_flat,
                                 ChapterService& chapters, DiscordClient& discord,
                                 AuditService& audit);
