#include "routes/pages/ScheduleRoutes.hpp"
#include "services/Features.hpp"
#include "utils/LocalTime.hpp"
#include "utils/text/UrlEncode.hpp"
#include "utils/web/AssetVersion.hpp"
#include "utils/web/CalendarLinks.hpp"
#include <crow/mustache.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <set>
#include <vector>

namespace {

struct Item {
    std::string kind;          // "meeting" | "event"
    int64_t id = 0;
    std::string title, start, end, location, scope, status, chapter;
    int64_t chapter_id = 0;
};

struct Filters {
    std::string view = "list", type = "all", when = "upcoming", scope = "all", status = "active", q, month;
    bool mine = false;
};

std::string param(const crow::query_string& qs, const char* k, size_t max = 80) {
    const char* v = qs.get(k);
    return v ? std::string(v).substr(0, max) : "";
}

Filters read_filters(const crow::request& req) {
    auto qs = crow::query_string(req.url_params);
    Filters f;
    auto pick = [&](const char* k, std::string& out, std::initializer_list<const char*> ok) {
        std::string v = param(qs, k);
        for (const char* o : ok) if (v == o) { out = v; return; }
    };
    pick("view", f.view, {"list", "calendar"});
    pick("type", f.type, {"all", "meetings", "events"});
    pick("when", f.when, {"upcoming", "past", "all"});
    pick("status", f.status, {"active", "tentative", "cancelled", "all"});
    std::string scope = param(qs, "scope");
    if (scope == "all" || scope == "group" || scope == "external" || scope == "chapters") f.scope = scope;
    else if (scope.rfind("chapter:", 0) == 0 && scope.size() > 8 &&
             scope.find_first_not_of("0123456789", 8) == std::string::npos) f.scope = scope;
    if (!Features::on("chapters") && (f.scope == "chapters" || f.scope.rfind("chapter:", 0) == 0)) f.scope = "all";
    f.q = param(qs, "q", 100);
    f.mine = param(qs, "mine") == "1";
    std::string m = param(qs, "month", 7);
    if (m.size() == 7 && m[4] == '-' && std::isdigit(static_cast<unsigned char>(m[0])) && std::isdigit(static_cast<unsigned char>(m[5])))
        f.month = m;
    return f;
}

// The query string for these filters, with one change (for links).
std::string query(const Filters& f, const std::string& key = "", const std::string& value = "") {
    std::vector<std::pair<std::string, std::string>> kv = {
        {"view", f.view}, {"type", f.type}, {"when", f.when}, {"scope", f.scope}, {"status", f.status},
        {"q", f.q}, {"mine", f.mine ? "1" : ""}, {"month", f.month}};
    std::string out;
    for (auto& [k, v] : kv) {
        std::string val = k == key ? value : v;
        if (val.empty()) continue;
        out += (out.empty() ? "?" : "&") + k + "=" + url_encode_component(val);
    }
    return out;
}

std::string scope_label(const Item& it, bool chapters_on) {
    if (it.scope == "lug_wide") return "Group-wide";
    if (it.scope == "non_lug") return "External";
    return chapters_on && !it.chapter.empty() ? it.chapter : "Chapter";
}

std::string hm(const std::string& iso) {   // "19:00" -> "7:00 PM"
    if (iso.size() < 16) return "";
    int h = std::atoi(iso.substr(11, 2).c_str()), m = std::atoi(iso.substr(14, 2).c_str());
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h >= 12 ? "PM" : "AM");
    return b;
}

std::string when_text(const Item& it) {
    const std::string sd = it.start.substr(0, 10), ed = it.end.size() >= 10 ? it.end.substr(0, 10) : sd;
    const bool timed = it.start.size() >= 16 && it.start.substr(11, 5) != "00:00";
    std::string out = friendly_date(sd);
    if (ed != sd) return out + " – " + friendly_date(ed);
    if (timed) {
        out += ", " + hm(it.start);
        if (it.end.size() >= 16 && it.end != it.start) out += " – " + hm(it.end);
    }
    return out;
}

std::vector<Item> load(SqliteDatabase& db, const Filters& f, int64_t me, const std::string& from, const std::string& to) {
    std::vector<Item> items;
    const std::string now = local_iso_now();
    for (const char* kind : {"meeting", "event"}) {
        const bool meeting = std::string(kind) == "meeting";
        if ((meeting && f.type == "events") || (!meeting && f.type == "meetings")) continue;
        const std::string t = meeting ? "meetings" : "lug_events";
        std::string sql = "SELECT x.id, x.title, x.start_time, COALESCE(x.end_time,''), COALESCE(x.location,''), x.scope, x.status, "
                          "COALESCE(x.chapter_id,0), COALESCE(c.name,'') FROM " + t + " x LEFT JOIN chapters c ON c.id = x.chapter_id WHERE 1=1";
        std::vector<std::string> args;
        auto end_expr = std::string("COALESCE(NULLIF(x.end_time,''), x.start_time)");
        if (!from.empty()) { sql += " AND " + end_expr + " >= ? AND x.start_time < ?"; args.push_back(from); args.push_back(to); }
        else if (f.when == "upcoming") { sql += " AND " + end_expr + " >= ?"; args.push_back(now.substr(0, 10)); }
        else if (f.when == "past") { sql += " AND " + end_expr + " < ?"; args.push_back(now.substr(0, 10)); }
        if (f.status == "active") sql += " AND x.status <> 'cancelled'";
        else if (f.status == "cancelled") sql += " AND x.status = 'cancelled'";
        else if (f.status == "tentative") sql += " AND x.status = 'tentative'";
        if (f.scope == "group") sql += " AND x.scope = 'lug_wide'";
        else if (f.scope == "external") sql += " AND x.scope = 'non_lug'";
        else if (f.scope == "chapters") sql += " AND x.scope = 'chapter'";
        else if (f.scope.rfind("chapter:", 0) == 0) { sql += " AND x.scope = 'chapter' AND x.chapter_id = ?"; args.push_back(f.scope.substr(8)); }
        if (!f.q.empty()) {
            sql += " AND (x.title LIKE ? OR x.location LIKE ? OR x.description LIKE ?)";
            for (int i = 0; i < 3; ++i) args.push_back("%" + f.q + "%");
        }
        if (f.mine) {
            const std::string m = std::to_string(me);
            if (meeting)   // meetings have no RSVPs: your chapters' and the group-wide ones
                sql += " AND (x.scope <> 'chapter' OR x.chapter_id IN (SELECT chapter_id FROM chapter_members WHERE member_id=" + m + "))";
            else
                sql += " AND (x.event_lead_id = " + m +
                       " OR x.id IN (SELECT event_id FROM event_rsvps WHERE member_id=" + m + " AND status IN ('going','waitlist'))"
                       " OR x.id IN (SELECT s.event_id FROM event_shifts s JOIN event_shift_signups u ON u.shift_id = s.id WHERE u.member_id=" + m + "))";
        }
        sql += f.when == "past" && from.empty() ? " ORDER BY x.start_time DESC" : " ORDER BY x.start_time";
        sql += " LIMIT 400";
        auto st = db.prepare(sql);
        for (size_t i = 0; i < args.size(); ++i) st.bind(static_cast<int>(i + 1), args[i]);
        while (st.step()) {
            Item it;
            it.kind = kind; it.id = st.col_int(0); it.title = st.col_text(1); it.start = st.col_text(2); it.end = st.col_text(3);
            it.location = st.col_text(4); it.scope = st.col_text(5); it.status = st.col_text(6);
            it.chapter_id = st.col_int(7); it.chapter = st.col_text(8);
            items.push_back(std::move(it));
        }
    }
    const bool desc = f.when == "past" && from.empty();
    std::stable_sort(items.begin(), items.end(), [desc](const Item& a, const Item& b) {
        return desc ? a.start > b.start : a.start < b.start;
    });
    return items;
}

crow::json::wvalue item_json(const Item& it, bool chapters_on) {
    crow::json::wvalue o;
    o["kind"] = it.kind;
    o["is_meeting"] = it.kind == "meeting";
    o["id"] = it.id;
    o["title"] = it.title;
    o["url"] = (it.kind == "meeting" ? "/meetings/" : "/events/") + std::to_string(it.id);
    o["when"] = when_text(it);
    o["time"] = it.start.size() >= 16 && it.start.substr(11, 5) != "00:00" ? hm(it.start) : "";
    o["location"] = it.location;
    o["scope_label"] = scope_label(it, chapters_on);
    o["scope_group"] = it.scope == "lug_wide";
    o["scope_external"] = it.scope == "non_lug";
    o["tentative"] = it.status == "tentative";
    o["cancelled"] = it.status == "cancelled";
    return o;
}

int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : d[m - 1];
}

} // namespace

void register_schedule_routes(LugApp& app, SqliteDatabase& db, ChapterMemberRepository& chapter_members) {
    CROW_ROUTE(app, "/schedule")([&app, &db, &chapter_members](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        Filters f = read_filters(req);
        const bool chapters_on = Features::on("chapters");

        crow::mustache::context ctx;
        ctx["asset_v"] = asset_version();
        ctx["is_list"] = f.view == "list";
        ctx["is_calendar"] = f.view == "calendar";
        ctx["list_url"] = "/schedule" + query(f, "view", "list");
        ctx["calendar_url"] = "/schedule" + query(f, "view", "calendar");
        for (const char* t : {"all", "meetings", "events"}) ctx[std::string("type_") + t] = f.type == t;
        for (const char* w : {"upcoming", "past", "all"}) ctx[std::string("when_") + w] = f.when == w;
        for (const char* s : {"active", "tentative", "cancelled", "all"}) ctx[std::string("status_") + s] = f.status == s;
        ctx["scope_all"] = f.scope == "all";
        ctx["scope_group"] = f.scope == "group";
        ctx["scope_external"] = f.scope == "external";
        ctx["scope_chapters"] = f.scope == "chapters";
        ctx["chapters_on"] = chapters_on;
        ctx["q"] = f.q;
        ctx["mine"] = f.mine;
        ctx["month"] = f.month;
        ctx["view"] = f.view;
        if (chapters_on) {
            crow::json::wvalue chs = crow::json::wvalue::list();
            auto st = db.prepare("SELECT id, name FROM chapters ORDER BY name");
            int i = 0;
            while (st.step()) {
                chs[i]["value"] = "chapter:" + std::to_string(st.col_int(0));
                chs[i]["name"] = st.col_text(1);
                chs[i]["selected"] = f.scope == "chapter:" + std::to_string(st.col_int(0));
                ++i;
            }
            ctx["chapters"] = std::move(chs);
        }
        // Subscribe: the chapter's feed when one chapter is picked, else everything
        {
            std::string lug_name = "LUG";
            auto st = db.prepare("SELECT value FROM lug_settings WHERE key = 'lug_name'");
            if (st.step() && !st.col_text(0).empty()) lug_name = st.col_text(0);
            std::string feed = "/calendar.ics", label = lug_name;
            if (f.scope.rfind("chapter:", 0) == 0) {
                feed = "/calendar/chapter/" + f.scope.substr(8) + "/feed.ics";
                ctx["subscribe_chapter"] = true;
            }
            ctx["f_calendar"] = Features::on("calendar");
            ctx["subscribe_html"] = cal_links::subscribe_html(feed, label, "sched-cal");
        }
        // Who can add meetings/events: admins and chapter event managers/leads
        bool can_create = auth.is_admin();
        if (!can_create && auth.member_id > 0)
            for (const auto& cm : chapter_members.find_by_member(auth.member_id))
                if (chapter_role_rank(cm.chapter_role) >= chapter_role_rank("event_manager")) { can_create = true; break; }
        ctx["can_create"] = can_create;

        if (f.view == "list") {
            auto items = load(db, f, auth.member_id, "", "");
            crow::json::wvalue groups = crow::json::wvalue::list();
            int gi = -1, ii = 0;
            std::string current;
            static const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                           "August", "September", "October", "November", "December"};
            for (const auto& it : items) {
                std::string ym = it.start.substr(0, 7);
                if (ym != current) {
                    current = ym; ++gi; ii = 0;
                    int mo = std::atoi(ym.substr(5, 2).c_str());
                    groups[gi]["heading"] = std::string(mo >= 1 && mo <= 12 ? months[mo - 1] : "") + " " + ym.substr(0, 4);
                    groups[gi]["items"] = crow::json::wvalue::list();
                }
                groups[gi]["items"][ii++] = item_json(it, chapters_on);
            }
            ctx["groups"] = std::move(groups);
            ctx["has_items"] = !items.empty();
            ctx["count"] = static_cast<int>(items.size());
            ctx["capped"] = items.size() >= 400;
        } else {
            std::tm now = local_tm(std::time(nullptr));
            int y = now.tm_year + 1900, m = now.tm_mon + 1;
            if (!f.month.empty()) { y = std::atoi(f.month.substr(0, 4).c_str()); m = std::atoi(f.month.substr(5, 2).c_str()); }
            if (m < 1 || m > 12 || y < 1970 || y > 2200) { y = now.tm_year + 1900; m = now.tm_mon + 1; }
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-01", y, m);
            const std::string first = buf;
            int ny = m == 12 ? y + 1 : y, nm = m == 12 ? 1 : m + 1, py = m == 1 ? y - 1 : y, pm = m == 1 ? 12 : m - 1;
            std::snprintf(buf, sizeof(buf), "%04d-%02d-01", ny, nm);
            const std::string next_first = buf;
            auto items = load(db, f, auth.member_id, first, next_first);
            static const char* months[] = {"January", "February", "March", "April", "May", "June", "July",
                                           "August", "September", "October", "November", "December"};
            ctx["month_title"] = std::string(months[m - 1]) + " " + std::to_string(y);
            std::snprintf(buf, sizeof(buf), "%04d-%02d", py, pm);
            ctx["prev_url"] = "/schedule" + query(f, "month", buf);
            std::snprintf(buf, sizeof(buf), "%04d-%02d", ny, nm);
            ctx["next_url"] = "/schedule" + query(f, "month", buf);
            ctx["today_url"] = "/schedule" + query(f, "month", "");
            // Weekday of the 1st (0 = Sunday)
            std::tm t{}; t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = 1; t.tm_hour = 12;
            std::time_t tt = timegm(&t);
            std::tm g{}; gmtime_r(&tt, &g);
            const int lead = g.tm_wday, days = days_in_month(y, m);
            const std::string today = local_iso_now().substr(0, 10);
            crow::json::wvalue weeks = crow::json::wvalue::list();
            int cell = 0, week = -1;
            const int cells = ((lead + days + 6) / 7) * 7;
            for (int c = 0; c < cells; ++c, ++cell) {
                if (c % 7 == 0) { ++week; weeks[week]["days"] = crow::json::wvalue::list(); }
                auto& d = weeks[week]["days"][c % 7];
                int day = c - lead + 1;
                if (day < 1 || day > days) { d["blank"] = true; continue; }
                std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, day);
                const std::string date = buf;
                d["day"] = day;
                d["today"] = date == today;
                crow::json::wvalue list = crow::json::wvalue::list();
                int n = 0;
                for (const auto& it : items) {
                    std::string s = it.start.substr(0, 10), e = it.end.size() >= 10 ? it.end.substr(0, 10) : s;
                    if (date < s || date > e) continue;
                    auto j = item_json(it, chapters_on);
                    j["continues"] = date != s;
                    list[n++] = std::move(j);
                }
                d["items"] = std::move(list);
                d["has_items"] = n > 0;
            }
            ctx["weeks"] = std::move(weeks);
        }
        std::string body = crow::mustache::load("schedule/_content.html").render(ctx).dump();
        return html_page(req, app, body, "Schedule", "active_schedule");
    });
}
