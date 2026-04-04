#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/AttendanceService.hpp"
#include "services/EventService.hpp"
#include "services/MeetingService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include "repositories/PerkLevelRepository.hpp"

void register_attendance_routes(LugApp& app, AttendanceService& attendance,
                                EventService& events, MeetingService& meetings,
                                ChapterMemberRepository& chapter_members,
                                PerkLevelRepository& perks);
