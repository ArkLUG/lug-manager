#include "routes/ChapterRoutes.hpp"
#include <crow/mustache.h>
#include <sstream>
#include <unordered_set>

// Helper: build <option> HTML for Discord role picker
static std::string build_role_options(DiscordClient& discord, const std::string& selected) {
    auto roles = discord.fetch_guild_roles();
    std::ostringstream oss;
    oss << "<option value=\"\">-- No role --</option>\n";
    for (auto& r : roles) {
        oss << "<option value=\"" << r.id << "\"";
        if (r.id == selected) oss << " selected";
        oss << ">@" << r.name << "</option>\n";
    }
    if (roles.empty()) {
        oss.str("");
        oss << "<option value=\"\">No roles found (configure Guild ID in Settings)</option>";
    }
    return oss.str();
}

// Helper: build <option> HTML for channel picker
static std::string build_channel_options(DiscordClient& discord, const std::string& selected) {
    auto channels = discord.fetch_text_channels();
    std::ostringstream oss;
    oss << "<option value=\"\">-- Select a channel --</option>\n";
    for (auto& ch : channels) {
        oss << "<option value=\"" << ch.id << "\"";
        if (ch.id == selected) oss << " selected";
        oss << ">#" << ch.name << "</option>\n";
    }
    if (channels.empty()) {
        oss.str("");
        oss << "<option value=\"\">No channels found (configure Guild ID in Settings)</option>";
    }
    return oss.str();
}

void register_chapter_routes(LugApp& app, ChapterService& chapters,
                              ChapterMemberRepository& chapter_members,
                              MemberService& members,
                              DiscordClient& discord) {

    // GET /chapters - list all chapters
    CROW_ROUTE(app, "/chapters")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) {
            res.code = 401;
            res.write("Unauthorized");
            return res;
        }

        auto all_chapters = chapters.list_all();
        auto stats = chapter_members.get_all_chapter_stats();

        crow::mustache::context mctx;
        mctx["title"] = "Chapters";
        mctx["is_admin"] = ctx.auth.is_admin();
        mctx["can_see_dues"] = ctx.auth.is_chapter_lead();

        bool can_see_dues = ctx.auth.is_chapter_lead();
        crow::json::wvalue arr;
        for (size_t i = 0; i < all_chapters.size(); ++i) {
            const auto& ch = all_chapters[i];
            arr[i]["id"]          = ch.id;
            arr[i]["name"]        = ch.name;
            arr[i]["can_see_dues"] = can_see_dues;
            arr[i]["shorthand"]   = ch.shorthand;
            arr[i]["has_shorthand"] = !ch.shorthand.empty();
            arr[i]["description"] = ch.description;

            auto it = stats.find(ch.id);
            if (it != stats.end()) {
                const auto& s = it->second;
                arr[i]["member_count"] = s.member_count;
                arr[i]["paid_count"]   = s.paid_count;
                // Join lead names into a comma-separated string
                std::string leads_str;
                for (size_t j = 0; j < s.lead_names.size(); ++j) {
                    if (j > 0) leads_str += ", ";
                    leads_str += s.lead_names[j];
                }
                arr[i]["leads_str"]    = leads_str;
                arr[i]["has_leads"]    = !s.lead_names.empty();
            } else {
                arr[i]["member_count"] = 0;
                arr[i]["paid_count"]   = 0;
                arr[i]["leads_str"]    = "";
                arr[i]["has_leads"]    = false;
            }
        }
        mctx["chapters"] = std::move(arr);

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("chapters/_list.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("chapters/_list.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]        = content;
            layout_ctx["page_title"]     = "Chapters";
            layout_ctx["active_chapters"]= true;
            layout_ctx["is_admin"]       = ctx.auth.role == "admin";
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /chapters - create chapter (admin only)
    CROW_ROUTE(app, "/chapters").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("Forbidden: admin only");
            return res;
        }

        auto params = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        Chapter ch;
        ch.name = get_param("name");
        ch.shorthand = get_param("shorthand");
        ch.description = get_param("description");
        ch.discord_announcement_channel_id = get_param("discord_announcement_channel_id");
        ch.discord_lead_role_id            = get_param("discord_lead_role_id");
        ch.discord_member_role_id          = get_param("discord_member_role_id");
        ch.created_by = ctx.auth.member_id;

        try {
            chapters.create(ch);
            res.add_header("HX-Redirect", "/chapters");
            res.code = 200;
            res.write("{\"success\":true}");
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // GET /chapters/<id> - chapter detail
    CROW_ROUTE(app, "/chapters/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) {
            res.code = 401;
            res.write("Unauthorized");
            return res;
        }

        auto ch = chapters.get(static_cast<int64_t>(id));
        if (!ch) {
            res.code = 404;
            res.write("Chapter not found");
            return res;
        }

        bool is_admin = ctx.auth.role == "admin";
        bool can_manage = is_admin;
        if (!can_manage) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, ch->id);
            can_manage = (role && *role == "lead");
        }

        // Build leads list and "add lead" options (non-leads in chapter)
        auto ch_members = chapter_members.find_by_chapter(ch->id);
        crow::json::wvalue leads_arr;
        std::ostringstream add_lead_opts;
        size_t lead_count = 0;
        bool has_non_leads = false;
        add_lead_opts << "<option value=\"\">-- Select member --</option>\n";
        for (auto& cm : ch_members) {
            if (cm.chapter_role == "lead") {
                leads_arr[lead_count]["member_id"]       = cm.member_id;
                leads_arr[lead_count]["display_name"]    = cm.display_name;
                leads_arr[lead_count]["discord_username"]= cm.discord_username;
                leads_arr[lead_count]["chapter_id"]      = ch->id;
                ++lead_count;
            } else {
                add_lead_opts << "<option value=\"" << cm.member_id << "\">"
                              << cm.display_name << " (@" << cm.discord_username << ")</option>\n";
                has_non_leads = true;
            }
        }

        crow::mustache::context mctx;
        mctx["id"]              = ch->id;
        mctx["name"]            = ch->name;
        mctx["shorthand"]       = ch->shorthand;
        mctx["has_shorthand"]   = !ch->shorthand.empty();
        mctx["description"]     = ch->description;
        mctx["discord_channel"] = ch->discord_announcement_channel_id;
        mctx["is_admin"]        = is_admin;
        mctx["can_manage"]      = can_manage;
        mctx["leads"]           = std::move(leads_arr);
        mctx["has_leads"]       = lead_count > 0;
        mctx["add_lead_options"]= add_lead_opts.str();
        mctx["has_non_leads"]   = has_non_leads;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("chapters/_detail.html");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("chapters/_detail.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = ch->name;
            layout_ctx["active_chapters"] = true;
            layout_ctx["is_admin"]        = is_admin;
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /chapters/<id>/lead - add a chapter lead (admin only)
    CROW_ROUTE(app, "/chapters/<int>/lead").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);

        int64_t chapter_id = static_cast<int64_t>(id);

        auto params = crow::query_string("?" + req.body);
        const char* mid_raw = params.get("member_id");
        if (!mid_raw || std::string(mid_raw).empty()) {
            res.code = 400; res.write("member_id required"); return res;
        }

        int64_t new_lead_id = std::stoll(mid_raw);
        chapter_members.upsert(new_lead_id, chapter_id, "lead", ctx.auth.member_id);

        // Assign the chapter's lead Discord role if configured
        auto ch = chapters.get(chapter_id);
        if (ch && !ch->discord_lead_role_id.empty()) {
            auto member = members.get(new_lead_id);
            if (member && !member->discord_user_id.empty()) {
                try {
                    discord.add_member_role(member->discord_user_id, ch->discord_lead_role_id);
                } catch (const std::exception& e) {
                    std::cerr << "[ChapterRoutes] Failed to add lead role: " << e.what() << "\n";
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id));
        res.code = 200;
        return res;
    });

    // POST /chapters/<id>/lead/<member_id>/demote - remove lead role (admin only)
    CROW_ROUTE(app, "/chapters/<int>/lead/<int>/demote").methods("POST"_method)(
        [&](const crow::request& req, int id, int member_id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);

        int64_t chapter_id = static_cast<int64_t>(id);

        chapter_members.upsert(static_cast<int64_t>(member_id), chapter_id,
                               "member", ctx.auth.member_id);

        // Remove the chapter's lead Discord role if configured
        auto ch = chapters.get(chapter_id);
        if (ch && !ch->discord_lead_role_id.empty()) {
            auto m = members.get(static_cast<int64_t>(member_id));
            if (m && !m->discord_user_id.empty()) {
                try {
                    discord.remove_member_role(m->discord_user_id, ch->discord_lead_role_id);
                } catch (const std::exception& e) {
                    std::cerr << "[ChapterRoutes] Failed to remove lead role: " << e.what() << "\n";
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id));
        res.code = 200;
        return res;
    });

    // PUT /chapters/<id> - update chapter (admin only)
    CROW_ROUTE(app, "/chapters/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("{\"error\":\"Forbidden\"}");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        // Support both form-encoded (from modal) and JSON (from API clients)
        Chapter updates;
        std::string content_type = req.get_header_value("Content-Type");
        if (content_type.find("application/json") != std::string::npos) {
            auto body = crow::json::load(req.body);
            if (!body) {
                res.code = 400;
                res.write("{\"error\":\"Invalid JSON\"}");
                res.add_header("Content-Type", "application/json");
                return res;
            }
            if (body.has("name")) updates.name = body["name"].s();
            if (body.has("shorthand")) updates.shorthand = body["shorthand"].s();
            if (body.has("description")) updates.description = body["description"].s();
            if (body.has("discord_announcement_channel_id"))
                updates.discord_announcement_channel_id = body["discord_announcement_channel_id"].s();
            if (body.has("discord_lead_role_id"))
                updates.discord_lead_role_id = body["discord_lead_role_id"].s();
            if (body.has("discord_member_role_id"))
                updates.discord_member_role_id = body["discord_member_role_id"].s();
        } else {
            auto params = crow::query_string("?" + req.body);
            auto gp = [&](const char* k) -> std::string {
                const char* v = params.get(k); return v ? std::string(v) : "";
            };
            updates.name = gp("name");
            updates.shorthand = gp("shorthand");
            updates.description = gp("description");
            updates.discord_announcement_channel_id = gp("discord_announcement_channel_id");
            updates.discord_lead_role_id   = gp("discord_lead_role_id");
            updates.discord_member_role_id = gp("discord_member_role_id");
        }

        try {
            auto updated = chapters.update(static_cast<int64_t>(id), updates);
            res.write("{\"success\":true}");
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // DELETE /chapters/<id> - delete chapter (admin only)
    CROW_ROUTE(app, "/chapters/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("{\"error\":\"Forbidden\"}");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        try {
            chapters.delete_chapter(static_cast<int64_t>(id));
            res.add_header("HX-Redirect", "/chapters");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // GET /chapters/new - create form modal (admin only)
    CROW_ROUTE(app, "/chapters/new")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("Forbidden");
            return res;
        }

        crow::mustache::context mctx;
        mctx["channel_options"]      = build_channel_options(discord, "");
        mctx["role_options"]         = build_role_options(discord, "");
        mctx["member_role_options"]  = build_role_options(discord, "");

        auto tmpl = crow::mustache::load("chapters/_form.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /chapters/<id>/members - chapter member management (admin or chapter lead)
    CROW_ROUTE(app, "/chapters/<int>/members")([&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; res.write("Unauthorized"); return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        bool is_lead  = false;
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            is_lead = (role && *role == "lead");
        }
        if (!is_admin && !is_lead) {
            res.code = 403; res.write("Forbidden"); return res;
        }

        auto ch = chapters.get(chapter_id);
        if (!ch) { res.code = 404; res.write("Chapter not found"); return res; }

        auto member_list = chapter_members.find_by_chapter(chapter_id);
        // All LUG members not already in this chapter (for the "add member" dropdown)
        auto all_members = members.list_all();

        crow::mustache::context mctx;
        mctx["chapter_id"]   = ch->id;
        mctx["chapter_name"] = ch->name;
        mctx["is_admin"]     = is_admin;

        crow::json::wvalue cm_arr;
        for (size_t i = 0; i < member_list.size(); ++i) {
            const auto& cm = member_list[i];
            cm_arr[i]["member_id"]       = cm.member_id;
            cm_arr[i]["display_name"]    = cm.display_name;
            cm_arr[i]["discord_username"]= cm.discord_username;
            cm_arr[i]["chapter_role"]    = cm.chapter_role;
            cm_arr[i]["is_lead"]         = (cm.chapter_role == "lead");
            cm_arr[i]["is_event_manager"]= (cm.chapter_role == "event_manager");
            cm_arr[i]["is_member"]       = (cm.chapter_role == "member");
        }
        mctx["members"] = std::move(cm_arr);

        // Build set of existing member IDs for filtering
        std::unordered_set<int64_t> in_chapter;
        for (auto& cm : member_list) in_chapter.insert(cm.member_id);

        std::ostringstream member_opts;
        member_opts << "<option value=\"\">-- Select member --</option>\n";
        for (auto& m : all_members) {
            if (in_chapter.count(m.id)) continue;
            member_opts << "<option value=\"" << m.id << "\">"
                        << m.display_name << " (@" << m.discord_username << ")</option>\n";
        }
        mctx["member_options"] = member_opts.str();

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx_req = req.get_header_value("HX-Request") == "true";
        auto tmpl = crow::mustache::load("chapters/_members.html");
        if (is_htmx_req) {
            res.write(tmpl.render(mctx).dump());
        } else {
            std::string content = tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = ch->name + " — Members";
            layout_ctx["active_chapters"] = true;
            layout_ctx["is_admin"]        = is_admin;
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /chapters/<id>/members - add or update a member's chapter role
    CROW_ROUTE(app, "/chapters/<int>/members").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            if (!role || *role != "lead") {
                res.code = 403;
                res.write(R"(<div class="text-red-500 text-sm p-2">Only chapter leads can manage members.</div>)");
                return res;
            }
        }

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };

        std::string member_id_str = gp("member_id");
        std::string chapter_role  = gp("chapter_role");
        if (member_id_str.empty() || chapter_role.empty()) {
            res.code = 400;
            res.write(R"(<div class="text-red-500 text-sm p-2">member_id and chapter_role required.</div>)");
            return res;
        }

        int64_t member_id = std::stoll(member_id_str);

        // Only admins can assign lead role
        if (chapter_role == "lead" && !is_admin) {
            res.code = 403;
            res.write(R"(<div class="text-red-500 text-sm p-2">Only admins can assign the lead role.</div>)");
            return res;
        }

        // Check previous role so we know whether to add/remove the Discord lead role
        auto prev_role = chapter_members.get_chapter_role(member_id, chapter_id);
        bool was_lead  = prev_role && *prev_role == "lead";
        bool will_lead = chapter_role == "lead";

        chapter_members.upsert(member_id, chapter_id, chapter_role, ctx.auth.member_id);

        // Sync Discord lead role if the chapter has one configured
        auto ch = chapters.get(chapter_id);
        if (ch && !ch->discord_lead_role_id.empty()) {
            auto member = members.get(member_id);
            if (member && !member->discord_user_id.empty()) {
                if (!was_lead && will_lead) {
                    discord.add_member_role(member->discord_user_id, ch->discord_lead_role_id);
                } else if (was_lead && !will_lead) {
                    discord.remove_member_role(member->discord_user_id, ch->discord_lead_role_id);
                }
            }
        }

        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id) + "/members");
        res.code = 200;
        return res;
    });

    // DELETE /chapters/<id>/members/<member_id> - remove member from chapter
    CROW_ROUTE(app, "/chapters/<int>/members/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id, int member_id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        int64_t chapter_id = static_cast<int64_t>(id);
        bool is_admin = ctx.auth.role == "admin";
        if (!is_admin) {
            auto role = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
            if (!role || *role != "lead") { res.code = 403; return res; }
        }

        // Remove Discord lead role if they had it
        auto prev_role = chapter_members.get_chapter_role(static_cast<int64_t>(member_id), chapter_id);
        if (prev_role && *prev_role == "lead") {
            auto ch = chapters.get(chapter_id);
            if (ch && !ch->discord_lead_role_id.empty()) {
                auto member = members.get(static_cast<int64_t>(member_id));
                if (member && !member->discord_user_id.empty()) {
                    discord.remove_member_role(member->discord_user_id, ch->discord_lead_role_id);
                }
            }
        }

        chapter_members.remove(static_cast<int64_t>(member_id), chapter_id);
        res.add_header("HX-Redirect", "/chapters/" + std::to_string(id) + "/members");
        res.code = 200;
        return res;
    });

    // GET /chapters/<id>/edit - edit form modal (admin only)
    CROW_ROUTE(app, "/chapters/<int>/edit")([&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("Forbidden");
            return res;
        }

        auto ch = chapters.get(static_cast<int64_t>(id));
        if (!ch) {
            res.code = 404;
            res.write("Chapter not found");
            return res;
        }

        crow::mustache::context mctx;
        mctx["id"]              = ch->id;
        mctx["name"]            = ch->name;
        mctx["shorthand"]       = ch->shorthand;
        mctx["description"]     = ch->description;
        mctx["channel_options"]     = build_channel_options(discord, ch->discord_announcement_channel_id);
        mctx["role_options"]        = build_role_options(discord, ch->discord_lead_role_id);
        mctx["member_role_options"] = build_role_options(discord, ch->discord_member_role_id);
        mctx["is_edit"]             = true;

        auto tmpl = crow::mustache::load("chapters/_form.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(tmpl.render(mctx).dump());
        return res;
    });
}
