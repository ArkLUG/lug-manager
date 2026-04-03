#pragma once
#include "routes/AuthRoutes.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "repositories/PerkLevelRepository.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "repositories/MemberRepository.hpp"

void register_calendar_routes(LugApp& app, CalendarGenerator& cal,
                               PerkLevelRepository& perks,
                               AttendanceRepository& attendance_repo,
                               MemberRepository& member_repo);
