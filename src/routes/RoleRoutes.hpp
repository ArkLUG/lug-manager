#pragma once
#include "middleware/AuthMiddleware.hpp"
#include "repositories/RoleMappingRepository.hpp"
#include "services/ChapterService.hpp"
#include "integrations/DiscordClient.hpp"
#include <crow.h>

using LugApp = crow::App<AuthMiddleware>;

void register_role_routes(LugApp& app,
                           RoleMappingRepository& role_mappings,
                           ChapterService& chapters,
                           DiscordClient& discord);
