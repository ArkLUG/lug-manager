#include "routes/api/SettingsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include <crow.h>

// All verbs require admin scope: settings can include operationally sensitive values
// (e.g. discord_guild_id). See plan Section 0.4's flagged deviation.
//
// No list-all-settings endpoint: SettingsRepository has no enumerate method; key lookups
// are always by known key name in the existing codebase (see plan Section 1.7, item 10).

void register_settings_api_routes(LugApp& app, SettingsRepository& settings, AuditService& audit) {

    // GET /api/v1/settings/<key>
    CROW_ROUTE(app, "/api/v1/settings/<string>").methods("GET"_method)(
        [&](const crow::request& req, std::string key) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        std::string value = settings.get(key, "");
        crow::json::wvalue body;
        body["key"]   = key;
        body["value"] = value;
        write_json(res, 200, envelope_ok(std::move(body)));
        return res;
    });

    // PUT /api/v1/settings/<key>
    CROW_ROUTE(app, "/api/v1/settings/<string>").methods("PUT"_method)(
        [&](const crow::request& req, std::string key) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("value")) {
            envelope_error(res, 400, "value is required", "invalid_request");
            return res;
        }

        std::string value = body["value"].s();
        settings.set(key, value);
        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("settings.update", "settings", 0, key,
                          "Set via " + actor_label(ctx.api_key));

        crow::json::wvalue body_out;
        body_out["key"]   = key;
        body_out["value"] = value;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });
}
