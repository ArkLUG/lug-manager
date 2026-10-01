#include "routes/CalendarRoutes.hpp"
#include "services/PerkProgress.hpp"
#include "services/DuesService.hpp"
#include "utils/LocalTime.hpp"
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
                                        : auth_ctx.auth.role == "chapter_lead" ? "Chapter Lead"
                                        : auth_ctx.auth.role == "moderator" ? "Moderator" : "Member";
            ctx["member_role_admin"]    = auth_ctx.auth.role == "admin";
            ctx["member_role_chapter_lead"] = auth_ctx.auth.role == "chapter_lead";
            ctx["member_role_moderator"]    = auth_ctx.auth.role == "moderator";
        }

        // Perk progress for current user
        {
            std::time_t now = std::time(nullptr);
            std::tm tm_buf = local_tm(now);
        std::tm* tm = &tm_buf;
            int year = tm->tm_year + 1900;

            int meeting_count = attendance_repo.count_member_by_year(auth_ctx.auth.member_id, year, "meeting");
            int event_count   = attendance_repo.count_member_by_year(auth_ctx.auth.member_id, year, "event");

            ctx["perk_meeting_count"] = meeting_count;
            ctx["perk_event_count"]   = event_count;
            ctx["perk_year"]          = year;

            auto levels = perks.find_by_year(year);
            auto member = member_repo.find_by_id(auth_ctx.auth.member_id);
            bool is_paid = member && member->is_paid;
            auto pp = compute_perk_progress(levels, meeting_count, event_count, is_paid,
                                            member ? member->fol_status : "afol");

            ctx["perk_achieved"]         = !pp.achieved.empty();
            ctx["perk_achieved_name"]    = pp.achieved;
            ctx["perk_achieved_desc"]    = pp.achieved_desc;
            ctx["perk_has_next"]         = !pp.next.empty();
            ctx["perk_next_name"]        = pp.next;
            ctx["perk_next_desc"]        = pp.next_desc;
            ctx["perk_next_meetings"]    = pp.meetings_needed;
            ctx["perk_next_events"]      = pp.events_needed;
            ctx["perk_show_meetings"]    = pp.meetings_needed > 0;
            ctx["perk_show_events"]      = pp.events_needed > 0;
            ctx["perk_next_needs_dues"]  = pp.needs_dues;
            ctx["perk_next_needs_fol"]   = !pp.needs_fol.empty();
            ctx["perk_is_paid"]          = is_paid;

            // Admins: members within 2 check-ins of their next tier, closest
            // first - a nudge list ("one more meeting gets you Gold").
            if (auth_ctx.auth.is_admin() && !levels.empty()) {
                AttendanceRepository::OverviewParams op;
                op.year = year;
                op.limit = 100000;
                struct Close { std::string name; std::string tier; int m; int e; int gap; };
                std::vector<Close> close;
                for (const auto& s : attendance_repo.get_overview_paginated(op)) {
                    int in_person = s.meeting_count - s.meeting_virtual_count;
                    auto mp = compute_perk_progress(levels, in_person, s.event_count, s.is_paid, s.fol_status);
                    if (mp.next.empty() || mp.needs_dues || !mp.needs_fol.empty()) continue;
                    if (mp.gap() < 1 || mp.gap() > 2) continue;
                    close.push_back({s.display_name, mp.next, mp.meetings_needed, mp.events_needed, mp.gap()});
                }
                std::stable_sort(close.begin(), close.end(),
                                 [](const Close& x, const Close& y) { return x.gap < y.gap; });
                if (close.size() > 10) close.resize(10);
                crow::json::wvalue arr;
                for (size_t i = 0; i < close.size(); ++i) {
                    arr[i]["name"]     = close[i].name;
                    arr[i]["tier"]     = close[i].tier;
                    arr[i]["meetings"] = close[i].m;
                    arr[i]["events"]   = close[i].e;
                    arr[i]["show_m"]   = close[i].m > 0;
                    arr[i]["show_e"]   = close[i].e > 0;
                }
                ctx["close_to_tier"]     = std::move(arr);
                ctx["has_close_to_tier"] = !close.empty();
            }
            ctx["has_perks"]             = !levels.empty();
        }

        // Chapter leads+: dues expiring in the next 30 days
        if (auth_ctx.auth.is_chapter_lead()) {
            DuesRepository dues(attendance_repo.db());
            std::time_t now_d = std::time(nullptr);
            auto rows = dues.expiring_between(DuesService::ymd(now_d), DuesService::ymd(now_d + 30 * 86400));
            crow::json::wvalue arr;
            for (size_t i = 0; i < rows.size() && i < 15; ++i) {
                arr[i]["id"]         = rows[i].member_id;
                arr[i]["name"]       = rows[i].display_name;
                arr[i]["paid_until"] = rows[i].paid_until;
            }
            ctx["dues_expiring"]       = std::move(arr);
            ctx["has_dues_expiring"]   = !rows.empty();
            ctx["dues_expiring_count"] = static_cast<int>(rows.size());
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("dashboard/_content.html");
            res.write(tmpl.render(ctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("dashboard/_content.html");
            std::string content = content_tmpl.render(ctx).dump();
            res.write(render_in_layout(req, app, content, "Dashboard", "active_dashboard"));
        }
        return res;
    });
}
