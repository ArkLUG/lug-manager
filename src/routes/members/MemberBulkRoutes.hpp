#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/MemberService.hpp"
#include "services/AuditService.hpp"

// POST /members/bulk - apply one change to many members (chapter lead+).
void register_member_bulk_routes(LugApp& app, MemberService& members, AuditService& audit);
