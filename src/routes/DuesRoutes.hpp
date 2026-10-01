#pragma once
#include "routes/AuthRoutes.hpp"
#include "repositories/DuesRepository.hpp"
#include "services/MemberService.hpp"
#include "services/AuditService.hpp"
#include <memory>

void register_dues_routes(LugApp& app, MemberService& members,
                          std::shared_ptr<DuesRepository> dues, AuditService& audit);
