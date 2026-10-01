#include "routes/events/ShowsRoutes.hpp"
#include "utils/web/ClientIp.hpp"
#include "services/FanCoLab.hpp"
#include "services/events/AttendanceService.hpp"
#include "utils/web/AssetVersion.hpp"
#include "utils/text/MarkdownRenderer.hpp"
#include "utils/text/Utf8.hpp"
#include <crow/mustache.h>
#include <algorithm>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace {

struct Show {
    int64_t id = 0;
    std::string title, description, location, start, end, status, fee;
    int64_t interest = 0;
    bool has_shifts = false;
};

std::vector<Show> upcoming(SqliteDatabase& db) {
    std::vector<Show> out;
    auto st = db.prepare(
        "SELECT id, title, description, location, start_time, end_time, status, entrance_fee, public_interest, "
        "EXISTS(SELECT 1 FROM event_shifts s WHERE s.event_id = lug_events.id) FROM lug_events "
        "WHERE is_private=0 AND status <> 'cancelled' AND substr(COALESCE(NULLIF(end_time,''), start_time),1,10) >= ? "
        "ORDER BY start_time LIMIT 50");
    st.bind(1, AttendanceService::today_ymd());
    while (st.step())
        out.push_back({st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_text(4),
                       st.col_text(5), st.col_text(6), st.col_text(7), st.col_int(8), st.col_int(9) != 0});
    return out;
}

bool parse(const std::string& iso, std::tm& tm) {
    tm = {};
    return iso.size() >= 10 && strptime(iso.substr(0, 16).c_str(), iso.size() >= 16 ? "%Y-%m-%dT%H:%M" : "%Y-%m-%d", &tm);
}

std::string fmt(const std::tm& tm, const char* f) {
    char b[64];
    std::strftime(b, sizeof(b), f, &tm);
    std::string s = b;
    // Drop leading zeros from day/hour ("June 06" -> "June 6", "07:00" -> "7:00").
    for (size_t p; (p = s.find(" 0")) != std::string::npos;) s.erase(p + 1, 1);
    if (!s.empty() && s[0] == '0') s.erase(0, 1);
    return s;
}

// "Saturday, June 6, 2026 · 10:00 AM - 4:00 PM" or "Sat, Jun 6 - Sun, Jun 7, 2026"
std::string when(const Show& s) {
    std::tm a{}, b{};
    if (!parse(s.start, a)) return s.start;
    bool has_end = parse(s.end, b);
    bool same_day = !has_end || s.end.substr(0, 10) == s.start.substr(0, 10);
    bool timed = s.start.size() >= 16;
    if (same_day) {
        std::string out = fmt(a, "%A, %B %d, %Y");
        if (timed) out += " · " + fmt(a, "%I:%M %p") + (has_end && s.end.size() >= 16 ? " - " + fmt(b, "%I:%M %p") : "");
        return out;
    }
    return fmt(a, "%a, %b %d") + " - " + fmt(b, "%a, %b %d, %Y");
}

// Events this browser said it's coming to (cookie "si": comma-separated ids).
std::set<int64_t> interested(const crow::request& req) {
    std::set<int64_t> out;
    std::stringstream ss(get_cookie(req, "si"));
    std::string tok;
    while (std::getline(ss, tok, ',')) { try { out.insert(std::stoll(tok)); } catch (...) {} }
    return out;
}

// Per-IP cap on interest clicks, so the public count can't be pumped.
bool rate_ok(const std::string& ip) {
    static std::mutex mu;
    static std::map<std::string, std::deque<std::time_t>> hits;
    std::lock_guard<std::mutex> l(mu);
    std::time_t now = std::time(nullptr);
    auto& q = hits[ip];
    while (!q.empty() && now - q.front() > 3600) q.pop_front();
    if (q.size() >= 30) return false;
    q.push_back(now);
    if (hits.size() > 10000) hits.clear();
    return true;
}

bool enabled(SettingsRepository&) { return Features::on("public_shows"); }

std::string settings_card(SettingsRepository& settings, const std::string& flash = "") {
    crow::mustache::context ctx;
    ctx["enabled"] = enabled(settings);
    ctx["title"] = settings.get("public_shows_title", "Upcoming shows");
    ctx["intro"] = settings.get("public_shows_intro", "");
    if (!flash.empty()) ctx["flash"] = flash;
    return crow::mustache::load("shows/_settings.html").render(ctx).dump();
}

} // namespace

void register_shows_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuditService& audit) {

    // GET /shows[?embed=1] - public, no login
    CROW_ROUTE(app, "/shows")([&db, &settings](const crow::request& req) {
        crow::response res;
        if (!enabled(settings)) { res.code = 404; return res; }
        bool embed = req.url_params.get("embed") != nullptr;
        auto mine = interested(req);
        crow::mustache::context ctx;
        crow::json::wvalue arr = crow::json::wvalue::list();
        int i = 0;
        for (const auto& s : upcoming(db)) {
            auto& o = arr[i++];
            o["title"] = s.title;
            o["when"] = when(s);
            if (!s.location.empty()) o["location"] = s.location;
            if (!s.fee.empty()) o["fee"] = s.fee;
            o["tentative"] = s.status == "tentative";
            if (!s.description.empty()) o["description_html"] = render_markdown(utf8_truncate(s.description, 1500));
            o["id"] = s.id;
            o["interest"] = s.interest;
            o["has_interest"] = s.interest > 0;
            o["mine"] = mine.count(s.id) > 0;
            o["has_shifts"] = s.has_shifts && Features::on("shifts");
            o["embed"] = embed;
        }
        ctx["shows"] = std::move(arr);
        ctx["has_shows"] = i > 0;
        ctx["title"] = settings.get("public_shows_title", "Upcoming shows");
        std::string intro = settings.get("public_shows_intro", "");
        if (!intro.empty()) ctx["intro"] = intro;
        ctx["has_logo"] = !settings.get("branding_logo_extension", "").empty();
        ctx["logo_v"] = settings.get("branding_logo_updated_at", "0");
        ctx["embed"] = embed;
        ctx["about_on"] = Features::on("about_page");
        add_fan_colab(ctx, db);
        ctx["asset_v"] = asset_version();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("Cache-Control", "private, no-cache");   // per-browser "you're coming" state
        if (embed) {
            // Meant to be iframed on the LUG's own website.
            res.add_header("X-Frame-Options", "ALLOWALL");
            res.add_header("Content-Security-Policy",
                "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                "object-src 'none'; base-uri 'none'; form-action 'self'; frame-ancestors *");
        }
        res.write(crow::mustache::load("shows/public.html").render(ctx).dump());
        return res;
    });

    // GET /shows.json - same list for the LUG website to render itself
    CROW_ROUTE(app, "/shows.json")([&db, &settings](const crow::request&) {
        crow::response res;
        if (!enabled(settings)) { res.code = 404; return res; }
        crow::json::wvalue out = crow::json::wvalue::list();
        int i = 0;
        for (const auto& s : upcoming(db)) {
            auto& o = out[i++];
            o["title"] = s.title;
            o["start"] = s.start;
            o["end"] = s.end;
            o["when"] = when(s);
            o["location"] = s.location;
            o["entrance_fee"] = s.fee;
            o["tentative"] = s.status == "tentative";
            o["description"] = utf8_truncate(s.description, 1500);
            o["planning_to_come"] = s.interest;
        }
        res.add_header("Content-Type", "application/json");
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Cache-Control", "public, max-age=300");
        res.write(out.dump());
        return res;
    });

    // POST /shows/<id>/interest - "I plan to come" toggle (no login)
    CROW_ROUTE(app, "/shows/<int>/interest").methods("POST"_method)([&db, &settings](const crow::request& req, int id) {
        crow::response res;
        if (!enabled(settings)) { res.code = 404; return res; }
        bool listed = false;
        for (const auto& s : upcoming(db)) listed |= s.id == id;
        if (!listed) { res.code = 404; return res; }
        bool embed = req.url_params.get("embed") != nullptr;
        res.code = 303;
        res.set_header("Location", std::string(embed ? "/shows?embed=1" : "/shows") + "#show-" + std::to_string(id));
        const std::string ip = client_ip(req);
        auto mine = interested(req);
        bool undo = mine.count(id) > 0;
        if (!rate_ok(ip)) return res;
        {
            auto up = db.prepare(undo ? "UPDATE lug_events SET public_interest=MAX(public_interest-1,0) WHERE id=?"
                                      : "UPDATE lug_events SET public_interest=public_interest+1 WHERE id=?");
            up.bind(1, static_cast<int64_t>(id));
            up.step();
        }
        if (undo) mine.erase(id); else mine.insert(id);
        std::string v;
        for (auto e : mine) v += (v.empty() ? "" : ",") + std::to_string(e);
        res.add_header("Set-Cookie", "si=" + v + "; Path=/shows; Max-Age=31536000; SameSite=Lax");
        return res;
    });

    // GET /events/<id>/public-interest - count for the event page (members)
    CROW_ROUTE(app, "/events/<int>/public-interest")([&app, &db](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto st = db.prepare("SELECT public_interest FROM lug_events WHERE id=?");
        st.bind(1, static_cast<int64_t>(id));
        int64_t n = st.step() ? st.col_int(0) : 0;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (n > 0) res.write("<span class=\"text-xs text-gray-500\">" + std::to_string(n) +
                             " visitor(s) said they plan to come (public shows page)</span>");
        return res;
    });

    // Settings card (admin)
    CROW_ROUTE(app, "/settings/public-shows")([&app, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(settings_card(settings));
        return res;
    });

    CROW_ROUTE(app, "/settings/public-shows").methods("POST"_method)([&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto p = crow::query_string("?" + req.body);
        auto gp = [&](const char* k, size_t max) { const char* v = p.get(k); return v ? std::string(v).substr(0, max) : std::string(); };
        bool on = gp("enabled", 2) == "1";
        std::string title = gp("title", 100);
        Features::set("public_shows", on);
        settings.set("public_shows_title", title.empty() ? "Upcoming shows" : title);
        settings.set("public_shows_intro", gp("intro", 1000));
        audit.log(req, app, "settings.public_shows", "settings", 0, "Public shows page", on ? "Enabled" : "Disabled");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(settings_card(settings, "Saved."));
        return res;
    });
}
