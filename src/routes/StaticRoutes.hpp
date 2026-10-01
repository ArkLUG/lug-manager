#pragma once
#include "routes/AuthRoutes.hpp"

// Serves /static/<path> from src/static with an in-memory cache, ETags,
// long-lived caching for versioned URLs (?v=) and gzip (via Crow's
// compression) for text assets. Replaces Crow's built-in static handler,
// which sent no caching headers and no compression.
void register_static_routes(LugApp& app);
