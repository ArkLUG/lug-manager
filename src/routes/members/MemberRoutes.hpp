#pragma once
#include "services/notifications/Notifier.hpp"
#include <memory>
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/MemberService.hpp"
#include "repositories/events/AttendanceRepository.hpp"
#include "services/AuditService.hpp"

// notifier/public_url: to send the "confirm your email" link when members change their own.
void register_member_routes(LugApp& app, MemberService& members, AttendanceRepository& attendance_repo, AuditService& audit,
                            std::shared_ptr<Notifier> notifier = nullptr, const std::string& public_url = "");
