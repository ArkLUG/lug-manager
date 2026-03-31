#pragma once
#include <crow.h>
#include "middleware/AuthMiddleware.hpp"
#include "auth/AuthService.hpp"
#include "integrations/DiscordOAuth.hpp"

using LugApp = crow::App<AuthMiddleware>;

void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth);
