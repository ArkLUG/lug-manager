#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "services/AttendanceService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "services/MemberService.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"

void register_event_routes(LugApp& app, EventService& events, AttendanceService& attendance,
                            ChapterMemberRepository& chapter_members, DiscordClient& discord,
                            MemberService& members, MeetingService& meetings,
                            ChapterService& chapters);
