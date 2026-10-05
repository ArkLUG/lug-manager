#pragma once
#include "routes/accounts/AuthRoutes.hpp"

// Serves /static/<path> from src/static with an in-memory cache, ETags,
// long-lived caching for versioned URLs (?v=) and gzip (via Crow's
// compression) for text assets. Replaces Crow's built-in static handler,
// which sent no caching headers and no compression.
void register_static_routes(LugApp& app);

// Unknown paths: a complete 404 page (or JSON under /api/). Crow's own reply
// for an unmatched route is cut short and a proxy turns it into a 500.
// Register it last: it matches any path no other route does.
void register_not_found_route(LugApp& app);
