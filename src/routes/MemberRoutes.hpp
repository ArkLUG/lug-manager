#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/MemberService.hpp"

void register_member_routes(LugApp& app, MemberService& members);
