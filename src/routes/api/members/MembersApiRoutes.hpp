#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/MemberService.hpp"
#include "repositories/members/MemberRepository.hpp"
#include "services/AuditService.hpp"

void register_members_api_routes(LugApp& app, MemberService& members,
                                  MemberRepository& member_repo, AuditService& audit);
