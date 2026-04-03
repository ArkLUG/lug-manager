#include "routes/CalendarRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <ctime>

void register_calendar_routes(LugApp& app, CalendarGenerator& cal,
                               PerkLevelRepository& perks,
                               AttendanceRepository& attendance_repo,
                               MemberRepository& member_repo) {

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

        // Perk progress for current user
        {
            std::time_t now = std::time(nullptr);
            std::tm* tm = std::localtime(&now);
            int year = tm->tm_year + 1900;

            int meeting_count = attendance_repo.count_member_by_year(auth_ctx.auth.member_id, year, "meeting");
            int event_count   = attendance_repo.count_member_by_year(auth_ctx.auth.member_id, year, "event");

            ctx["perk_meeting_count"] = meeting_count;
            ctx["perk_event_count"]   = event_count;
            ctx["perk_year"]          = year;

            auto levels = perks.find_all();
            std::string achieved_name;
            std::string next_name;
            int next_meetings_needed = 0;
            int next_events_needed = 0;

            auto member = member_repo.find_by_id(auth_ctx.auth.member_id);
            bool is_paid = member && member->is_paid;

            for (const auto& lvl : levels) {
                bool meets = meeting_count >= lvl.meeting_attendance_required &&
                             event_count >= lvl.event_attendance_required &&
                             (!lvl.requires_paid_dues || is_paid);
                if (meets) {
                    achieved_name = lvl.name;
                } else if (next_name.empty()) {
                    next_name = lvl.name;
                    next_meetings_needed = std::max(0, lvl.meeting_attendance_required - meeting_count);
                    next_events_needed = std::max(0, lvl.event_attendance_required - event_count);
                }
            }

            ctx["perk_achieved"]         = !achieved_name.empty();
            ctx["perk_achieved_name"]    = achieved_name;
            ctx["perk_has_next"]         = !next_name.empty();
            ctx["perk_next_name"]        = next_name;
            ctx["perk_next_meetings"]    = next_meetings_needed;
            ctx["perk_next_events"]      = next_events_needed;
            ctx["has_perks"]             = !levels.empty();
        }

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
