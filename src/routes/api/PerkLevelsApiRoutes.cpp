#include "routes/api/PerkLevelsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

void register_perk_levels_api_routes(LugApp& app, PerkLevelRepository& perks, AuditService& audit) {

    // GET /api/v1/perk-levels?year=
    CROW_ROUTE(app, "/api/v1/perk-levels").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        const char* year_p = req.url_params.get("year");
        std::vector<PerkLevel> items;
        if (year_p) {
            try { items = perks.find_by_year(std::stoi(std::string(year_p))); }
            catch (const std::exception&) {
                envelope_error(res, 400, "year must be numeric", "invalid_request");
                return res;
            }
        } else {
            items = perks.find_all();
        }
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // GET /api/v1/perk-levels/<id>
    CROW_ROUTE(app, "/api/v1/perk-levels/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto p = perks.find_by_id(static_cast<int64_t>(id));
        if (!p) { envelope_error(res, 404, "perk level not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*p)));
        return res;
    });

    // POST /api/v1/perk-levels
    CROW_ROUTE(app, "/api/v1/perk-levels").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        PerkLevel p;
        if (body.has("name"))                        p.name                        = body["name"].s();
        if (body.has("description"))                 p.description                 = body["description"].s();
        if (body.has("discord_role_id"))              p.discord_role_id             = body["discord_role_id"].s();
        if (body.has("meeting_attendance_required"))  p.meeting_attendance_required = static_cast<int>(body["meeting_attendance_required"].i());
        if (body.has("event_attendance_required"))    p.event_attendance_required   = static_cast<int>(body["event_attendance_required"].i());
        if (body.has("requires_paid_dues"))           p.requires_paid_dues          = body["requires_paid_dues"].b();
        if (body.has("min_fol_status"))               p.min_fol_status              = body["min_fol_status"].s();
        if (body.has("sort_order"))                   p.sort_order                  = static_cast<int>(body["sort_order"].i());
        if (body.has("year"))                         p.year                        = static_cast<int>(body["year"].i());

        try {
            auto created = perks.create(p);
            audit.log_system("perk_level.create", "perk", created.id, created.name,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[PerkLevelsApiRoutes] POST error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create perk level", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/perk-levels/<id>
    CROW_ROUTE(app, "/api/v1/perk-levels/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = perks.find_by_id(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "perk level not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        PerkLevel updates = *existing;
        if (body.has("name"))                        updates.name                        = body["name"].s();
        if (body.has("description"))                 updates.description                 = body["description"].s();
        if (body.has("discord_role_id"))              updates.discord_role_id             = body["discord_role_id"].s();
        if (body.has("meeting_attendance_required"))  updates.meeting_attendance_required = static_cast<int>(body["meeting_attendance_required"].i());
        if (body.has("event_attendance_required"))    updates.event_attendance_required   = static_cast<int>(body["event_attendance_required"].i());
        if (body.has("requires_paid_dues"))           updates.requires_paid_dues          = body["requires_paid_dues"].b();
        if (body.has("min_fol_status"))               updates.min_fol_status              = body["min_fol_status"].s();
        if (body.has("sort_order"))                   updates.sort_order                  = static_cast<int>(body["sort_order"].i());
        if (body.has("year"))                         updates.year                        = static_cast<int>(body["year"].i());

        try {
            perks.update(updates);
            audit.log_system("perk_level.update", "perk", updates.id, updates.name,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updates)));
        } catch (const std::exception& ex) {
            std::cerr << "[PerkLevelsApiRoutes] PUT /api/v1/perk-levels/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update perk level", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/perk-levels/<id> - admin scope (Discord-role-linked)
    CROW_ROUTE(app, "/api/v1/perk-levels/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto p = perks.find_by_id(static_cast<int64_t>(id));
        if (!p) { envelope_error(res, 404, "perk level not found", "not_found"); return res; }

        bool ok = perks.remove(static_cast<int64_t>(id));
        if (!ok) { envelope_error(res, 500, "could not remove perk level", "internal_error"); return res; }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("perk_level.delete", "perk", p->id, p->name,
                          "Deleted via " + actor_label(ctx.api_key));
        res.code = 200;
        res.add_header("Content-Type", "application/json");
        res.write(R"({"data":{"success":true}})");
        return res;
    });
}
