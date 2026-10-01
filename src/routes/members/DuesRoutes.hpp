#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "repositories/members/DuesRepository.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_dues_routes(LugApp& app, MemberService& members,
                          std::shared_ptr<DuesRepository> dues, AuditService& audit);
