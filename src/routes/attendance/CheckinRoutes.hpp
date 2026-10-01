#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/MeetingService.hpp"
#include "services/events/EventService.hpp"
#include "services/events/AttendanceService.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"
#include "repositories/events/MeetingRepository.hpp"
#include "repositories/events/EventRepository.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "integrations/discord/DiscordOAuth.hpp"

void register_checkin_routes(LugApp& app,
                              MeetingRepository& meeting_repo,
                              EventRepository& event_repo,
                              MeetingService& meetings,
                              EventService& events,
                              AttendanceService& attendance,
                              MemberService& members,
                              MemberRepository& member_repo,
                              ChapterMemberRepository& chapter_members,
                              DiscordOAuth& oauth,
                              AuditService& audit);
