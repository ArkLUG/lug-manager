#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "integrations/discord/sync/RoleMappingRepository.hpp"
#include "services/members/ChapterService.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/AuditService.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_role_routes(LugApp& app,
                           RoleMappingRepository& role_mappings,
                           ChapterService& chapters,
                           DiscordClient& discord,
                           AuditService& audit);
