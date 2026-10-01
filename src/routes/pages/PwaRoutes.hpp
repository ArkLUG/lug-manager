#pragma once
#include "routes/accounts/AuthRoutes.hpp"

// /sw.js and /manifest.webmanifest at the site root (a service worker's scope
// is limited to its own path, so it can't be served from /static/).
void register_pwa_routes(LugApp& app);
