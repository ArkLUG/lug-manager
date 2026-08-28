#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"
#include "services/AuditService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_role_routes(LugApp& app,
                           RoleMappingRepository& role_mappings,
                           ChapterService& chapters,
                           DiscordClient& discord,
                           AuditService& audit);
