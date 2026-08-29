#include "routes/DiscordMatchRoutes.hpp"
#include <crow/mustache.h>
#include <sstream>
#include <unordered_set>

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

// Same "keep a previously-saved id visible as a selected option even if the
// live Discord fetch comes back empty" logic as SettingsRoutes.cpp's
// build_options - duplicated here (rather than shared) because these are two
// separate small route files and the helper is a few lines; see that file's
// longer comment for the full rationale (production once silently wiped a
// setting when a live-fetch dropdown rendered with nothing selected).
static std::string build_channel_options(const std::vector<DiscordChannel>& channels,
                                          const std::string& selected,
                                          const std::string& empty_label,
                                          const std::string& none_msg) {
    std::ostringstream oss;
    oss << "<option value=\"\">" << empty_label << "</option>\n";
    bool saw_selected = selected.empty();
    for (auto& ch : channels) {
        oss << "<option value=\"" << ch.id << "\"";
        if (ch.id == selected) { oss << " selected"; saw_selected = true; }
        oss << ">#" << ch.name << "</option>\n";
    }
    if (channels.empty()) {
        oss.str("");
        oss << "<option value=\"\">" << none_msg << "</option>\n";
    }
    if (!saw_selected)
        oss << "<option value=\"" << selected << "\" selected>(saved: " << selected << ")</option>\n";
    return oss.str();
}

void register_discord_match_routes(LugApp& app,
                                    PendingDiscordMatchRepository& pending_matches,
                                    MemberRepository& member_repo,
                                    AuditService& audit,
                                    SettingsRepository& settings,
                                    DiscordClient& discord) {

    // GET /settings/discord-matches - review queue page. Admins additionally
    // see the notification-channel/authorized-roles config form at the top;
    // chapter leads/moderators (who can also reach this page) only see the
    // review queue itself - that config is Discord-wide, admin-only.
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

        bool is_admin = ctx.auth.role == "admin";
        mctx["is_admin"] = is_admin;
        if (is_admin) {
            std::string guild_id = settings.get("discord_guild_id", discord.get_guild_id());
            std::string no_guild = "Enter a Guild ID first on the Discord settings page";

            std::string matches_channel = settings.get("discord_matches_notification_channel_id", "");
            std::string matches_channel_options = guild_id.empty()
                ? "<option value=\"\">" + no_guild + "</option>"
                : build_channel_options(discord.fetch_text_channels(), matches_channel,
                                        "-- Select a channel --",
                                        "No text channels found (check guild ID &amp; bot permissions)");
            mctx["matches_channel_id"]      = matches_channel;
            mctx["matches_channel_options"] = matches_channel_options;

            // Explicit admin-chosen role-ID allowlist, never a role-name match.
            std::string authorized_role_ids_csv = settings.get("discord_matches_authorized_role_ids", "");
            std::unordered_set<std::string> authorized_role_ids;
            {
                std::istringstream ss(authorized_role_ids_csv);
                std::string rid;
                while (std::getline(ss, rid, ',')) if (!rid.empty()) authorized_role_ids.insert(rid);
            }
            auto all_roles = guild_id.empty() ? std::vector<DiscordRole>{} : discord.fetch_guild_roles();
            std::ostringstream authorized_role_options;
            for (auto& r : all_roles) {
                authorized_role_options << "<option value=\"" << r.id << "\""
                    << (authorized_role_ids.count(r.id) ? " selected" : "") << ">@" << r.name << "</option>\n";
            }
            mctx["authorized_role_options"] = authorized_role_options.str();
        }

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

    // POST /settings/discord-matches - save notification channel + authorized
    // role allowlist. Its own dedicated form/endpoint (admin-only, unlike the
    // rest of this page): only ever touches these two settings, so saving it
    // can never affect any other settings section elsewhere in the app.
    CROW_ROUTE(app, "/settings/discord-matches").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write("Forbidden");
            return res;
        }

        auto params = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        // Allow clearing (empty = disabled) - this form's own field, no
        // cross-section ambiguity.
        std::string matches_channel = get_param("discord_matches_notification_channel_id");
        settings.set("discord_matches_notification_channel_id", matches_channel);

        // Explicit admin-chosen role-ID allowlist, never a role-name match.
        // Empty = nobody authorized (default-closed).
        auto role_vals = params.get_list("discord_matches_authorized_role_ids", false);
        std::string csv;
        for (auto* r : role_vals)
            if (r && r[0]) { if (!csv.empty()) csv += ","; csv += r; }
        settings.set("discord_matches_authorized_role_ids", csv);

        audit.log(req, app, "settings.update", "settings", 0, "", "Updated Discord match settings");

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.add_header("HX-Redirect", "/settings/discord-matches");
            res.code = 200;
        } else {
            res.redirect("/settings/discord-matches");
        }
        return res;
    });
}
