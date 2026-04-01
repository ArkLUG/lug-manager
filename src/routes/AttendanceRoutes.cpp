#include "routes/AttendanceRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>

// Returns true if the current user is admin OR the event lead for the given entity.
static bool can_manage_attendance(const crow::request& req, LugApp& app,
                                   EventService& events,
                                   const std::string& entity_type, int64_t entity_id) {
    auto& auth = app.get_context<AuthMiddleware>(req);
    if (auth.auth.role == "admin") return true;
    if (entity_type == "event" && auth.auth.member_id > 0) {
        auto ev = events.get(entity_id);
        if (ev && ev->event_lead_id == auth.auth.member_id) return true;
    }
    return false;
}

// Build the attendance list context and render the template fragment.
static std::string render_attendance_list(AttendanceService& attendance,
                                           const std::string& entity_type, int64_t entity_id,
                                           bool can_manage, bool is_meeting) {
    auto attendees = attendance.get_attendees(entity_type, entity_id);
    crow::mustache::context ctx;
    ctx["entity_type"]      = entity_type;
    ctx["entity_id"]        = static_cast<int>(entity_id);
    ctx["attendance_count"] = static_cast<int>(attendees.size());
    ctx["is_admin"]         = can_manage;
    ctx["is_meeting"]       = is_meeting;

    crow::json::wvalue arr;
    for (size_t i = 0; i < attendees.size(); ++i) {
        arr[i]["id"]                     = attendees[i].id;
        arr[i]["member_id"]              = attendees[i].member_id;
        arr[i]["member_display_name"]    = attendees[i].member_display_name;
        arr[i]["member_discord_username"]= attendees[i].member_discord_username;
        arr[i]["checked_in_at"]          = attendees[i].checked_in_at;
        arr[i]["notes"]                  = attendees[i].notes;
        arr[i]["is_virtual"]             = attendees[i].is_virtual;
        // Propagate into each row so Mustache can access inside {{#attendees}}
        arr[i]["is_admin"]               = can_manage;
        arr[i]["is_meeting"]             = is_meeting;
        arr[i]["entity_type"]            = entity_type;
        arr[i]["entity_id"]              = static_cast<int>(entity_id);
    }
    ctx["attendees"] = std::move(arr);
    auto tmpl = crow::mustache::load("meetings/_attendance.html");
    return tmpl.render(ctx).dump();
}

void register_attendance_routes(LugApp& app, AttendanceService& attendance,
                                EventService& events, MeetingService& meetings) {

    // GET /attendance/overview - admin/lead attendance overview for all members
    CROW_ROUTE(app, "/attendance/overview")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req);
        if (auth.auth.role != "admin") {
            res.code = 403;
            res.write("Forbidden");
            return res;
        }

        auto summaries = attendance.get_all_member_summaries();

        crow::mustache::context ctx;
        ctx["is_admin"] = true;

        crow::json::wvalue arr;
        for (size_t i = 0; i < summaries.size(); ++i) {
            auto& s = summaries[i];
            arr[i]["member_id"]             = s.member_id;
            arr[i]["display_name"]          = s.display_name;
            arr[i]["discord_username"]      = s.discord_username;
            arr[i]["meeting_count"]         = s.meeting_count;
            arr[i]["meeting_virtual_count"] = s.meeting_virtual_count;
            arr[i]["meeting_in_person"]     = s.meeting_count - s.meeting_virtual_count;
            arr[i]["event_count"]           = s.event_count;
            arr[i]["total"]                 = s.meeting_count + s.event_count;
            arr[i]["has_attendance"]        = (s.meeting_count + s.event_count) > 0;
        }
        ctx["members"]      = std::move(arr);
        ctx["member_count"] = static_cast<int>(summaries.size());

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("attendance/_overview.html");
            res.write(tmpl.render(ctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("attendance/_overview.html");
            std::string content = content_tmpl.render(ctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]           = content;
            layout_ctx["page_title"]        = "Attendance Overview";
            layout_ctx["active_attendance"] = true;
            layout_ctx["is_admin"]          = true;
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // GET /attendance - personal attendance history for current user
    CROW_ROUTE(app, "/attendance")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& ctx_auth = app.get_context<AuthMiddleware>(req);
        auto history   = attendance.get_member_history(ctx_auth.auth.member_id);

        crow::mustache::context ctx;
        ctx["title"]     = "My Attendance";
        ctx["member_id"] = ctx_auth.auth.member_id;

        crow::json::wvalue arr;
        for (size_t i = 0; i < history.size(); ++i) {
            arr[i]["entity_type"]  = history[i].entity_type;
            arr[i]["entity_id"]    = history[i].entity_id;
            arr[i]["checked_in_at"]= history[i].checked_in_at;
            arr[i]["notes"]        = history[i].notes;
            arr[i]["is_virtual"]   = history[i].is_virtual;
            arr[i]["type_meeting"] = (history[i].entity_type == "meeting");
            arr[i]["type_event"]   = (history[i].entity_type == "event");
            arr[i]["entity_url"]   = "/" + history[i].entity_type + "s/" +
                                     std::to_string(history[i].entity_id);

            // Look up entity title
            std::string title;
            if (history[i].entity_type == "meeting") {
                auto m = meetings.get(history[i].entity_id);
                if (m) title = m->title;
            } else {
                auto e = events.get(history[i].entity_id);
                if (e) title = e->title;
            }
            arr[i]["entity_title"] = title.empty() ? (history[i].entity_type + " #" + std::to_string(history[i].entity_id)) : title;
            arr[i]["entity_label"] = history[i].entity_type == "meeting" ? "Meeting" : "Event";
        }
        ctx["history"]       = std::move(arr);
        ctx["history_count"] = static_cast<int>(history.size());
        ctx["has_history"]   = !history.empty();

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("attendance/_content.html");
            res.write(tmpl.render(ctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("attendance/_content.html");
            std::string content = content_tmpl.render(ctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]           = content;
            layout_ctx["page_title"]        = "My Attendance";
            layout_ctx["active_attendance"] = true;
            layout_ctx["is_admin"]          = ctx_auth.auth.role == "admin";
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // GET /attendance/count/<type>/<id> - returns live count (for HTMX polling)
    CROW_ROUTE(app, "/attendance/count/<str>/<int>")(
        [&](const crow::request& req, std::string entity_type, int entity_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        int count = attendance.get_count(entity_type, static_cast<int64_t>(entity_id));
        std::string eid = std::to_string(entity_id);
        res.write(
            "<span id=\"attendance-count-" + entity_type + "-" + eid + "\""
            " class=\"text-xs text-gray-400\""
            " hx-get=\"/attendance/count/" + entity_type + "/" + eid + "\""
            " hx-trigger=\"attendanceUpdated from:body\""
            " hx-swap=\"outerHTML\">"
            "<span class=\"font-semibold\">" + std::to_string(count) + "</span> checked in"
            "</span>");
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // GET /attendance/list/<type>/<id> - returns attendee list fragment
    CROW_ROUTE(app, "/attendance/list/<str>/<int>")(
        [&](const crow::request& req, std::string entity_type, int entity_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        bool can_manage = can_manage_attendance(req, app, events,
                                                entity_type, static_cast<int64_t>(entity_id));
        res.write(render_attendance_list(attendance, entity_type,
                                         static_cast<int64_t>(entity_id),
                                         can_manage, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // POST /attendance/admin/checkin - admin/event lead adds a member to an entity
    CROW_ROUTE(app, "/attendance/admin/checkin").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };
        std::string entity_type = gp("entity_type");
        std::string entity_id_s = gp("entity_id");
        bool is_virtual = gp("is_virtual") == "1";

        // Support multiple member_id values (multi-select)
        auto member_ids = params.get_list("member_id", false);

        if (entity_type.empty() || entity_id_s.empty() || member_ids.empty()) {
            res.code = 400;
            res.write(R"(<span class="text-red-500 text-xs">Select at least one member</span>)");
            res.add_header("Content-Type", "text/html");
            return res;
        }

        int64_t entity_id = std::stoll(entity_id_s);
        if (!can_manage_attendance(req, app, events, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            res.add_header("Content-Type", "text/html");
            return res;
        }

        for (auto* mid_str : member_ids) {
            if (!mid_str || !mid_str[0]) continue;
            try {
                int64_t member_id = std::stoll(mid_str);
                attendance.check_in(member_id, entity_type, entity_id, "", is_virtual);
            } catch (...) {}
        }

        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // POST /attendance/admin/<id>/remove - admin/event lead removes an attendance record
    CROW_ROUTE(app, "/attendance/admin/<int>/remove").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };
        std::string entity_type = gp("entity_type");
        std::string entity_id_s = gp("entity_id");

        if (entity_type.empty() || entity_id_s.empty()) {
            res.code = 400;
            res.add_header("Content-Type", "text/html");
            res.write(R"(<span class="text-red-500 text-xs">Missing parameters</span>)");
            return res;
        }

        int64_t entity_id = std::stoll(entity_id_s);
        if (!can_manage_attendance(req, app, events, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            res.add_header("Content-Type", "text/html");
            return res;
        }

        attendance.remove_by_id(static_cast<int64_t>(id));
        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // POST /attendance/admin/<id>/toggle-virtual - admin/event lead toggles virtual flag
    CROW_ROUTE(app, "/attendance/admin/<int>/toggle-virtual").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };
        std::string entity_type = gp("entity_type");
        std::string entity_id_s = gp("entity_id");
        bool current = gp("current") == "1";

        if (entity_type.empty() || entity_id_s.empty()) {
            res.code = 400;
            res.add_header("Content-Type", "text/html");
            res.write(R"(<span class="text-red-500 text-xs">Missing parameters</span>)");
            return res;
        }

        int64_t entity_id = std::stoll(entity_id_s);
        if (!can_manage_attendance(req, app, events, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            res.add_header("Content-Type", "text/html");
            return res;
        }

        attendance.set_virtual(static_cast<int64_t>(id), !current);
        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });
}
