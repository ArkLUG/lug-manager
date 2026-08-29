#include "routes/api/MembersApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>

void register_members_api_routes(LugApp& app, MemberService& members,
                                  MemberRepository& member_repo, AuditService& audit) {

    // GET /api/v1/members - paginated list
    CROW_ROUTE(app, "/api/v1/members").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pg = parse_pagination(req, 25, 200);
        const char* search_p = req.url_params.get("search");
        std::string search = search_p ? search_p : "";
        const char* sort_p  = req.url_params.get("sort");
        const char* dir_p   = req.url_params.get("dir");
        std::string sort_col = sort_p ? sort_p : "display_name";
        std::string sort_dir = dir_p  ? dir_p  : "asc";

        auto items = member_repo.find_paginated(search, sort_col, sort_dir, pg.limit, pg.offset);
        int total = search.empty() ? member_repo.count_all() : member_repo.count_search(search);
        write_json(res, 200, envelope_list(to_json_list(items), total, pg.limit, pg.offset));
        return res;
    });

    // GET /api/v1/members/<id>
    CROW_ROUTE(app, "/api/v1/members/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "member not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*m)));
        return res;
    });

    // POST /api/v1/members - create (write scope; role is not settable here, only via PUT+admin)
    CROW_ROUTE(app, "/api/v1/members").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        if (body.has("role")) {
            if (!require_api_scope(req, res, app, "admin")) return res;
        }

        Member m;
        if (body.has("discord_user_id"))  m.discord_user_id  = body["discord_user_id"].s();
        if (body.has("discord_username")) m.discord_username = body["discord_username"].s();
        if (body.has("first_name"))       m.first_name       = body["first_name"].s();
        if (body.has("last_name"))        m.last_name        = body["last_name"].s();
        if (body.has("email"))            m.email            = body["email"].s();
        if (body.has("phone"))            m.phone            = body["phone"].s();
        if (body.has("address_line1"))    m.address_line1    = body["address_line1"].s();
        if (body.has("address_line2"))    m.address_line2    = body["address_line2"].s();
        if (body.has("city"))             m.city             = body["city"].s();
        if (body.has("state"))            m.state            = body["state"].s();
        if (body.has("zip"))              m.zip              = body["zip"].s();
        if (body.has("birthday"))         m.birthday         = body["birthday"].s();
        if (body.has("fol_status"))       m.fol_status       = body["fol_status"].s();
        if (body.has("is_paid"))          m.is_paid          = body["is_paid"].b();
        if (body.has("paid_until"))       m.paid_until       = body["paid_until"].s();
        if (body.has("role"))             m.role             = body["role"].s();

        try {
            auto created = members.create(m);
            // chapter_id lives in chapter_members, not the members table itself -
            // members.create() can't set it (there's no such column to INSERT), so
            // it needs its own call, same as the PUT handler below.
            if (body.has("chapter_id")) {
                members.set_chapter(created.id, body["chapter_id"].i());
                auto refetched = members.get(created.id);
                if (refetched) created = *refetched;
            }
            audit.log_system("member.create", "member", created.id, created.display_name,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[MembersApiRoutes] POST /api/v1/members error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create member", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/members/<id> - partial update. Requires admin scope if "role" is present.
    CROW_ROUTE(app, "/api/v1/members/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = members.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "member not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        if (body.has("role")) {
            if (!require_api_scope(req, res, app, "admin")) return res;
        }

        Member updates = *existing;
        if (body.has("first_name"))       updates.first_name       = body["first_name"].s();
        if (body.has("last_name"))        updates.last_name        = body["last_name"].s();
        if (body.has("email"))            updates.email            = body["email"].s();
        if (body.has("phone"))            updates.phone            = body["phone"].s();
        if (body.has("address_line1"))    updates.address_line1    = body["address_line1"].s();
        if (body.has("address_line2"))    updates.address_line2    = body["address_line2"].s();
        if (body.has("city"))             updates.city             = body["city"].s();
        if (body.has("state"))            updates.state            = body["state"].s();
        if (body.has("zip"))              updates.zip              = body["zip"].s();
        if (body.has("birthday"))         updates.birthday         = body["birthday"].s();
        if (body.has("fol_status"))       updates.fol_status       = body["fol_status"].s();
        if (body.has("is_paid"))          updates.is_paid          = body["is_paid"].b();
        if (body.has("paid_until"))       updates.paid_until       = body["paid_until"].s();
        if (body.has("role"))             updates.role             = body["role"].s();
        if (body.has("sharing_email"))    updates.sharing_email    = body["sharing_email"].s();
        if (body.has("sharing_phone"))    updates.sharing_phone    = body["sharing_phone"].s();
        if (body.has("sharing_address"))  updates.sharing_address  = body["sharing_address"].s();
        if (body.has("sharing_birthday")) updates.sharing_birthday = body["sharing_birthday"].s();
        if (body.has("sharing_discord"))  updates.sharing_discord  = body["sharing_discord"].s();

        try {
            auto updated = members.update(static_cast<int64_t>(id), updates);
            // chapter_id goes through its own dedicated call, not update() -
            // MemberService::update() never touches chapter_id at all (same
            // pattern the browser member-edit form already follows).
            if (body.has("chapter_id")) {
                members.set_chapter(static_cast<int64_t>(id), body["chapter_id"].i());
                auto refetched = members.get(static_cast<int64_t>(id));
                if (refetched) updated = *refetched;
            }
            audit.log_system("member.update", "member", updated.id, updated.display_name,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updated)));
        } catch (const std::exception& ex) {
            std::cerr << "[MembersApiRoutes] PUT /api/v1/members/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update member", "validation_error");
        }
        return res;
    });

    // POST /api/v1/members/<id>/link-discord - attach a Discord identity to an
    // existing member. Deliberately narrow (mirrors the browser's Discord
    // Matches review action and MemberRepository::link_discord_id's own
    // comment) - does NOT go through update(), so it can't accidentally
    // overwrite unrelated fields. This is the correct way to merge a
    // Discord-only member-sync duplicate onto an existing hand-entered
    // member record: link here, then DELETE the duplicate.
    CROW_ROUTE(app, "/api/v1/members/<int>/link-discord").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = members.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "member not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body || !body.has("discord_user_id")) {
            envelope_error(res, 400, "discord_user_id is required", "invalid_request");
            return res;
        }
        std::string discord_user_id = body["discord_user_id"].s();
        std::string discord_username = body.has("discord_username") ? std::string(body["discord_username"].s()) : "";

        bool ok = member_repo.link_discord_id(static_cast<int64_t>(id), discord_user_id, discord_username);
        if (!ok) { envelope_error(res, 400, "could not link Discord identity", "validation_error"); return res; }

        auto updated = members.get(static_cast<int64_t>(id));
        audit.log_system("member.link_discord", "member", id, updated ? updated->display_name : "",
                          "Linked Discord " + discord_user_id + " via " +
                          actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
        write_json(res, 200, envelope_ok(updated ? to_json(*updated) : crow::json::wvalue{}));
        return res;
    });

    // DELETE /api/v1/members/<id> - admin scope
    CROW_ROUTE(app, "/api/v1/members/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "member not found", "not_found"); return res; }

        try {
            members.delete_member(static_cast<int64_t>(id));
            audit.log_system("member.delete", "member", m->id, m->display_name,
                              "Deleted via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[MembersApiRoutes] DELETE /api/v1/members/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 500, "could not delete member", "internal_error");
        }
        return res;
    });
}
