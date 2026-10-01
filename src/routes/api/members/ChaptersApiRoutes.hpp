#pragma once
#include "routes/accounts/AuthRoutes.hpp"
#include "services/members/ChapterService.hpp"
#include "services/AuditService.hpp"

void register_chapters_api_routes(LugApp& app, ChapterService& chapters, AuditService& audit);
