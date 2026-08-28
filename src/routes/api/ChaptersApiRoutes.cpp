#include "routes/api/ChaptersApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

void register_chapters_api_routes(LugApp& app, ChapterService& chapters, AuditService& audit) {

    // GET /api/v1/chapters - full list, no pagination (small table)
    CROW_ROUTE(app, "/api/v1/chapters").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto items = chapters.list_all();
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // GET /api/v1/chapters/<id>
    CROW_ROUTE(app, "/api/v1/chapters/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto c = chapters.get(static_cast<int64_t>(id));
        if (!c) { envelope_error(res, 404, "chapter not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*c)));
        return res;
    });

    // POST /api/v1/chapters
    CROW_ROUTE(app, "/api/v1/chapters").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Chapter c;
        if (body.has("name"))        c.name        = body["name"].s();
        if (body.has("shorthand"))   c.shorthand   = body["shorthand"].s();
        if (body.has("description")) c.description = body["description"].s();
        if (body.has("discord_announcement_channel_id")) c.discord_announcement_channel_id = body["discord_announcement_channel_id"].s();
        if (body.has("discord_lead_role_id"))   c.discord_lead_role_id   = body["discord_lead_role_id"].s();
        if (body.has("discord_member_role_id")) c.discord_member_role_id = body["discord_member_role_id"].s();

        try {
            auto created = chapters.create(c);
            audit.log_system("chapter.create", "chapter", created.id, created.name,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[ChaptersApiRoutes] POST /api/v1/chapters error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create chapter", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/chapters/<id>
    CROW_ROUTE(app, "/api/v1/chapters/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = chapters.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "chapter not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Chapter updates = *existing;
        if (body.has("name"))        updates.name        = body["name"].s();
        if (body.has("shorthand"))   updates.shorthand   = body["shorthand"].s();
        if (body.has("description")) updates.description = body["description"].s();
        if (body.has("discord_announcement_channel_id")) updates.discord_announcement_channel_id = body["discord_announcement_channel_id"].s();
        if (body.has("discord_lead_role_id"))   updates.discord_lead_role_id   = body["discord_lead_role_id"].s();
        if (body.has("discord_member_role_id")) updates.discord_member_role_id = body["discord_member_role_id"].s();

        try {
            auto updated = chapters.update(static_cast<int64_t>(id), updates);
            audit.log_system("chapter.update", "chapter", updated.id, updated.name,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updated)));
        } catch (const std::exception& ex) {
            std::cerr << "[ChaptersApiRoutes] PUT /api/v1/chapters/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update chapter", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/chapters/<id> - admin scope (cascades chapter_members/content)
    CROW_ROUTE(app, "/api/v1/chapters/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto c = chapters.get(static_cast<int64_t>(id));
        if (!c) { envelope_error(res, 404, "chapter not found", "not_found"); return res; }

        try {
            chapters.delete_chapter(static_cast<int64_t>(id));
            audit.log_system("chapter.delete", "chapter", c->id, c->name,
                              "Deleted via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[ChaptersApiRoutes] DELETE /api/v1/chapters/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 500, "could not delete chapter", "internal_error");
        }
        return res;
    });
}
