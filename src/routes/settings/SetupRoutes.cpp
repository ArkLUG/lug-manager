#include "routes/settings/SetupRoutes.hpp"
#include "utils/LocalTime.hpp"
#include "services/Features.hpp"
#include "services/FanCoLab.hpp"
#include "services/events/AttendanceService.hpp"
#include "auth/AccountSecurity.hpp"
#include "auth/SessionStore.hpp"
#include "utils/text/HtmlEscape.hpp"
#include <crow/mustache.h>
#include <iostream>
#include <mutex>

namespace {

std::mutex g_mu;
std::string g_token;   // one-time first-admin token; "" when not needed

bool has_admin(SqliteDatabase& db) {
    auto st = db.prepare("SELECT 1 FROM members WHERE role='admin' LIMIT 1");
    return st.step();
}

bool token_ok(const std::string& t) {
    std::lock_guard<std::mutex> l(g_mu);
    return !g_token.empty() && !t.empty() && t == g_token;
}

crow::response page(const crow::request& req, LugApp& app, const std::string& body, bool standalone, int code = 200) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.add_header("Cache-Control", "no-store");
    if (standalone || is_htmx(req)) res.write(body);
    else res.write(render_in_layout(req, app, body, "Setup", "active_setup"));
    return res;
}

std::string render_first_admin(const std::string& token, const std::string& error = "") {
    crow::mustache::context ctx;
    ctx["token"] = token;
    ctx["asset_v"] = asset_version();
    if (!error.empty()) ctx["error"] = error;
    return crow::mustache::load("setup/first_admin.html").render(ctx).dump();
}

std::string render_checklist(SettingsRepository& settings, SqliteDatabase& db, const std::string& flash = "") {
    crow::mustache::context ctx;
    std::string name = settings.get("lug_name", "");
    std::string tz = settings.get("lug_timezone", "America/Chicago");
    bool basics = !name.empty();
    bool features = settings.get("setup_features_done", "") == "1";
    bool discord = !settings.get("discord_guild_id", "").empty();
    int mapped = 0;
    {
        auto st = db.prepare("SELECT COUNT(*) FROM discord_role_mappings");
        if (st.step()) mapped = static_cast<int>(st.col_int(0));
    }
    bool roles = mapped > 0;
    // Without Discord: step 3 is just "you're not using it", step 4 is having
    // members with email addresses who can be sent set-password links.
    bool discord_on = Features::on("discord");
    ctx["discord_on"] = discord_on;
    if (!discord_on) {
        discord = true;
        auto st = db.prepare("SELECT COUNT(*) FROM members WHERE COALESCE(email,'') <> ''");
        int with_email = st.step() ? static_cast<int>(st.col_int(0)) : 0;
        roles = with_email >= 2;
        ctx["members_with_email"] = with_email;
    }
    ctx["lug_name"] = name;
    ctx["timezone"] = tz;
    ctx["basics_done"] = basics;
    ctx["features_done"] = features;
    ctx["discord_done"] = discord;
    ctx["roles_done"] = roles;
    ctx["mapped_roles"] = mapped;
    ctx["guild_id"] = settings.get("discord_guild_id", "");
    ctx["lug_channel"] = settings.get("discord_announcements_channel_id", "");
    ctx["done_count"] = static_cast<int>(basics) + features + discord + roles;
    ctx["completed"] = settings.get("setup_completed", "") == "1";
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    for (const auto& f : Features::all()) {
        arr[i]["key"] = f.key; arr[i]["label"] = f.label; arr[i]["help"] = f.help; arr[i]["on"] = Features::on(f.key);
        ++i;
    }
    ctx["features"] = std::move(arr);
    crow::json::wvalue zones = crow::json::wvalue::list();
    i = 0;
    for (const char* z : {"America/New_York", "America/Chicago", "America/Denver", "America/Phoenix",
                          "America/Los_Angeles", "America/Anchorage", "Pacific/Honolulu", "Europe/London",
                          "Europe/Berlin", "Australia/Sydney"}) {
        zones[i]["zone"] = z; zones[i]["selected"] = tz == z; ++i;
    }
    ctx["zones"] = std::move(zones);
    // Optional: LEGO Fan CoLab recognition + Community Ambassador
    auto fc = fan_colab_info(db);
    ctx["fan_colab_recognized"] = fc.recognized;
    ctx["fan_colab_done"] = fc.recognized && fc.ambassador_id > 0 && !fc.ambassador.empty();
    crow::json::wvalue people = crow::json::wvalue::list();
    {
        auto st = db.prepare("SELECT id, display_name FROM members ORDER BY display_name COLLATE NOCASE");
        i = 0;
        while (st.step()) {
            people[i]["id"] = st.col_int(0); people[i]["name"] = st.col_text(1);
            people[i]["selected"] = st.col_int(0) == fc.ambassador_id;
            ++i;
        }
    }
    ctx["people"] = std::move(people);
    if (!flash.empty()) ctx["flash"] = flash;
    return crow::mustache::load("setup/_checklist.html").render(ctx).dump();
}

std::string form(const crow::request& req, const char* k, size_t max = 200) {
    auto q = crow::query_string("?" + req.body);
    const char* v = q.get(k);
    return v ? std::string(v).substr(0, max) : "";
}

} // namespace

std::string setup_token() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_token;
}

std::string ensure_setup_token(SqliteDatabase& db) {
    if (has_admin(db)) return "";
    std::lock_guard<std::mutex> l(g_mu);
    if (g_token.empty()) g_token = SessionStore::generate_token().substr(0, 32);
    return g_token;
}

void register_setup_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings,
                           DiscordClient& discord, CalendarGenerator& calendar, AuditService& audit,
                           const std::string& public_url) {
    if (std::string t = ensure_setup_token(db); !t.empty())
        std::cout << "[setup] No admin yet. Create the first one at "
                  << (public_url.empty() ? "http://<this-server>" : public_url)
                  << "/setup?token=" << t << "\n";

    // GET /setup
    CROW_ROUTE(app, "/setup")([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!has_admin(db)) {
            const char* t = req.url_params.get("token");
            if (!token_ok(t ? t : "")) { res.code = 404; return res; }
            return page(req, app, render_first_admin(t), true);
        }
        if (!require_auth(req, res, app, "admin")) return res;
        return page(req, app, render_checklist(settings, db), false);
    });

    // POST /setup/admin - create the first admin (setup token, only while there's none)
    CROW_ROUTE(app, "/setup/admin").methods("POST"_method)([&app, &db](const crow::request& req) {
        std::string t = form(req, "token", 64);
        if (has_admin(db) || !token_ok(t)) { crow::response r; r.code = 404; return r; }
        std::string first = form(req, "first_name", 50), last = form(req, "last_name", 50);
        std::string discord_id = form(req, "discord_user_id", 30), email = form(req, "email", 200);
        bool id_ok = !discord_id.empty() && discord_id.find_first_not_of("0123456789") == std::string::npos;
        std::string pw = form(req, "password", 201), pw2 = form(req, "confirm", 201);
        if (first.empty() || (!id_ok && email.find('@') == std::string::npos))
            return page(req, app, render_first_admin(t, "Enter your name and your Discord user ID (numbers only) or an email address."), true, 400);
        if (!pw.empty()) {
            std::string err = email.find('@') == std::string::npos ? "A password needs an email address to sign in with."
                                                                    : password::policy_error(pw, email);
            if (err.empty() && pw != pw2) err = "The two passwords don't match.";
            if (!err.empty()) return page(req, app, render_first_admin(t, err), true, 400);
        }
        std::string display = first + (last.empty() ? "" : " " + last.substr(0, 1) + ".");
        {
            auto ins = db.prepare("INSERT INTO members (discord_user_id, display_name, first_name, last_name, email, role, role_source) "
                                  "VALUES (?,?,?,?,?, 'admin', 'manual')");
            if (id_ok) ins.bind(1, discord_id); else ins.bind_null(1);
            ins.bind(2, display); ins.bind(3, first); ins.bind(4, last); ins.bind(5, email);
            ins.step();
        }
        if (!pw.empty()) AccountSecurity(db).set_password(db.last_insert_rowid(), pw);
        {
            std::lock_guard<std::mutex> l(g_mu);
            g_token.clear();
        }
        crow::mustache::context ctx;
        ctx["asset_v"] = asset_version();
        ctx["name"] = display;
        ctx["discord"] = id_ok;
        ctx["password"] = !pw.empty();
        ctx["email"] = email;
        return page(req, app, crow::mustache::load("setup/first_admin_done.html").render(ctx).dump(), true);
    });

    // POST /setup/basics - LUG name + time zone
    CROW_ROUTE(app, "/setup/basics").methods("POST"_method)([&app, &db, &settings, &discord, &calendar, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        std::string name = form(req, "lug_name", 80), tz = form(req, "lug_timezone", 60);
        if (name.empty()) return page(req, app, render_checklist(settings, db, "Give your LUG a name."), true, 400);
        settings.set("lug_name", name);
        if (!tz.empty() && tz.find('/') != std::string::npos) {
            settings.set("lug_timezone", tz);
            discord.set_timezone(tz);
            calendar.set_timezone(tz);
            set_process_timezone(tz);
        }
        audit.log(req, app, "settings.update", "settings", 0, "Setup", "Name and time zone");
        return page(req, app, render_checklist(settings, db, "Saved."), true);
    });

    // POST /setup/features
    CROW_ROUTE(app, "/setup/features").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto q = crow::query_string("?" + req.body);
        for (const auto& f : Features::all()) Features::set(f.key, q.get(f.key) != nullptr);
        settings.set("setup_features_done", "1");
        audit.log(req, app, "settings.features", "settings", 0, "Setup", "Features chosen");
        res.add_header("HX-Refresh", "true");   // sidebar follows the new features
        return page(req, app, render_checklist(settings, db, "Saved."), true);
    });

    // POST /setup/discord - server and announcements channel ids
    CROW_ROUTE(app, "/setup/discord").methods("POST"_method)([&app, &db, &settings, &discord, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        std::string guild = form(req, "discord_guild_id", 30), ch = form(req, "discord_announcements_channel_id", 30);
        auto digits = [](const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
        if (!digits(guild) || (!ch.empty() && !digits(ch)))
            return page(req, app, render_checklist(settings, db, "Server and channel IDs are long numbers (Discord > Developer Mode > Copy ID)."), true, 400);
        settings.set("discord_guild_id", guild);
        if (!ch.empty()) settings.set("discord_announcements_channel_id", ch);
        discord.reconfigure(guild, settings.get("discord_announcements_channel_id", ""),
                            settings.get("discord_events_forum_channel_id", ""),
                            settings.get("discord_announcement_role_id", ""),
                            settings.get("discord_non_lug_event_role_id", ""), discord.get_timezone());
        audit.log(req, app, "settings.update", "settings", 0, "Setup", "Discord server " + guild);
        return page(req, app, render_checklist(settings, db, "Saved."), true);
    });

    // POST /setup/fancolab - Recognized LEGO Fan Community + Community Ambassador
    CROW_ROUTE(app, "/setup/fancolab").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        bool recognized = form(req, "fan_colab_recognized", 2) == "1";
        std::string amb = form(req, "community_ambassador_id", 20);
        int64_t amb_id = 0;
        try { amb_id = std::stoll(amb); } catch (...) {}
        if (amb_id > 0) {
            auto st = db.prepare("SELECT 1 FROM members WHERE id=?");
            st.bind(1, amb_id);
            if (!st.step()) return page(req, app, render_checklist(settings, db, "That member doesn't exist."), true, 400);
        }
        record_ambassador_change(db, amb_id, AttendanceService::today_ymd());
        settings.set("fan_colab_recognized", recognized ? "1" : "0");
        settings.set("community_ambassador_id", amb_id > 0 ? std::to_string(amb_id) : "");
        audit.log(req, app, "settings.update", "settings", 0, "LEGO Fan CoLab",
                  std::string(recognized ? "Recognized" : "Not recognized") + (amb_id > 0 ? ", ambassador #" + std::to_string(amb_id) : ""));
        return page(req, app, render_checklist(settings, db, "Saved."), true);
    });

    // POST /setup/finish
    CROW_ROUTE(app, "/setup/finish").methods("POST"_method)([&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        settings.set("setup_completed", "1");
        audit.log(req, app, "settings.update", "settings", 0, "Setup", "Setup finished");
        res.add_header("HX-Redirect", "/dashboard");
        return res;
    });
}
