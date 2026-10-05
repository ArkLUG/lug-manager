#pragma once
#include "routes/accounts/AuthRoutes.hpp"

// GET /settings/overview - every settings page, grouped (the sidebar's Settings)
void register_settings_overview_routes(LugApp& app);
