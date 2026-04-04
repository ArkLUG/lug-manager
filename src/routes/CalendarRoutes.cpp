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
        ctx["is_admin"]   = auth_ctx.auth.is_admin();
        ctx["is_chapter_lead"] = auth_ctx.auth.is_chapter_lead();
        ctx["is_member"]  = auth_ctx.auth.role == "member" || auth_ctx.auth.role == "admin";

        // Member profile info
        auto member_info = member_repo.find_by_id(auth_ctx.auth.member_id);
        if (member_info) {
            ctx["member_display_name"]  = member_info->display_name;
            ctx["member_initial"]       = member_info->display_name.empty() ? std::string("?")
                                        : std::string(1, member_info->display_name[0]);
            ctx["member_first_name"]    = member_info->first_name;
            ctx["member_last_name"]     = member_info->last_name;
            ctx["member_email"]         = member_info->email;
            ctx["member_phone"]         = member_info->phone;
            ctx["member_chapter"]       = member_info->chapter_name;
            ctx["member_has_chapter"]   = !member_info->chapter_name.empty();
            ctx["member_is_paid"]       = member_info->is_paid;
            ctx["member_paid_until"]    = member_info->paid_until;
            ctx["member_fol_status"]    = member_info->fol_status.empty() ? "afol" : member_info->fol_status;
            ctx["member_fol_label"]     = member_info->fol_status == "kfol" ? "KFOL"
                                        : member_info->fol_status == "tfol" ? "TFOL" : "AFOL";
            ctx["member_role_label"]    = auth_ctx.auth.role == "admin" ? "Admin"
                                        : auth_ctx.auth.role == "chapter_lead" ? "Chapter Lead" : "Member";
            ctx["member_role_admin"]    = auth_ctx.auth.role == "admin";
            ctx["member_role_chapter_lead"] = auth_ctx.auth.role == "chapter_lead";
        }

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

            auto levels = perks.find_by_year(year);
            std::string achieved_name;
            std::string next_name;
            int next_meetings_needed = 0;
            int next_events_needed = 0;

            auto member = member_repo.find_by_id(auth_ctx.auth.member_id);
            bool is_paid = member && member->is_paid;
            std::string member_fol = member ? member->fol_status : "afol";

            for (const auto& lvl : levels) {
                bool meets = meeting_count >= lvl.meeting_attendance_required &&
                             event_count >= lvl.event_attendance_required &&
                             (!lvl.requires_paid_dues || is_paid) &&
                             fol_rank(member_fol) >= fol_rank(lvl.min_fol_status);
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
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });
}
