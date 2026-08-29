#include "routes/api/PendingDiscordMatchesApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

// API equivalent of the Discord member-match review page (DiscordMatchRoutes.cpp) -
// same read-only list, same two resolution actions (link to an existing member, or
// create a new one from the Discord identity), mirrored exactly so an admin tool can
// drive the same review queue an admin/chapter-lead would work through in the browser.

void register_pending_discord_matches_api_routes(LugApp& app,
                                                   PendingDiscordMatchRepository& pending_matches,
                                                   MemberRepository& member_repo,
                                                   AuditService& audit) {

    // GET /api/v1/discord-matches - unresolved pending matches only (same as the
    // browser page, which only ever shows the unresolved queue).
    CROW_ROUTE(app, "/api/v1/discord-matches").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pending = pending_matches.find_all_unresolved();
        write_json(res, 200, envelope_ok(to_json_list(pending)));
        return res;
    });

    // GET /api/v1/discord-matches/<id>
    CROW_ROUTE(app, "/api/v1/discord-matches/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto p = pending_matches.find_by_id(static_cast<int64_t>(id));
        if (!p) { envelope_error(res, 404, "pending match not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*p)));
        return res;
    });

    // POST /api/v1/discord-matches/<id>/link - link the Discord identity to an
    // existing member. Mirrors POST /settings/discord-matches/<id>/link.
    CROW_ROUTE(app, "/api/v1/discord-matches/<int>/link").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("member_id")) {
            envelope_error(res, 400, "member_id is required", "invalid_request");
            return res;
        }
        int64_t member_id = body["member_id"].i();

        auto pending = pending_matches.find_by_id(static_cast<int64_t>(id));
        if (!pending) { envelope_error(res, 404, "pending match not found", "not_found"); return res; }
        if (!pending->resolved_at.empty()) {
            envelope_error(res, 400, "this match has already been resolved", "conflict");
            return res;
        }

        auto member = member_repo.find_by_id(member_id);
        if (!member) { envelope_error(res, 404, "member not found", "not_found"); return res; }

        try {
            member_repo.link_discord_id(member_id, pending->discord_user_id, pending->discord_username);
            pending_matches.mark_resolved(pending->id, "linked", member_id);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("discord_match.link", "member", member_id, member->display_name,
                              "Linked Discord user " + pending->discord_display_name +
                              " via " + actor_label(ctx.api_key));

            crow::json::wvalue body_out;
            body_out["id"]        = pending->id;
            body_out["action"]    = "linked";
            body_out["member_id"] = member_id;
            write_json(res, 200, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[PendingDiscordMatchesApiRoutes] POST /link error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not link Discord identity", "validation_error");
        }
        return res;
    });

    // POST /api/v1/discord-matches/<id>/create-new - create a fresh member from the
    // Discord identity. Mirrors POST /settings/discord-matches/<id>/create-new.
    CROW_ROUTE(app, "/api/v1/discord-matches/<int>/create-new").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto pending = pending_matches.find_by_id(static_cast<int64_t>(id));
        if (!pending) { envelope_error(res, 404, "pending match not found", "not_found"); return res; }
        if (!pending->resolved_at.empty()) {
            envelope_error(res, 400, "this match has already been resolved", "conflict");
            return res;
        }

        try {
            Member m;
            m.discord_user_id  = pending->discord_user_id;
            m.discord_username = pending->discord_username;
            m.display_name     = pending->discord_display_name;
            m.role             = "member";
            Member created = member_repo.create(m);

            pending_matches.mark_resolved(pending->id, "created_new", created.id);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("discord_match.create_new", "member", created.id, created.display_name,
                              "Created new member from Discord: " + pending->discord_display_name +
                              " via " + actor_label(ctx.api_key));

            crow::json::wvalue body_out;
            body_out["id"]        = pending->id;
            body_out["action"]    = "created_new";
            body_out["member_id"] = created.id;
            write_json(res, 201, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[PendingDiscordMatchesApiRoutes] POST /create-new error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create member", "validation_error");
        }
        return res;
    });
}
