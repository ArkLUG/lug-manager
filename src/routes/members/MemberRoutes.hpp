#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/MemberService.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "services/AuditService.hpp"

void register_member_routes(LugApp& app, MemberService& members, AttendanceRepository& attendance_repo, AuditService& audit);
