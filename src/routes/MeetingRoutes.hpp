#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MeetingService.hpp"
#include "services/AttendanceService.hpp"
#include "services/ChapterService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "integrations/DiscordClient.hpp"

void register_meeting_routes(LugApp& app, MeetingService& meetings, AttendanceService& attendance,
                              ChapterMemberRepository& chapter_members, ChapterService& chapters,
                              DiscordClient& discord);
