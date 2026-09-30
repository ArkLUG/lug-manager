#pragma once
#include <crow.h>
#include "middleware/AuthMiddleware.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "auth/AuthService.hpp"
#include "integrations/DiscordOAuth.hpp"

using LugApp = crow::App<AuthMiddleware, ApiKeyMiddleware>;

// public_url: optional canonical base URL (e.g. "https://lug.example.com");
// empty = derive from request headers.
void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth,
                          const std::string& public_url = "");
