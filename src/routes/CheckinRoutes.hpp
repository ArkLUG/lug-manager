#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MeetingService.hpp"
#include "services/EventService.hpp"
#include "services/AttendanceService.hpp"
#include "services/MemberService.hpp"
#include "repositories/MeetingRepository.hpp"
#include "repositories/EventRepository.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "integrations/DiscordOAuth.hpp"
#include "integrations/DiscordClient.hpp"

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
                              DiscordClient& discord);
