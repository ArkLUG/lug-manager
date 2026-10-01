#include "routes/SeriesRoutes.hpp"
#include "services/AttendanceService.hpp"
#include "utils/LocalTime.hpp"
#include "utils/ParseId.hpp"
#include <crow/mustache.h>
#include <regex>

namespace {

bool can_manage_series(const AuthContext& a, ChapterMemberRepository& cm, const std::string& scope, int64_t chapter_id) {
    if (a.is_admin()) return true;
    if (scope != "chapter" || chapter_id <= 0) return false; // LUG-wide/non-LUG series: admins
    auto r = cm.get_chapter_role(a.member_id, chapter_id);
    return r && chapter_role_rank(*r) >= chapter_role_rank("event_manager");
}

std::string render(const crow::request& req, LugApp& app, SeriesService& series, ChapterService& chapters,
                   ChapterMemberRepository& cm, const std::string& flash) {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    std::unordered_map<int64_t, std::string> names;
    for (const auto& ch : chapters.list_all()) names[ch.id] = ch.name;
    crow::mustache::context ctx;
    crow::json::wvalue arr = crow::json::wvalue::list();
    auto list = series.list();
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& s = list[i];
        arr[i]["id"]       = s.id;
        arr[i]["title"]    = s.title;
        arr[i]["when"]     = SeriesService::describe(s);
        arr[i]["where"]    = s.location;
        arr[i]["chapter"]  = s.chapter_id > 0 && names.count(s.chapter_id) ? names[s.chapter_id]
                           : (s.scope == "lug_wide" ? "LUG-wide" : s.scope == "non_lug" ? "Non-LUG" : "");
        arr[i]["active"]   = s.active;
        arr[i]["range"]    = s.starts_on + (s.ends_on.empty() ? " onward" : " to " + s.ends_on);
        arr[i]["can_stop"] = s.active && can_manage_series(a, cm, s.scope, s.chapter_id);
    }
    ctx["series"] = std::move(arr);
    ctx["has_series"] = !list.empty();
    ctx["flash"] = flash;
    ctx["can_create"] = a.is_chapter_lead() || !cm.find_by_member(a.member_id).empty();
    ctx["is_admin"] = a.is_admin();
    ctx["today"] = AttendanceService::today_ymd();
    Features::add_flags(ctx);
    return crow::mustache::load("meetings/_series.html").render(ctx).dump();
}

} // namespace

void register_series_routes(LugApp& app, std::shared_ptr<SeriesService> series,
                            ChapterService& chapters, ChapterMemberRepository& chapter_members,
                            AuditService& audit) {

    CROW_ROUTE(app, "/meetings/series")([&app, series, &chapters, &chapter_members](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        std::string page = render(req, app, *series, chapters, chapter_members, "");
        return html_page(req, app, page, "Recurring Meetings", "active_meetings");
    });

    CROW_ROUTE(app, "/meetings/series").methods("POST"_method)(
        [&app, series, &chapters, &chapter_members, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = params.get(k); return v ? std::string(v) : ""; };
        auto fail = [&](int code, const std::string& msg) {
            res.code = code;
            res.write(render(req, app, *series, chapters, chapter_members, msg));
            return std::move(res);
        };

        MeetingSeries s;
        s.title = gp("title").substr(0, 200);
        s.description = gp("description").substr(0, 4000);
        s.location = gp("location").substr(0, 300);
        s.scope = Features::normalize_scope(gp("scope"));
        if (s.scope != "chapter" && s.scope != "lug_wide" && s.scope != "non_lug") s.scope = "chapter";
        s.chapter_id = s.scope == "chapter" ? parse_id(gp("chapter_id")) : 0;
        s.rule = gp("rule") == "weekly" ? "weekly" : "monthly";
        try { s.weekday = std::stoi(gp("weekday")); } catch (...) { s.weekday = -1; }
        try { s.nth = std::stoi(gp("nth")); } catch (...) { s.nth = 1; }
        try { s.interval_weeks = std::stoi(gp("interval_weeks")); } catch (...) { s.interval_weeks = 1; }
        s.start_hm = gp("start_hm");
        s.end_hm = gp("end_hm");
        s.starts_on = gp("starts_on").empty() ? AttendanceService::today_ymd() : gp("starts_on");
        s.ends_on = gp("ends_on");
        s.is_virtual = gp("is_virtual") == "1";
        s.suppress_discord = gp("suppress_discord") == "1";
        s.suppress_calendar = gp("suppress_calendar") == "1";
        s.is_private = gp("is_private") == "1";
        s.excludes_perks = gp("excludes_perks") == "1";
        s.created_by = app.get_context<AuthMiddleware>(req).auth.member_id;

        static const std::regex hm(R"(([01]\d|2[0-3]):[0-5]\d)"), ymd(R"(\d{4}-\d{2}-\d{2})");
        if (s.title.empty()) return fail(400, "Give the series a title.");
        if (s.weekday < 0 || s.weekday > 6) return fail(400, "Pick a day of the week.");
        if (s.rule == "monthly" && !(s.nth == -1 || (s.nth >= 1 && s.nth <= 4))) return fail(400, "Pick which week of the month.");
        if (s.interval_weeks < 1 || s.interval_weeks > 8) return fail(400, "Repeat every 1-8 weeks.");
        if (!std::regex_match(s.start_hm, hm) || !std::regex_match(s.end_hm, hm) || s.end_hm <= s.start_hm)
            return fail(400, "Enter a start and end time (end after start).");
        if (!std::regex_match(s.starts_on, ymd) || (!s.ends_on.empty() && (!std::regex_match(s.ends_on, ymd) || s.ends_on < s.starts_on)))
            return fail(400, "Check the start/end dates.");
        if (s.scope == "chapter" && s.chapter_id <= 0) return fail(400, "Pick a chapter (or make it LUG-wide).");
        if (!can_manage_series(app.get_context<AuthMiddleware>(req).auth, chapter_members, s.scope, s.chapter_id))
            return fail(403, "You can only create series for chapters you manage (LUG-wide series need an admin).");

        int64_t id = series->create(s);
        s.id = id;
        int made = series->materialize(AttendanceService::today_ymd());
        audit.log(req, app, "meeting.series_create", "meeting_series", id, s.title,
                  SeriesService::describe(s) + "; created " + std::to_string(made) + " meeting(s)");
        res.write(render(req, app, *series, chapters, chapter_members,
                         "Series created - " + std::to_string(made) + " upcoming meeting(s) scheduled."));
        return res;
    });

    CROW_ROUTE(app, "/meetings/series/<int>/stop").methods("POST"_method)(
        [&app, series, &chapters, &chapter_members, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto s = series->get(id);
        if (!s) { res.code = 404; return res; }
        if (!can_manage_series(app.get_context<AuthMiddleware>(req).auth, chapter_members, s->scope, s->chapter_id)) {
            res.code = 403;
            return res;
        }
        int removed = series->stop(id, local_iso_now());
        audit.log(req, app, "meeting.series_stop", "meeting_series", id, s->title,
                  "Stopped; removed " + std::to_string(removed) + " future meeting(s)");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, *series, chapters, chapter_members,
                         "Series stopped - " + std::to_string(removed) + " future meeting(s) removed."));
        return res;
    });
}
