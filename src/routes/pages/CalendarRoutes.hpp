#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "integrations/ical/CalendarGenerator.hpp"
#include "repositories/members/PerkLevelRepository.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "repositories/members/MemberRepository.hpp"

void register_calendar_routes(LugApp& app, CalendarGenerator& cal,
                               PerkLevelRepository& perks,
                               AttendanceRepository& attendance_repo,
                               MemberRepository& member_repo);
