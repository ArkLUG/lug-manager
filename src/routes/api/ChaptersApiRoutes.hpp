#pragma once
#include "routes/AuthRoutes.hpp"
#include "services/ChapterService.hpp"
#include "services/AuditService.hpp"

void register_chapters_api_routes(LugApp& app, ChapterService& chapters, AuditService& audit);
