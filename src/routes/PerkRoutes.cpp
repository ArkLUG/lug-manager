#include "routes/PerkRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <ctime>

// Compute the highest perk level a member qualifies for.
// Returns nullopt if they don't qualify for any.
static std::optional<PerkLevel> compute_perk_level(
        const std::vector<PerkLevel>& levels,
        int meeting_count, int event_count, bool is_paid) {
    std::optional<PerkLevel> best;
    for (const auto& lvl : levels) {
        if (meeting_count >= lvl.meeting_attendance_required &&
            event_count   >= lvl.event_attendance_required &&
            (!lvl.requires_paid_dues || is_paid)) {
            if (!best || lvl.sort_order > best->sort_order) {
                best = lvl;
            }
        }
    }
    return best;
}

void register_perk_routes(LugApp& app, PerkLevelRepository& perks,
                           AttendanceRepository& attendance,
                           MemberRepository& members,
                           DiscordClient& discord) {

    // GET /settings/perks — list perk levels (admin only)
    CROW_ROUTE(app, "/settings/perks")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto levels = perks.find_all();

        // Build role ID -> name lookup for display
        std::map<std::string, std::string> role_names;
        try {
            auto guild_roles = discord.fetch_guild_roles();
            for (const auto& r : guild_roles) role_names[r.id] = r.name;
        } catch (...) {}

        crow::mustache::context ctx;
        crow::json::wvalue arr;
        for (size_t i = 0; i < levels.size(); ++i) {
            arr[i]["id"]                          = levels[i].id;
            arr[i]["name"]                        = levels[i].name;
            arr[i]["discord_role_id"]              = levels[i].discord_role_id;
            auto rit = role_names.find(levels[i].discord_role_id);
            arr[i]["discord_role_name"]            = rit != role_names.end() ? ("@" + rit->second) :
                                                     (levels[i].discord_role_id.empty() ? "None" : levels[i].discord_role_id);
            arr[i]["meeting_attendance_required"]  = levels[i].meeting_attendance_required;
            arr[i]["event_attendance_required"]    = levels[i].event_attendance_required;
            arr[i]["requires_paid_dues"]           = levels[i].requires_paid_dues;
            arr[i]["sort_order"]                   = levels[i].sort_order;
        }
        ctx["perk_levels"] = std::move(arr);
        ctx["has_perks"]   = !levels.empty();
        ctx["is_admin"]    = true;
        ctx["title"]       = "Perk Levels";

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        auto tmpl = crow::mustache::load("settings/_perks.html");
        std::string content = tmpl.render(ctx).dump();
        if (is_htmx) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(content);
            return res;
        }

        crow::mustache::context layout_ctx;
        layout_ctx["content"]         = content;
        layout_ctx["page_title"]      = "Perk Levels";
        layout_ctx["active_settings"] = true;
        layout_ctx["is_admin"]        = true;
        auto layout = crow::mustache::load("layout.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(layout.render(layout_ctx).dump());
        return res;
    });

    // POST /settings/perks — create perk level
    CROW_ROUTE(app, "/settings/perks").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        PerkLevel p;
        p.name = gp("name");
        if (p.name.empty()) {
            res.code = 400;
            res.write(R"(<div class="text-red-500 text-sm p-2">Name is required.</div>)");
            return res;
        }
        p.discord_role_id = gp("discord_role_id");
        try { p.meeting_attendance_required = std::stoi(gp("meeting_attendance_required")); } catch (...) {}
        try { p.event_attendance_required = std::stoi(gp("event_attendance_required")); } catch (...) {}
        p.requires_paid_dues = (gp("requires_paid_dues") == "on" || gp("requires_paid_dues") == "1");
        try { p.sort_order = std::stoi(gp("sort_order")); } catch (...) {}

        perks.create(p);
        res.add_header("HX-Redirect", "/settings/perks");
        res.code = 200;
        return res;
    });

    // PUT /settings/perks/<id> — update perk level
    CROW_ROUTE(app, "/settings/perks/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        auto existing = perks.find_by_id(static_cast<int64_t>(id));
        if (!existing) { res.code = 404; res.write("Not found"); return res; }

        PerkLevel p = *existing;
        std::string name = gp("name");
        if (!name.empty()) p.name = name;
        p.discord_role_id = gp("discord_role_id");
        try { p.meeting_attendance_required = std::stoi(gp("meeting_attendance_required")); } catch (...) {}
        try { p.event_attendance_required = std::stoi(gp("event_attendance_required")); } catch (...) {}
        p.requires_paid_dues = (gp("requires_paid_dues") == "on" || gp("requires_paid_dues") == "1");
        try { p.sort_order = std::stoi(gp("sort_order")); } catch (...) {}

        perks.update(p);
        res.add_header("HX-Redirect", "/settings/perks");
        res.code = 200;
        return res;
    });

    // DELETE /settings/perks/<id>
    CROW_ROUTE(app, "/settings/perks/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        perks.remove(static_cast<int64_t>(id));
        res.add_header("HX-Redirect", "/settings/perks");
        res.code = 200;
        return res;
    });

    // POST /api/perks/sync-roles — bulk sync perk Discord roles for all members
    CROW_ROUTE(app, "/api/perks/sync-roles").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto levels = perks.find_all();
        if (levels.empty()) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-yellow-600 text-xs">No perk levels defined.</span>)");
            return res;
        }

        // Get current year
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        int year = tm->tm_year + 1900;

        auto all_members = members.find_all();
        int synced = 0;
        for (const auto& m : all_members) {
            int meeting_count = attendance.count_member_by_year(m.id, year, "meeting");
            int event_count   = attendance.count_member_by_year(m.id, year, "event");

            auto achieved = compute_perk_level(levels, meeting_count, event_count, m.is_paid);

            for (const auto& lvl : levels) {
                if (lvl.discord_role_id.empty() || m.discord_user_id.empty()) continue;

                // Compute if member meets THIS specific tier
                bool meets_tier = meeting_count >= lvl.meeting_attendance_required &&
                                  event_count >= lvl.event_attendance_required &&
                                  (!lvl.requires_paid_dues || m.is_paid);

                if (meets_tier) {
                    try { discord.add_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                } else {
                    try { discord.remove_member_role(m.discord_user_id, lvl.discord_role_id); }
                    catch (...) {}
                }
            }
            ++synced;
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write("<span class=\"text-green-600 text-xs\">Synced " +
                  std::to_string(synced) + " members.</span>");
        return res;
    });
}
