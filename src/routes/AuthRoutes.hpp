#pragma once
#include <crow.h>
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "auth/AuthService.hpp"
#include "integrations/DiscordOAuth.hpp"

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth);
