#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/events/AttendanceService.hpp"
#include "services/events/EventService.hpp"
#include "services/events/MeetingService.hpp"
#include "services/members/MemberService.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "services/AuditService.hpp"

void register_attendance_routes(LugApp& app, AttendanceService& attendance,
                                EventService& events, MeetingService& meetings,
                                MemberService& members,
                                ChapterMemberRepository& chapter_members,
                                PerkLevelRepository& perks, AuditService& audit);
