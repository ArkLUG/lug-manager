#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/MeetingService.hpp"
#include "services/events/AttendanceService.hpp"
#include "services/members/ChapterService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_meeting_routes(LugApp& app, MeetingService& meetings, AttendanceService& attendance,
                              ChapterMemberRepository& chapter_members, ChapterService& chapters,
                              DiscordClient& discord, AuditService& audit);
