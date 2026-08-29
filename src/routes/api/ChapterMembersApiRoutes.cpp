#include "routes/api/ChapterMembersApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

void register_chapter_members_api_routes(LugApp& app, ChapterMemberRepository& chapter_members,
                                          ChapterService& chapters, MemberRepository& member_repo,
                                          DiscordClient& discord, AuditService& audit) {

    // GET /api/v1/chapter-members?chapter_id=&member_id= - at least one filter required
    CROW_ROUTE(app, "/api/v1/chapter-members").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        const char* chapter_p = req.url_params.get("chapter_id");
        const char* member_p  = req.url_params.get("member_id");
        if (!chapter_p && !member_p) {
            envelope_error(res, 400, "chapter_id or member_id query param required", "invalid_request");
            return res;
        }

        std::vector<ChapterMember> items;
        try {
            if (chapter_p) {
                items = chapter_members.find_by_chapter(std::stoll(std::string(chapter_p)));
            } else {
                items = chapter_members.find_by_member(std::stoll(std::string(member_p)));
            }
        } catch (const std::exception&) {
            envelope_error(res, 400, "chapter_id/member_id must be numeric", "invalid_request");
            return res;
        }
        write_json(res, 200, envelope_ok(to_json_list(items)));
        return res;
    });

    // POST /api/v1/chapter-members - upsert (create or update a member's chapter role).
    // Requires admin scope if chapter_role == "lead".
    CROW_ROUTE(app, "/api/v1/chapter-members").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("member_id") || !body.has("chapter_id") || !body.has("chapter_role")) {
            envelope_error(res, 400, "member_id, chapter_id, and chapter_role are required", "invalid_request");
            return res;
        }

        std::string chapter_role = body["chapter_role"].s();
        if (chapter_role == "lead") {
            if (!require_api_scope(req, res, app, "admin")) return res;
        }

        int64_t member_id  = body["member_id"].i();
        int64_t chapter_id = body["chapter_id"].i();
        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);

        try {
            // Read the prior role before upserting so a lead <-> non-lead
            // transition can be detected and mirrored to Discord, same as
            // the browser's /chapters/<id>/lead and .../demote routes.
            auto prior_role = chapter_members.get_chapter_role(member_id, chapter_id);
            bool was_lead = prior_role && *prior_role == "lead";
            bool now_lead = chapter_role == "lead";

            chapter_members.upsert(member_id, chapter_id, chapter_role, /*granted_by=*/0);
            audit.log_system("chapter_member.upsert", "chapter_member", member_id,
                              "member " + std::to_string(member_id) + " in chapter " + std::to_string(chapter_id),
                              "Set role=" + chapter_role + " via " + actor_label(ctx.api_key));

            if (was_lead != now_lead) {
                auto ch = chapters.get(chapter_id);
                auto mbr = member_repo.find_by_id(member_id);
                if (ch && mbr && !ch->discord_lead_role_id.empty() && !mbr->discord_user_id.empty()) {
                    try {
                        if (now_lead) discord.add_member_role(mbr->discord_user_id, ch->discord_lead_role_id);
                        else          discord.remove_member_role(mbr->discord_user_id, ch->discord_lead_role_id);
                    } catch (const std::exception& e) {
                        std::cerr << "[ChapterMembersApiRoutes] Failed to sync lead Discord role: " << e.what() << "\n";
                    }
                }
            }

            crow::json::wvalue body_out;
            body_out["member_id"]    = member_id;
            body_out["chapter_id"]   = chapter_id;
            body_out["chapter_role"] = chapter_role;
            write_json(res, 201, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[ChapterMembersApiRoutes] POST /api/v1/chapter-members error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not set chapter membership", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/chapter-members/<member_id>/<chapter_id> - thin alias to the same upsert,
    // for REST-shape consistency with other entities (no separate create/update semantics
    // exist in the repository - upsert covers both).
    CROW_ROUTE(app, "/api/v1/chapter-members/<int>/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int member_id, int chapter_id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("chapter_role")) {
            envelope_error(res, 400, "chapter_role is required", "invalid_request");
            return res;
        }

        std::string chapter_role = body["chapter_role"].s();
        if (chapter_role == "lead") {
            if (!require_api_scope(req, res, app, "admin")) return res;
        }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        try {
            chapter_members.upsert(member_id, chapter_id, chapter_role, /*granted_by=*/0);
            audit.log_system("chapter_member.upsert", "chapter_member", member_id,
                              "member " + std::to_string(member_id) + " in chapter " + std::to_string(chapter_id),
                              "Set role=" + chapter_role + " via " + actor_label(ctx.api_key));
            crow::json::wvalue body_out;
            body_out["member_id"]    = member_id;
            body_out["chapter_id"]   = chapter_id;
            body_out["chapter_role"] = chapter_role;
            write_json(res, 200, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[ChapterMembersApiRoutes] PUT chapter-members error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not set chapter membership", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/chapter-members?member_id=&chapter_id= - admin scope (composite key, no bare id)
    CROW_ROUTE(app, "/api/v1/chapter-members").methods("DELETE"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        const char* member_p  = req.url_params.get("member_id");
        const char* chapter_p = req.url_params.get("chapter_id");
        if (!member_p || !chapter_p) {
            envelope_error(res, 400, "member_id and chapter_id query params required", "invalid_request");
            return res;
        }

        try {
            int64_t member_id  = std::stoll(std::string(member_p));
            int64_t chapter_id = std::stoll(std::string(chapter_p));
            chapter_members.remove(member_id, chapter_id);
            auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
            audit.log_system("chapter_member.remove", "chapter_member", member_id,
                              "member " + std::to_string(member_id) + " from chapter " + std::to_string(chapter_id),
                              "Removed via " + actor_label(ctx.api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[ChapterMembersApiRoutes] DELETE chapter-members error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not remove chapter membership", "validation_error");
        }
        return res;
    });
}
