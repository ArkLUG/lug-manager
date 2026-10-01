#include "routes/ShowsRoutes.hpp"
#include "services/AttendanceService.hpp"
#include "utils/AssetVersion.hpp"
#include "utils/MarkdownRenderer.hpp"
#include "utils/Utf8.hpp"
#include <crow/mustache.h>
#include <ctime>

namespace {

struct Show {
    int64_t id = 0;
    std::string title, description, location, start, end, status, fee;
};

std::vector<Show> upcoming(SqliteDatabase& db) {
    std::vector<Show> out;
    auto st = db.prepare(
        "SELECT id, title, description, location, start_time, end_time, status, entrance_fee FROM lug_events "
        "WHERE is_private=0 AND status <> 'cancelled' AND substr(COALESCE(NULLIF(end_time,''), start_time),1,10) >= ? "
        "ORDER BY start_time LIMIT 50");
    st.bind(1, AttendanceService::today_ymd());
    while (st.step())
        out.push_back({st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_text(4),
                       st.col_text(5), st.col_text(6), st.col_text(7)});
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

bool enabled(SettingsRepository& settings) { return settings.get("public_shows_enabled", "") == "1"; }

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
            if (!s.location.empty()) o["map_q"] = s.location;
        }
        ctx["shows"] = std::move(arr);
        ctx["has_shows"] = i > 0;
        ctx["title"] = settings.get("public_shows_title", "Upcoming shows");
        std::string intro = settings.get("public_shows_intro", "");
        if (!intro.empty()) ctx["intro"] = intro;
        ctx["has_logo"] = !settings.get("branding_logo_extension", "").empty();
        ctx["logo_v"] = settings.get("branding_logo_updated_at", "0");
        ctx["embed"] = embed;
        ctx["asset_v"] = asset_version();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("Cache-Control", "public, max-age=300");
        if (embed) {
            // Meant to be iframed on the LUG's own website.
            res.add_header("X-Frame-Options", "ALLOWALL");
            res.add_header("Content-Security-Policy",
                "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                "object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors *");
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
        }
        res.add_header("Content-Type", "application/json");
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Cache-Control", "public, max-age=300");
        res.write(out.dump());
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
        settings.set("public_shows_enabled", on ? "1" : "0");
        settings.set("public_shows_title", title.empty() ? "Upcoming shows" : title);
        settings.set("public_shows_intro", gp("intro", 1000));
        audit.log(req, app, "settings.public_shows", "settings", 0, "Public shows page", on ? "Enabled" : "Disabled");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(settings_card(settings, "Saved."));
        return res;
    });
}
