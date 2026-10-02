#pragma once
#include "db/SqliteDatabase.hpp"
#include "routes/accounts/AuthRoutes.hpp"

// GET /healthz - "ok" when the server answers and its database works; 503
// otherwise. For Docker's HEALTHCHECK (lug_manager --healthcheck), Unraid
// and the reverse proxy. Says nothing else, so it's safe to leave public.
void register_health_routes(LugApp& app, SqliteDatabase& db);

// `lug_manager --healthcheck`: asks the running server's /healthz on this
// machine (LUG_PORT). Returns the process exit code: 0 healthy, 1 not.
int run_healthcheck();
