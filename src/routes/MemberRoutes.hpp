#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MemberService.hpp"
#include "repositories/AttendanceRepository.hpp"
#include "services/AuditService.hpp"

void register_member_routes(LugApp& app, MemberService& members, AttendanceRepository& attendance_repo, AuditService& audit);
