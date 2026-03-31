#include "routes/CalendarRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>

void register_calendar_routes(LugApp& app, CalendarGenerator& cal) {

    // GET /calendar.ics - public iCal feed (no auth required)
    CROW_ROUTE(app, "/calendar.ics")([&](const crow::request& /*req*/) {
        crow::response res;
        res.write(cal.get_ics());
        res.add_header("Content-Type", "text/calendar; charset=utf-8");
        res.add_header("Content-Disposition", "inline; filename=\"lug-calendar.ics\"");
        res.add_header("Cache-Control", "public, max-age=300"); // 5 min cache
        return res;
    });

    // GET /dashboard - main dashboard page
    CROW_ROUTE(app, "/dashboard")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth_ctx = app.get_context<AuthMiddleware>(req);

        crow::mustache::context ctx;
        ctx["title"]      = "Dashboard";
        ctx["member_id"]  = auth_ctx.auth.member_id;
        ctx["role"]       = auth_ctx.auth.role;
        ctx["is_admin"]   = auth_ctx.auth.role == "admin";
        ctx["is_member"]  = auth_ctx.auth.role == "member" || auth_ctx.auth.role == "admin";

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("dashboard/_content.html");
            res.write(tmpl.render(ctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("dashboard/_content.html");
            std::string content = content_tmpl.render(ctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]           = content;
            layout_ctx["page_title"]        = "Dashboard";
            layout_ctx["active_dashboard"]  = true;
            layout_ctx["is_admin"]          = auth_ctx.auth.role == "admin";
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });
}
