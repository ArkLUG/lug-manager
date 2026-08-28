#include "routes/DiscordMatchRoutes.hpp"
#include <crow/mustache.h>
#include <sstream>

// Builds the <option> list for the member picker used on the review page,
// with the given member id pre-selected (mirrors EventRoutes.cpp's
// /api/member-options helper, but rendered inline since this page's picker
// needs a per-row default selection computed server-side).
static std::string build_member_options(const std::vector<Member>& all, int64_t selected) {
    std::ostringstream oss;
    oss << "<option value=\"\">-- Select member --</option>\n";
    for (const auto& m : all) {
        oss << "<option value=\"" << m.id << "\"";
        if (m.id == selected) oss << " selected";
        oss << ">" << m.display_name << "</option>\n";
    }
    if (all.empty()) {
        oss.str("");
        oss << "<option value=\"\">No members found</option>";
    }
    return oss.str();
}

void register_discord_match_routes(LugApp& app,
                                    PendingDiscordMatchRepository& pending_matches,
                                    MemberRepository& member_repo,
                                    AuditService& audit) {

    // GET /settings/discord-matches - review queue page
    CROW_ROUTE(app, "/settings/discord-matches")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.is_chapter_lead()) {
            res.redirect("/dashboard");
            return res;
        }

        auto pending = pending_matches.find_all_unresolved();
        auto all_members = member_repo.find_all();

        crow::mustache::context mctx;
        crow::json::wvalue arr;
        for (size_t i = 0; i < pending.size(); ++i) {
            const auto& p = pending[i];
            arr[i]["id"]                    = p.id;
            arr[i]["discord_display_name"]  = p.discord_display_name;
            arr[i]["discord_username"]      = p.discord_username;
            arr[i]["member_options"]        = build_member_options(all_members, p.suggested_member_id);
        }
        mctx["pending"]     = std::move(arr);
        mctx["has_pending"] = !pending.empty();

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("settings/_discord_matches.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("settings/_discord_matches.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]                 = content;
            layout_ctx["page_title"]              = "Discord Matches";
            layout_ctx["active_discord_matches"]  = true;
            set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /settings/discord-matches/<id>/link - link the Discord identity to an existing member
    CROW_ROUTE(app, "/settings/discord-matches/<int>/link").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.is_chapter_lead()) {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto params = crow::query_string("?" + req.body);
        int64_t member_id = 0;
        if (auto v = params.get("member_id")) {
            try { member_id = std::stoll(v); } catch (...) {}
        }

        auto pending = pending_matches.find_by_id(static_cast<int64_t>(id));
        if (!pending || !pending->resolved_at.empty()) {
            res.write(R"(<tr><td colspan="4" class="px-3 py-2 text-gray-400 text-center">Already resolved</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }
        if (member_id == 0) {
            res.write(R"(<tr><td colspan="4" class="px-3 py-2 text-red-600">Select a member first</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto member = member_repo.find_by_id(member_id);
        if (!member) {
            res.write(R"(<tr><td colspan="4" class="px-3 py-2 text-red-600">Member not found</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        member_repo.link_discord_id(member_id, pending->discord_user_id, pending->discord_username);
        pending_matches.mark_resolved(pending->id, "linked", member_id);
        audit.log(req, app, "discord_match.link", "member", member_id, member->display_name,
                  "Linked Discord user " + pending->discord_display_name + " to existing member");

        res.write("<tr class=\"bg-green-50\"><td colspan=\"4\" class=\"px-3 py-2 text-green-700 text-center\">"
                   "Linked to " + member->display_name + "</td></tr>");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /settings/discord-matches/<id>/create-new - create a fresh member from the Discord identity
    CROW_ROUTE(app, "/settings/discord-matches/<int>/create-new").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.is_chapter_lead()) {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto pending = pending_matches.find_by_id(static_cast<int64_t>(id));
        if (!pending || !pending->resolved_at.empty()) {
            res.write(R"(<tr><td colspan="4" class="px-3 py-2 text-gray-400 text-center">Already resolved</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        Member m;
        m.discord_user_id  = pending->discord_user_id;
        m.discord_username = pending->discord_username;
        m.display_name     = pending->discord_display_name;
        m.role              = "member";
        Member created = member_repo.create(m);

        pending_matches.mark_resolved(pending->id, "created_new", created.id);
        audit.log(req, app, "discord_match.create_new", "member", created.id, created.display_name,
                  "Created new member from Discord: " + pending->discord_display_name);

        res.write("<tr class=\"bg-green-50\"><td colspan=\"4\" class=\"px-3 py-2 text-green-700 text-center\">"
                   "Created new member " + created.display_name + "</td></tr>");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });
}
