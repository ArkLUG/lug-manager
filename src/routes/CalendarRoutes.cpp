#include "routes/CalendarRoutes.hpp"
#include "auth/SessionStore.hpp"
#include "utils/Crypto.hpp"
#include "services/PerkProgress.hpp"
#include "services/DuesService.hpp"
#include "services/FanCoLab.hpp"
#include "routes/DashboardWidgets.hpp"
#include "services/Features.hpp"
#include "utils/LocalTime.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <ctime>
#include <chrono>
#include <mutex>

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
    // GET /calendar/chapter/<id>/feed.ics[?lug_wide=0] - one chapter's items
    // (+ LUG-wide items unless lug_wide=0). Public, like /calendar.ics.
    CROW_ROUTE(app, "/calendar/chapter/<int>/feed.ics")([&](const crow::request& req, int chapter_id) {
        crow::response res;
        CalendarGenerator::Filter f;
        f.chapter_id = chapter_id;
        const char* lw = req.url_params.get("lug_wide");
        f.include_lug_wide = !(lw && std::string(lw) == "0");
        f.name_suffix = " (chapter " + std::to_string(chapter_id) + ")";
        res.write(cal.get_ics(f));
        res.add_header("Content-Type", "text/calendar; charset=utf-8");
        res.add_header("Cache-Control", "public, max-age=300");
        return res;
    });

    // GET /calendar/me/<token>/feed.ics - personal feed with full details of
    // private items. The token is the credential (stored hashed).
    CROW_ROUTE(app, "/calendar/me/<string>/feed.ics")([&](const crow::request&, const std::string& token) {
        crow::response res;
        int64_t member_id = token.size() == 64 ? member_repo.find_by_calendar_token_hash(sha256_hex(token)) : 0;
        if (member_id == 0) { res.code = 404; return res; }
        CalendarGenerator::Filter f;
        f.full_details = true;
        f.name_suffix = " (private)";
        res.write(cal.get_ics(f));
        res.add_header("Content-Type", "text/calendar; charset=utf-8");
        res.add_header("Cache-Control", "private, no-store");
        return res;
    });

    // POST /account/calendar-token - (re)generate the personal feed URL
    CROW_ROUTE(app, "/account/calendar-token").methods("POST"_method)([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        std::string token = SessionStore::generate_token();
        member_repo.set_calendar_token_hash(a.member_id, sha256_hex(token));
        std::string url = "/calendar/me/" + token + "/feed.ics";
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write("<div class=\"space-y-1\"><p class=\"text-xs text-gray-600\">Your private feed (includes private "
                  "meetings/events - don't share it). Copy it now; it won't be shown again. Generating a new one "
                  "disables the old link.</p><input readonly data-action=\"select-self\" "
                  "class=\"w-full text-xs font-mono border border-gray-300 rounded px-2 py-1\" "
                  "data-path=\"" + url + "\" id=\"private-cal-url\"></div>"
                  "<script>(function(){var i=document.getElementById('private-cal-url');"
                  "i.value=window.location.origin+i.dataset.path;})();</script>");
        return res;
    });

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
            ctx["member_chapter_id"]    = member_info->chapter_id;
            ctx["member_is_paid"]       = member_info->is_paid;
            ctx["member_paid_until"]    = member_info->paid_until;
            ctx["member_fol_status"]    = member_info->fol_status.empty() ? "afol" : member_info->fol_status;
            ctx["member_fol_label"]     = member_info->fol_status == "kfol" ? "KFOL"
                                        : member_info->fol_status == "tfol" ? "TFOL" : "AFOL";
            ctx["member_role_label"]    = Features::role_label(auth_ctx.auth.role);
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
                // The full-overview query is the heaviest thing on the dashboard;
                // admins reload it constantly, so cache the result for 5 minutes.
                struct Close { std::string name; std::string tier; int m; int e; int gap; };
                static std::mutex close_mutex;
                static std::chrono::steady_clock::time_point close_at;
                static int close_year = 0;
                static const void* close_src = nullptr; // per database (tests run several)
                static std::vector<Close> close_cached;
                std::vector<Close> close;
                {
                    std::lock_guard<std::mutex> lock(close_mutex);
                    if (close_year == year && close_src == &attendance_repo && std::chrono::steady_clock::now() - close_at < std::chrono::minutes(5)) {
                        close = close_cached;
                    } else {
                        AttendanceRepository::OverviewParams op;
                        op.year = year;
                        op.limit = 100000;
                        for (const auto& s : attendance_repo.get_overview_paginated(op)) {
                            int in_person = s.meeting_count - s.meeting_virtual_count;
                            auto mp = compute_perk_progress(levels, in_person, s.event_count, s.is_paid, s.fol_status);
                            if (mp.next.empty() || mp.needs_dues || !mp.needs_fol.empty()) continue;
                            if (mp.gap() < 1 || mp.gap() > 2) continue;
                            close.push_back({s.display_name, mp.next, mp.meetings_needed, mp.events_needed, mp.gap()});
                        }
                        std::stable_sort(close.begin(), close.end(),
                                         [](const Close& x, const Close& y) { return x.gap < y.gap; });
                        close_cached = close;
                        close_at = std::chrono::steady_clock::now();
                        close_year = year;
                        close_src = &attendance_repo;
                    }
                }
                if (close.size() > 10) close.resize(10);
                crow::json::wvalue arr;
                for (size_t i = 0; i < close.size(); ++i) {
                    arr[i]["name"]     = close[i].name;
                    arr[i]["tier"]     = close[i].tier;
                    arr[i]["meetings"] = close[i].m;
                    arr[i]["events"]   = close[i].e;
                    arr[i]["show_m"]   = close[i].m > 0;
                    arr[i]["show_e"]   = close[i].e > 0;
                    arr[i]["extra"]    = i >= 5;   // past the first five: folded away
                }
                ctx["close_more"] = close.size() > 5 ? static_cast<int>(close.size() - 5) : 0;
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
        if (auto* st = app.get_middleware<AuthMiddleware>().settings;
            st && auth_ctx.auth.is_admin() && st->get("setup_completed", "") != "1")
            ctx["show_setup_banner"] = true;
        if (auto* st = app.get_middleware<AuthMiddleware>().settings) {
            dashboard::add_coming_up(ctx, st->db(), auth_ctx.auth.member_id, st->get("lug_timezone", "America/Chicago"));
            if (auth_ctx.auth.is_admin()) dashboard::add_needs_attention(ctx, st->db());
        }
        // Sections of switched-off features (Settings > Features)
        Features::add_flags(ctx);
        if (!Features::on("chapters")) ctx["member_has_chapter"] = false;
        if (!Features::on("perks"))    { ctx["has_perks"] = false; ctx["has_close_to_tier"] = false; }
        if (!Features::on("dues"))     ctx["has_dues_expiring"] = false;

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
