#include "routes/api/PerkLevelsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>
#include <ctime>

void register_perk_levels_api_routes(LugApp& app, PerkLevelRepository& perks,
                                      MemberRepository& members, AttendanceRepository& attendance,
                                      DiscordClient& discord, AuditService& audit) {

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

    // POST /api/v1/perk-levels/clone - copy every tier from source_year to target_year.
    // Mirrors the browser's /perks/clone route. Fails if target_year already has tiers
    // (same guard as the browser route, so this never silently duplicates a year's setup).
    CROW_ROUTE(app, "/api/v1/perk-levels/clone").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("source_year") || !body.has("target_year")) {
            envelope_error(res, 400, "source_year and target_year are required", "invalid_request");
            return res;
        }
        int source_year = static_cast<int>(body["source_year"].i());
        int target_year = static_cast<int>(body["target_year"].i());
        if (source_year == 0 || target_year == 0 || source_year == target_year) {
            envelope_error(res, 400, "source_year and target_year must be non-zero and different", "invalid_request");
            return res;
        }

        auto existing = perks.find_by_year(target_year);
        if (!existing.empty()) {
            envelope_error(res, 400, std::to_string(target_year) + " already has " +
                            std::to_string(existing.size()) + " tiers - delete them first", "conflict");
            return res;
        }

        int cloned = perks.clone_year(source_year, target_year);
        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("perk.clone", "perk", 0, "",
                          "Cloned " + std::to_string(cloned) + " tiers from " + std::to_string(source_year) +
                          " to " + std::to_string(target_year) + " via " + actor_label(ctx.api_key));
        crow::json::wvalue body_out;
        body_out["cloned"] = cloned;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // POST /api/v1/perk-levels/sync-roles - bulk sync every member's Discord perk roles
    // against the current calendar year's tiers. Mirrors the browser's
    // /api/perks/sync-roles route exactly (per-tier add/remove, not a single "best tier").
    CROW_ROUTE(app, "/api/v1/perk-levels/sync-roles").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        std::time_t now = std::time(nullptr);
        int year = std::localtime(&now)->tm_year + 1900;
        auto levels = perks.find_by_year(year);
        if (levels.empty()) {
            crow::json::wvalue body_out;
            body_out["synced"] = 0;
            body_out["year"]   = year;
            body_out["note"]   = "No perk levels defined for " + std::to_string(year);
            write_json(res, 200, envelope_ok(std::move(body_out)));
            return res;
        }

        auto all_members = members.find_all();
        int synced = 0;
        for (const auto& m : all_members) {
            int meeting_count = attendance.count_member_by_year(m.id, year, "meeting");
            int event_count   = attendance.count_member_by_year(m.id, year, "event");

            for (const auto& lvl : levels) {
                if (lvl.discord_role_id.empty() || m.discord_user_id.empty()) continue;

                bool meets_tier = meeting_count >= lvl.meeting_attendance_required &&
                                  event_count >= lvl.event_attendance_required &&
                                  (!lvl.requires_paid_dues || m.is_paid) &&
                                  fol_rank(m.fol_status) >= fol_rank(lvl.min_fol_status);

                if (meets_tier) {
                    try { discord.add_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                } else {
                    try { discord.remove_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                }
            }
            ++synced;
        }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("perk.sync_roles", "perk", 0, "",
                          "Synced " + std::to_string(synced) + " members via " + actor_label(ctx.api_key));
        crow::json::wvalue body_out;
        body_out["synced"] = synced;
        body_out["year"]   = year;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });
}
