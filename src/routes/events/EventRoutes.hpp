#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/EventService.hpp"
#include "services/events/MeetingService.hpp"
#include "services/events/AttendanceService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "services/members/MemberService.hpp"
#include "services/members/ChapterService.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"

void register_event_routes(LugApp& app, EventService& events, AttendanceService& attendance,
                            ChapterMemberRepository& chapter_members, DiscordClient& discord,
                            MemberService& members, MeetingService& meetings,
                            ChapterService& chapters, AuditService& audit);
