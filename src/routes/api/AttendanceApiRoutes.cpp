#include "routes/api/AttendanceApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

void register_attendance_api_routes(LugApp& app, AttendanceRepository& attendance,
                                     EventDayAttendanceRepository& event_day_attendance,
                                     AuditService& audit) {

    // GET /api/v1/attendance?entity_type=&entity_id=
    CROW_ROUTE(app, "/api/v1/attendance").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        const char* type_p = req.url_params.get("entity_type");
        const char* id_p   = req.url_params.get("entity_id");
        if (!type_p || !id_p) {
            envelope_error(res, 400, "entity_type and entity_id query params required", "invalid_request");
            return res;
        }

        try {
            auto items = attendance.find_by_entity(type_p, std::stoll(std::string(id_p)));
            write_json(res, 200, envelope_ok(to_json_list(items)));
        } catch (const std::exception&) {
            envelope_error(res, 400, "entity_id must be numeric", "invalid_request");
        }
        return res;
    });

    // GET /api/v1/attendance/member/<member_id>
    CROW_ROUTE(app, "/api/v1/attendance/member/<int>").methods("GET"_method)(
        [&](const crow::request& req, int member_id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto items = attendance.find_by_member(static_cast<int64_t>(member_id));
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // POST /api/v1/attendance - check in
    CROW_ROUTE(app, "/api/v1/attendance").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("member_id") || !body.has("entity_type") || !body.has("entity_id")) {
            envelope_error(res, 400, "member_id, entity_type, and entity_id are required", "invalid_request");
            return res;
        }

        int64_t member_id       = body["member_id"].i();
        std::string entity_type = body["entity_type"].s();
        int64_t entity_id       = body["entity_id"].i();
        std::string notes       = body.has("notes") ? std::string(body["notes"].s()) : "";
        bool is_virtual         = body.has("is_virtual") && body["is_virtual"].b();

        // Events are tracked exclusively via event_day_attendance (one row per
        // day attended) - this flat table is meetings-only. Rejecting
        // entity_type="event" here closes off the only remaining path that
        // could write a stray, un-day-scoped attendance row for an event
        // (AttendanceService::check_in already branches correctly for events
        // when called from elsewhere in the app; this route talks to the raw
        // repository directly, bypassing that branch entirely).
        if (entity_type == "event") {
            envelope_error(res, 400,
                "events are tracked per-day - use POST /api/v1/event-day-attendance instead",
                "invalid_request");
            return res;
        }
        if (entity_type != "meeting") {
            envelope_error(res, 400, "entity_type must be \"meeting\"", "invalid_request");
            return res;
        }

        try {
            attendance.check_in(member_id, entity_type, entity_id, notes, is_virtual);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("attendance.check_in", entity_type, entity_id,
                              "member " + std::to_string(member_id),
                              "Checked in via " + actor_label(ctx.api_key));
            crow::json::wvalue body_out;
            body_out["member_id"]   = member_id;
            body_out["entity_type"] = entity_type;
            body_out["entity_id"]   = entity_id;
            write_json(res, 201, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[AttendanceApiRoutes] POST /api/v1/attendance error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not check in", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/attendance/<id> - update is_virtual on an existing record.
    // Mirrors the browser's /attendance/admin/<id>/toggle-virtual action, but
    // takes the target state directly instead of toggling from a client-sent
    // "current" value.
    CROW_ROUTE(app, "/api/v1/attendance/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("is_virtual")) {
            envelope_error(res, 400, "is_virtual is required", "invalid_request");
            return res;
        }

        bool is_virtual = body["is_virtual"].b();
        bool ok = attendance.set_virtual(static_cast<int64_t>(id), is_virtual);
        if (!ok) { envelope_error(res, 404, "attendance record not found", "not_found"); return res; }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("attendance.set_virtual", "attendance", id, "",
                          "Set is_virtual=" + std::string(is_virtual ? "true" : "false") +
                          " via " + actor_label(ctx.api_key));
        crow::json::wvalue body_out;
        body_out["id"]         = id;
        body_out["is_virtual"] = is_virtual;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // DELETE /api/v1/attendance/<id> - write scope (routine check-out correction)
    CROW_ROUTE(app, "/api/v1/attendance/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        bool ok = attendance.remove_by_id(static_cast<int64_t>(id));
        if (!ok) { envelope_error(res, 404, "attendance record not found", "not_found"); return res; }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("attendance.remove", "attendance", id, "",
                          "Removed via " + actor_label(ctx.api_key));
        res.code = 200;
        res.add_header("Content-Type", "application/json");
        res.write(R"({"data":{"success":true}})");
        return res;
    });

    // GET /api/v1/event-day-attendance?event_day_id=
    CROW_ROUTE(app, "/api/v1/event-day-attendance").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        const char* day_p = req.url_params.get("event_day_id");
        if (!day_p) {
            envelope_error(res, 400, "event_day_id query param required", "invalid_request");
            return res;
        }
        try {
            auto items = event_day_attendance.find_by_day(std::stoll(std::string(day_p)));
            write_json(res, 200, envelope_ok(to_json_list(items)));
        } catch (const std::exception&) {
            envelope_error(res, 400, "event_day_id must be numeric", "invalid_request");
        }
        return res;
    });

    // GET /api/v1/event-day-attendance/event/<event_id>
    CROW_ROUTE(app, "/api/v1/event-day-attendance/event/<int>").methods("GET"_method)(
        [&](const crow::request& req, int event_id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto items = event_day_attendance.find_by_event(static_cast<int64_t>(event_id));
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // POST /api/v1/event-day-attendance - check in to a specific day
    CROW_ROUTE(app, "/api/v1/event-day-attendance").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("event_day_id") || !body.has("member_id")) {
            envelope_error(res, 400, "event_day_id and member_id are required", "invalid_request");
            return res;
        }

        int64_t event_day_id = body["event_day_id"].i();
        int64_t member_id    = body["member_id"].i();
        std::string notes    = body.has("notes") ? std::string(body["notes"].s()) : "";

        try {
            event_day_attendance.check_in(event_day_id, member_id, notes);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("event_day_attendance.check_in", "event_day", event_day_id,
                              "member " + std::to_string(member_id),
                              "Checked in via " + actor_label(ctx.api_key));
            crow::json::wvalue body_out;
            body_out["event_day_id"] = event_day_id;
            body_out["member_id"]    = member_id;
            write_json(res, 201, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[AttendanceApiRoutes] POST /api/v1/event-day-attendance error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not check in", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/event-day-attendance/<id> - write scope
    CROW_ROUTE(app, "/api/v1/event-day-attendance/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        bool ok = event_day_attendance.remove_by_id(static_cast<int64_t>(id));
        if (!ok) { envelope_error(res, 404, "event day attendance record not found", "not_found"); return res; }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("event_day_attendance.remove", "event_day_attendance", id, "",
                          "Removed via " + actor_label(ctx.api_key));
        res.code = 200;
        res.add_header("Content-Type", "application/json");
        res.write(R"({"data":{"success":true}})");
        return res;
    });
}
