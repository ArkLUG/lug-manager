#include "routes/api/AuditLogApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>

// Read-only: no POST/PUT/DELETE routes are registered for audit_log at all (see plan 0.5).

void register_audit_log_api_routes(LugApp& app, AuditService& audit) {

    // GET /api/v1/audit-log?search=&action_filter=&limit=&offset=
    CROW_ROUTE(app, "/api/v1/audit-log").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pg = parse_pagination(req, 50, 200);
        const char* search_p = req.url_params.get("search");
        const char* action_p = req.url_params.get("action_filter");
        std::string search = search_p ? search_p : "";
        std::string action_filter = action_p ? action_p : "";

        auto items = audit.repo().find_paginated(search, action_filter, pg.limit, pg.offset);
        int total = audit.repo().count_filtered(search, action_filter);
        write_json(res, 200, envelope_list(to_json_list(items), total, pg.limit, pg.offset));
        return res;
    });

    // GET /api/v1/audit-log/<id>
    CROW_ROUTE(app, "/api/v1/audit-log/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto entry = audit.repo().find_by_id(static_cast<int64_t>(id));
        if (!entry) { envelope_error(res, 404, "audit log entry not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*entry)));
        return res;
    });
}
