#include "routes/RoleRoutes.hpp"
#include <crow/mustache.h>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

void register_role_routes(LugApp& app,
                           RoleMappingRepository& role_mappings,
                           ChapterService& chapters,
                           DiscordClient& discord,
                           AuditService& audit) {

    // GET /api/discord/roles
    // Returns JSON array of Discord guild roles (for role mapping UI).
    CROW_ROUTE(app, "/api/discord/roles")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.code = 403; return res; }

        auto guild_roles  = discord.fetch_guild_roles();
        auto existing     = role_mappings.find_all();

        // Build a lookup: discord_role_id -> lug_role
        std::unordered_map<std::string, std::string> mapped;
        for (auto& m : existing) mapped[m.discord_role_id] = m.lug_role;

        crow::json::wvalue arr;
        for (size_t i = 0; i < guild_roles.size(); ++i) {
            arr[i]["id"]       = guild_roles[i].id;
            arr[i]["name"]     = guild_roles[i].name;
            arr[i]["color"]    = guild_roles[i].color;
            auto it = mapped.find(guild_roles[i].id);
            arr[i]["lug_role"] = (it != mapped.end()) ? it->second : "";
        }
        res.add_header("Content-Type", "application/json");
        res.write(arr.dump());
        return res;
    });

    // GET /api/chapter-options?selected=<chapter_id>
    // Returns <option> HTML for chapter select dropdowns.
    CROW_ROUTE(app, "/api/chapter-options")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        std::string selected;
        const char* s = req.url_params.get("selected");
        if (s) selected = s;

        auto all = chapters.list_all();
        std::ostringstream html;
        html << "<option value=\"\">-- Select chapter --</option>\n";
        for (auto& ch : all) {
            html << "<option value=\"" << ch.id << "\"";
            if (selected == std::to_string(ch.id)) html << " selected";
            html << ">" << ch.name << "</option>\n";
        }
        if (all.empty()) {
            html.str("");
            html << "<option value=\"\">No chapters defined yet</option>";
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /settings/roles - role mapping admin page
    CROW_ROUTE(app, "/settings/roles")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.redirect("/dashboard"); return res; }

        auto guild_roles = discord.fetch_guild_roles();
        auto existing    = role_mappings.find_all();

        // Build lookup: discord_role_id -> lug_role
        std::unordered_map<std::string, std::string> mapped;
        for (auto& m : existing) mapped[m.discord_role_id] = m.lug_role;

        // Build per-LUG-role option lists (same Discord roles, different "selected" flag)
        auto build_options = [&](const std::string& lug_role) {
            crow::json::wvalue arr;
            for (size_t i = 0; i < guild_roles.size(); ++i) {
                arr[i]["id"]       = guild_roles[i].id;
                arr[i]["name"]     = guild_roles[i].name;
                auto it = mapped.find(guild_roles[i].id);
                arr[i]["selected"] = (it != mapped.end() && it->second == lug_role);
            }
            return arr;
        };

        crow::mustache::context mctx;
        mctx["admin_roles"]     = build_options("admin");
        mctx["member_roles"]    = build_options("member");
        mctx["guild_configured"]= !discord.get_guild_id().empty();
        mctx["has_discord_roles"]= !guild_roles.empty();

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("settings/_roles.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("settings/_roles.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = "Role Mappings";
            layout_ctx["active_settings"] = true;
            layout_ctx["is_admin"]        = true;
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /settings/roles
    // Form submits lug_admin[]=<discord_id> and lug_member[]=<discord_id> as repeated params.
    // Any Discord role not present in either list has its mapping removed.
    CROW_ROUTE(app, "/settings/roles").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.code = 403; res.write("Forbidden"); return res; }

        auto params = crow::query_string("?" + req.body);

        std::unordered_set<std::string> admin_ids, member_ids;
        for (auto* id : params.get_list("lug_admin", false))  if (id) admin_ids.insert(id);
        for (auto* id : params.get_list("lug_member", false)) if (id) member_ids.insert(id);

        auto guild_roles = discord.fetch_guild_roles();
        for (auto& gr : guild_roles) {
            if (admin_ids.count(gr.id)) {
                role_mappings.upsert(gr.id, gr.name, "admin");
            } else if (member_ids.count(gr.id)) {
                role_mappings.upsert(gr.id, gr.name, "member");
            } else {
                role_mappings.remove(gr.id);
            }
        }

        audit.log(req, app, "roles.update", "settings", 0, "", "Updated role mappings");

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.add_header("HX-Redirect", "/settings/roles");
            res.code = 200;
        } else {
            res.redirect("/settings/roles");
        }
        return res;
    });
}
