#pragma once
#include "auth/AuthService.hpp"
#include "routes/accounts/AuthRoutes.hpp"
#include <string>

// GET /live (websocket) - signed-in pages connect here to hear what changed
// (see LiveHub.hpp). The site's public address is an allowed Origin besides
// the request's own host.
void register_live_routes(LugApp& app, AuthService& auth);
