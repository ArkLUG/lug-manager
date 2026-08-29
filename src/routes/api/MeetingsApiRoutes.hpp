#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MeetingService.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_meetings_api_routes(LugApp& app, MeetingService& meetings,
                                   AttendanceRepository& attendance,
                                   ChapterService& chapters, DiscordClient& discord,
                                   AuditService& audit);
