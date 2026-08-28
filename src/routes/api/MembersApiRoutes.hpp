#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MemberService.hpp"
#include "repositories/MemberRepository.hpp"
#include "services/AuditService.hpp"

void register_members_api_routes(LugApp& app, MemberService& members,
                                  MemberRepository& member_repo, AuditService& audit);
