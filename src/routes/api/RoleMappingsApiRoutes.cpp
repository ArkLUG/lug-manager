#include "routes/api/RoleMappingsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

// All verbs (including GET) require admin scope here: role_mappings are the mechanism
// that grants admin/chapter_lead privilege via Discord role sync, so even read access is
// privilege-relevant reconnaissance info. See plan Section 0.4's flagged deviation.

void register_role_mappings_api_routes(LugApp& app, RoleMappingRepository& role_mappings,
                                        AuditService& audit) {

    // GET /api/v1/role-mappings
    CROW_ROUTE(app, "/api/v1/role-mappings").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto items = role_mappings.find_all();
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // POST /api/v1/role-mappings - upsert
    CROW_ROUTE(app, "/api/v1/role-mappings").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("discord_role_id") || !body.has("discord_role_name") || !body.has("lug_role")) {
            envelope_error(res, 400, "discord_role_id, discord_role_name, and lug_role are required", "invalid_request");
            return res;
        }

        std::string discord_role_id   = body["discord_role_id"].s();
        std::string discord_role_name = body["discord_role_name"].s();
        std::string lug_role          = body["lug_role"].s();

        try {
            role_mappings.upsert(discord_role_id, discord_role_name, lug_role);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("role_mapping.upsert", "role_mapping", 0, discord_role_name,
                              "Set lug_role=" + lug_role + " via " + actor_label(ctx.api_key));
            crow::json::wvalue body_out;
            body_out["discord_role_id"]   = discord_role_id;
            body_out["discord_role_name"] = discord_role_name;
            body_out["lug_role"]          = lug_role;
            write_json(res, 201, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[RoleMappingsApiRoutes] POST error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not set role mapping", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/role-mappings/<discord_role_id>
    CROW_ROUTE(app, "/api/v1/role-mappings/<string>").methods("PUT"_method)(
        [&](const crow::request& req, std::string discord_role_id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("discord_role_name") || !body.has("lug_role")) {
            envelope_error(res, 400, "discord_role_name and lug_role are required", "invalid_request");
            return res;
        }

        std::string discord_role_name = body["discord_role_name"].s();
        std::string lug_role          = body["lug_role"].s();

        try {
            role_mappings.upsert(discord_role_id, discord_role_name, lug_role);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("role_mapping.upsert", "role_mapping", 0, discord_role_name,
                              "Set lug_role=" + lug_role + " via " + actor_label(ctx.api_key));
            crow::json::wvalue body_out;
            body_out["discord_role_id"]   = discord_role_id;
            body_out["discord_role_name"] = discord_role_name;
            body_out["lug_role"]          = lug_role;
            write_json(res, 200, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[RoleMappingsApiRoutes] PUT error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update role mapping", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/role-mappings/<discord_role_id>
    CROW_ROUTE(app, "/api/v1/role-mappings/<string>").methods("DELETE"_method)(
        [&](const crow::request& req, std::string discord_role_id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        try {
            role_mappings.remove(discord_role_id);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("role_mapping.remove", "role_mapping", 0, discord_role_id,
                              "Removed via " + actor_label(ctx.api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[RoleMappingsApiRoutes] DELETE error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not remove role mapping", "validation_error");
        }
        return res;
    });
}
