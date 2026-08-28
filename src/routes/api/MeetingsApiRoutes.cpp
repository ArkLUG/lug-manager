#include "routes/api/MeetingsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

static std::string api_mt_normalize_datetime(const std::string& dt) {
    if (dt.size() == 16) return dt + ":00";
    return dt;
}

void register_meetings_api_routes(LugApp& app, MeetingService& meetings, AuditService& audit) {

    // GET /api/v1/meetings - paginated list
    CROW_ROUTE(app, "/api/v1/meetings").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pg = parse_pagination(req, 25, 200);
        const char* search_p = req.url_params.get("search");
        std::string search = search_p ? search_p : "";

        auto items = meetings.list_paginated(search, pg.limit, pg.offset);
        int total = meetings.count_filtered(search);
        write_json(res, 200, envelope_list(to_json_list(items), total, pg.limit, pg.offset));
        return res;
    });

    // GET /api/v1/meetings/<id>
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*m)));
        return res;
    });

    // POST /api/v1/meetings - create
    CROW_ROUTE(app, "/api/v1/meetings").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Meeting m;
        if (body.has("title"))       m.title       = body["title"].s();
        if (body.has("description")) m.description = body["description"].s();
        if (body.has("location"))    m.location    = body["location"].s();
        if (body.has("start_time"))  m.start_time  = api_mt_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))    m.end_time    = api_mt_normalize_datetime(body["end_time"].s());
        if (body.has("scope"))       m.scope       = body["scope"].s();
        if (body.has("chapter_id"))  m.chapter_id  = body["chapter_id"].i();
        if (body.has("is_virtual"))  m.is_virtual  = body["is_virtual"].b();
        if (body.has("discord_voice_channel_id")) m.discord_voice_channel_id = body["discord_voice_channel_id"].s();
        if (body.has("notes"))              m.notes              = body["notes"].s();
        if (body.has("suppress_discord"))   m.suppress_discord   = body["suppress_discord"].b();
        if (body.has("suppress_calendar"))  m.suppress_calendar  = body["suppress_calendar"].b();

        try {
            auto created = meetings.create(m);
            audit.log_system("meeting.create", "meeting", created.id, created.title,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] POST /api/v1/meetings error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create meeting", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/meetings/<id> - partial update
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = meetings.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Meeting updates = *existing;
        if (body.has("title"))       updates.title       = body["title"].s();
        if (body.has("description")) updates.description = body["description"].s();
        if (body.has("location"))    updates.location    = body["location"].s();
        if (body.has("start_time"))  updates.start_time  = api_mt_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))    updates.end_time    = api_mt_normalize_datetime(body["end_time"].s());
        if (body.has("scope"))       updates.scope       = body["scope"].s();
        if (body.has("chapter_id"))  updates.chapter_id  = body["chapter_id"].i();
        if (body.has("is_virtual"))  updates.is_virtual  = body["is_virtual"].b();
        if (body.has("discord_voice_channel_id")) updates.discord_voice_channel_id = body["discord_voice_channel_id"].s();
        if (body.has("notes"))  updates.notes  = body["notes"].s();
        if (body.has("status")) updates.status = body["status"].s();

        try {
            auto updated = meetings.update(static_cast<int64_t>(id), updates);
            audit.log_system("meeting.update", "meeting", updated.id, updated.title,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updated)));
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] PUT /api/v1/meetings/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update meeting", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/meetings/<id> - admin scope (destructive)
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }

        try {
            meetings.cancel(static_cast<int64_t>(id));
            audit.log_system("meeting.delete", "meeting", m->id, m->title,
                              "Cancelled via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] DELETE /api/v1/meetings/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 500, "could not cancel meeting", "internal_error");
        }
        return res;
    });
}
