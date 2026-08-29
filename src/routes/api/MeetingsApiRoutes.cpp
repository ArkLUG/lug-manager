#include "routes/api/MeetingsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>
#include <sstream>

static std::string api_mt_normalize_datetime(const std::string& dt) {
    if (dt.size() == 16) return dt + ":00";
    return dt;
}

void register_meetings_api_routes(LugApp& app, MeetingService& meetings,
                                   AttendanceRepository& attendance,
                                   ChapterService& chapters, DiscordClient& discord,
                                   AuditService& audit) {

    // GET /api/v1/meetings - paginated list
    CROW_ROUTE(app, "/api/v1/meetings").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pg = parse_pagination(req, 25, 200);
        const char* search_p = req.url_params.get("search");
        std::string search = search_p ? search_p : "";

        auto items = meetings.list_paginated(search, pg.limit, pg.offset);
        int total = meetings.count_filtered(search);
        write_json(res, 200, envelope_list(to_json_list(items), total, pg.limit, pg.offset));
        return res;
    });

    // GET /api/v1/meetings/<id>
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*m)));
        return res;
    });

    // POST /api/v1/meetings - create
    CROW_ROUTE(app, "/api/v1/meetings").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Meeting m;
        if (body.has("title"))       m.title       = body["title"].s();
        if (body.has("description")) m.description = body["description"].s();
        if (body.has("location"))    m.location    = body["location"].s();
        if (body.has("start_time"))  m.start_time  = api_mt_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))    m.end_time    = api_mt_normalize_datetime(body["end_time"].s());
        if (body.has("scope"))       m.scope       = body["scope"].s();
        if (body.has("chapter_id"))  m.chapter_id  = body["chapter_id"].i();
        if (body.has("is_virtual"))  m.is_virtual  = body["is_virtual"].b();
        if (body.has("discord_voice_channel_id")) m.discord_voice_channel_id = body["discord_voice_channel_id"].s();
        if (body.has("notes"))              m.notes              = body["notes"].s();
        if (body.has("suppress_discord"))   m.suppress_discord   = body["suppress_discord"].b();
        if (body.has("suppress_calendar"))  m.suppress_calendar  = body["suppress_calendar"].b();
        if (body.has("is_private"))         m.is_private         = body["is_private"].b();
        if (body.has("excludes_perks"))     m.excludes_perks     = body["excludes_perks"].b();

        try {
            auto created = meetings.create(m);
            audit.log_system("meeting.create", "meeting", created.id, created.title,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] POST /api/v1/meetings error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create meeting", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/meetings/<id> - partial update
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = meetings.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        Meeting updates = *existing;
        if (body.has("title"))       updates.title       = body["title"].s();
        if (body.has("description")) updates.description = body["description"].s();
        if (body.has("location"))    updates.location    = body["location"].s();
        if (body.has("start_time"))  updates.start_time  = api_mt_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))    updates.end_time    = api_mt_normalize_datetime(body["end_time"].s());
        if (body.has("scope"))       updates.scope       = body["scope"].s();
        if (body.has("chapter_id"))  updates.chapter_id  = body["chapter_id"].i();
        if (body.has("is_virtual"))  updates.is_virtual  = body["is_virtual"].b();
        if (body.has("discord_voice_channel_id")) updates.discord_voice_channel_id = body["discord_voice_channel_id"].s();
        if (body.has("notes"))  updates.notes  = body["notes"].s();
        if (body.has("status")) updates.status = body["status"].s();
        if (body.has("suppress_discord"))  updates.suppress_discord  = body["suppress_discord"].b();
        if (body.has("suppress_calendar")) updates.suppress_calendar = body["suppress_calendar"].b();
        if (body.has("is_private"))        updates.is_private        = body["is_private"].b();
        if (body.has("excludes_perks"))    updates.excludes_perks    = body["excludes_perks"].b();

        try {
            auto updated = meetings.update(static_cast<int64_t>(id), updates);
            audit.log_system("meeting.update", "meeting", updated.id, updated.title,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updated)));
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] PUT /api/v1/meetings/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update meeting", "validation_error");
        }
        return res;
    });

    // POST /api/v1/meetings/<id>/checkin-token - generate (or return existing) QR check-in
    // token. Mirrors the browser's /meetings/<id>/generate-checkin action; virtual meetings
    // have no in-person QR flow, same restriction as the browser route.
    CROW_ROUTE(app, "/api/v1/meetings/<int>/checkin-token").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }
        if (m->is_virtual) {
            envelope_error(res, 400, "QR check-in is not available for virtual meetings", "invalid_request");
            return res;
        }

        std::string token = m->checkin_token;
        if (token.empty()) {
            token = MeetingService::generate_uuid();
            meetings.repo().update_checkin_token(m->id, token);
        }

        audit.log_system("meeting.generate_checkin_token", "meeting", m->id, m->title,
                          "Generated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
        crow::json::wvalue body_out;
        body_out["id"]            = m->id;
        body_out["checkin_token"] = token;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // POST /api/v1/meetings/<id>/publish-report - build the same markdown report the
    // browser's /meetings/<id>/publish-report route builds, and post/update it in the
    // meeting-reports Discord forum. Mirrors that route's logic exactly (MeetingRoutes.cpp).
    CROW_ROUTE(app, "/api/v1/meetings/<int>/publish-report").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto mtg = meetings.get(static_cast<int64_t>(id));
        if (!mtg) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }

        auto attendees = attendance.find_by_entity("meeting", mtg->id);

        std::string chapter_name;
        if (mtg->chapter_id > 0) {
            auto ch = chapters.get(mtg->chapter_id);
            if (ch) chapter_name = ch->name;
        }

        std::ostringstream report;
        report << "**Meeting:** " << mtg->title << "\n";
        report << "**Chapter:** " << (chapter_name.empty() ? "LUG Wide" : chapter_name) << "\n";
        report << "**Meeting date:** " << mtg->start_time.substr(0, 10) << "\n";
        if (mtg->is_virtual)
            report << "**Format:** Virtual\n";
        else if (!mtg->location.empty())
            report << "**Location:** " << mtg->location << "\n";

        std::vector<std::string> in_person, virtual_list;
        for (const auto& a : attendees) {
            if (a.is_virtual) virtual_list.push_back(a.member_display_name);
            else              in_person.push_back(a.member_display_name);
        }
        report << "**Members by name:**\n";
        if (in_person.empty() && virtual_list.empty()) {
            report << "- (none)\n";
        } else {
            for (const auto& name : in_person)
                report << "- " << name << "\n";
            for (const auto& name : virtual_list)
                report << "- " << name << " (virtual)\n";
        }

        if (!mtg->description.empty())
            report << "\n## Description\n" << mtg->description << "\n";
        if (!mtg->notes.empty())
            report << "\n## Notes\n" << mtg->notes << "\n";

        std::string forum_id = discord.get_meeting_reports_forum_id();
        if (forum_id.empty()) forum_id = discord.get_events_forum_channel_id();

        std::string thread_id = discord.publish_report_to_forum(
            forum_id, mtg->notes_discord_post_id, "Report: " + mtg->title, report.str());

        if (!thread_id.empty() && thread_id != mtg->notes_discord_post_id) {
            meetings.repo().update_notes_discord_post_id(mtg->id, thread_id);
        }

        audit.log_system("meeting.publish_report", "meeting", mtg->id, mtg->title,
                          "Published via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
        crow::json::wvalue body_out;
        body_out["id"]               = mtg->id;
        body_out["discord_thread_id"] = thread_id;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // DELETE /api/v1/meetings/<id> - admin scope (destructive)
    CROW_ROUTE(app, "/api/v1/meetings/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) { envelope_error(res, 404, "meeting not found", "not_found"); return res; }

        try {
            meetings.cancel(static_cast<int64_t>(id));
            audit.log_system("meeting.delete", "meeting", m->id, m->title,
                              "Cancelled via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[MeetingsApiRoutes] DELETE /api/v1/meetings/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 500, "could not cancel meeting", "internal_error");
        }
        return res;
    });
}
