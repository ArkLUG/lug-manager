#include "routes/api/EventsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include "routes/api/Serialize.hpp"
#include <crow.h>
#include <iostream>
#include <sstream>

// ev_normalize_datetime mirrors EventRoutes.cpp's own normalization for start/end/signup
// times; kept local (not shared) since the two files' normalization needs are identical
// but this file should not depend on EventRoutes.cpp's internals.
static std::string api_ev_normalize_datetime(const std::string& dt) {
    if (dt.size() == 16) return dt + ":00"; // "YYYY-MM-DDTHH:MM" -> add seconds
    return dt;
}

void register_events_api_routes(LugApp& app, EventService& events, MeetingService& meetings,
                                 EventDayRepository& event_days,
                                 EventDayAttendanceRepository& event_day_attendance,
                                 AttendanceRepository& attendance_flat,
                                 ChapterService& chapters, DiscordClient& discord,
                                 AuditService& audit) {

    // GET /api/v1/events - paginated list
    CROW_ROUTE(app, "/api/v1/events").methods("GET"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto pg = parse_pagination(req, 25, 200);
        const char* search_p = req.url_params.get("search");
        std::string search = search_p ? search_p : "";
        const char* upcoming_p = req.url_params.get("upcoming_only");
        bool upcoming_only = !upcoming_p || std::string(upcoming_p) != "false";

        auto items = events.list_paginated(search, pg.limit, pg.offset, upcoming_only);
        int total = events.count_filtered(search, upcoming_only);
        write_json(res, 200, envelope_list(to_json_list(items), total, pg.limit, pg.offset));
        return res;
    });

    // GET /api/v1/events/<id>
    CROW_ROUTE(app, "/api/v1/events/<int>").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { envelope_error(res, 404, "event not found", "not_found"); return res; }
        write_json(res, 200, envelope_ok(to_json(*ev)));
        return res;
    });

    // GET /api/v1/events/<id>/days - read-only, event days are server-derived (see plan 0.6)
    CROW_ROUTE(app, "/api/v1/events/<int>/days").methods("GET"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;

        auto days = event_days.find_by_event(static_cast<int64_t>(id));
        write_json(res, 200, envelope_ok(to_json_list(days)));
        return res;
    });

    // POST /api/v1/events - create
    CROW_ROUTE(app, "/api/v1/events").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        LugEvent e;
        if (body.has("title"))            e.title            = body["title"].s();
        if (body.has("description"))      e.description      = body["description"].s();
        if (body.has("location"))         e.location         = body["location"].s();
        if (body.has("start_time"))       e.start_time       = api_ev_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))         e.end_time         = api_ev_normalize_datetime(body["end_time"].s());
        if (body.has("signup_deadline"))  e.signup_deadline  = api_ev_normalize_datetime(body["signup_deadline"].s());
        if (body.has("max_attendees"))    e.max_attendees    = static_cast<int>(body["max_attendees"].i());
        if (body.has("scope"))            e.scope            = body["scope"].s();
        if (body.has("chapter_id"))       e.chapter_id       = body["chapter_id"].i();
        if (body.has("event_lead_id"))    e.event_lead_id    = body["event_lead_id"].i();
        if (body.has("entrance_fee"))     e.entrance_fee     = body["entrance_fee"].s();
        if (body.has("notes"))            e.notes            = body["notes"].s();
        if (body.has("discord_ping_role_ids")) e.discord_ping_role_ids = body["discord_ping_role_ids"].s();
        if (body.has("suppress_discord"))  e.suppress_discord  = body["suppress_discord"].b();
        if (body.has("suppress_calendar")) e.suppress_calendar = body["suppress_calendar"].b();
        if (body.has("public_kids"))       e.public_kids       = static_cast<int>(body["public_kids"].i());
        if (body.has("public_teens"))      e.public_teens      = static_cast<int>(body["public_teens"].i());
        if (body.has("public_adults"))     e.public_adults     = static_cast<int>(body["public_adults"].i());
        if (body.has("social_media_links")) e.social_media_links = body["social_media_links"].s();
        if (body.has("event_feedback"))    e.event_feedback    = body["event_feedback"].s();

        try {
            auto created = events.create(e);
            audit.log_system("event.create", "event", created.id, created.title,
                              "Created via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 201, envelope_ok(to_json(created)));
        } catch (const std::exception& ex) {
            std::cerr << "[EventsApiRoutes] POST /api/v1/events error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not create event", "validation_error");
        }
        return res;
    });

    // PUT /api/v1/events/<id> - partial update
    CROW_ROUTE(app, "/api/v1/events/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto existing = events.get(static_cast<int64_t>(id));
        if (!existing) { envelope_error(res, 404, "event not found", "not_found"); return res; }

        auto body = crow::json::load(req.body);
        if (!body) { envelope_error(res, 400, "invalid JSON body", "invalid_request"); return res; }

        LugEvent updates = *existing;
        if (body.has("title"))            updates.title            = body["title"].s();
        if (body.has("description"))      updates.description      = body["description"].s();
        if (body.has("location"))         updates.location         = body["location"].s();
        if (body.has("start_time"))       updates.start_time       = api_ev_normalize_datetime(body["start_time"].s());
        if (body.has("end_time"))         updates.end_time         = api_ev_normalize_datetime(body["end_time"].s());
        if (body.has("signup_deadline"))  updates.signup_deadline  = api_ev_normalize_datetime(body["signup_deadline"].s());
        if (body.has("max_attendees"))    updates.max_attendees    = static_cast<int>(body["max_attendees"].i());
        if (body.has("scope"))            updates.scope            = body["scope"].s();
        if (body.has("chapter_id"))       updates.chapter_id       = body["chapter_id"].i();
        if (body.has("event_lead_id"))    updates.event_lead_id    = body["event_lead_id"].i();
        if (body.has("entrance_fee"))     updates.entrance_fee     = body["entrance_fee"].s();
        if (body.has("notes"))            updates.notes            = body["notes"].s();
        if (body.has("status"))           updates.status           = body["status"].s();
        if (body.has("discord_ping_role_ids")) updates.discord_ping_role_ids = body["discord_ping_role_ids"].s();
        if (body.has("suppress_discord"))  updates.suppress_discord  = body["suppress_discord"].b();
        if (body.has("suppress_calendar")) updates.suppress_calendar = body["suppress_calendar"].b();
        if (body.has("public_kids"))       updates.public_kids       = static_cast<int>(body["public_kids"].i());
        if (body.has("public_teens"))      updates.public_teens      = static_cast<int>(body["public_teens"].i());
        if (body.has("public_adults"))     updates.public_adults     = static_cast<int>(body["public_adults"].i());
        if (body.has("social_media_links")) updates.social_media_links = body["social_media_links"].s();
        if (body.has("event_feedback"))    updates.event_feedback    = body["event_feedback"].s();

        try {
            auto updated = events.update(static_cast<int64_t>(id), updates);
            audit.log_system("event.update", "event", updated.id, updated.title,
                              "Updated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            write_json(res, 200, envelope_ok(to_json(updated)));
        } catch (const std::exception& ex) {
            std::cerr << "[EventsApiRoutes] PUT /api/v1/events/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not update event", "validation_error");
        }
        return res;
    });

    // POST /api/v1/events/<id>/checkin-token - generate (or return existing) QR check-in token.
    // Mirrors the browser's /events/<id>/generate-checkin action; the QR image itself is just
    // this token rendered client-side (window.location.origin + "/checkin/" + token), so
    // returning the raw token is all an API consumer needs.
    CROW_ROUTE(app, "/api/v1/events/<int>/checkin-token").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { envelope_error(res, 404, "event not found", "not_found"); return res; }

        std::string token = ev->checkin_token;
        if (token.empty()) {
            token = EventService::generate_uuid();
            events.repo().update_checkin_token(ev->id, token);
        }

        audit.log_system("event.generate_checkin_token", "event", ev->id, ev->title,
                          "Generated via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
        crow::json::wvalue body_out;
        body_out["id"]            = ev->id;
        body_out["checkin_token"] = token;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // POST /api/v1/events/<id>/publish-report - build the same markdown report the browser's
    // /events/<id>/publish-report route builds, and post/update it in the event-reports
    // Discord forum. Mirrors that route's logic exactly (see EventRoutes.cpp) rather than
    // sharing code across the two files, matching this codebase's existing convention for
    // route-local helpers (e.g. api_ev_normalize_datetime above).
    CROW_ROUTE(app, "/api/v1/events/<int>/publish-report").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "write")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { envelope_error(res, 404, "event not found", "not_found"); return res; }

        std::string chapter_name;
        if (ev->chapter_id > 0) {
            auto ch = chapters.get(ev->chapter_id);
            if (ch) chapter_name = ch->name;
        }

        std::ostringstream report;
        report << "**Event name:** " << ev->title << "\n";
        report << "**Chapter:** " << (chapter_name.empty() ? "LUG Wide" : chapter_name) << "\n";
        report << "**Start date:** " << ev->start_time.substr(0, 10) << "\n";
        report << "**End date:** " << ev->end_time.substr(0, 10) << "\n";
        if (!ev->location.empty())
            report << "**Location:** " << ev->location << "\n";
        if (!ev->event_lead_name.empty())
            report << "**Lead:** " << ev->event_lead_name << "\n";
        if (!ev->entrance_fee.empty())
            report << "**Entrance fee:** " << ev->entrance_fee << "\n";

        auto days = event_days.find_by_event(ev->id);
        for (const auto& d : days) {
            auto rows = event_day_attendance.find_by_day(d.id);
            report << "**Member names day" << d.day_number << ":**\n";
            if (rows.empty()) {
                report << "- (none)\n";
            } else {
                for (const auto& r : rows) {
                    report << "- " << r.member_display_name << "\n";
                }
            }
        }

        report << "**Public kids:** " << ev->public_kids << "\n";
        report << "**Public teens:** " << ev->public_teens << "\n";
        report << "**Public adults:** " << ev->public_adults << "\n";
        if (!ev->social_media_links.empty())
            report << "**Social media links, ArkLUG mentions, announcements for show:** " << ev->social_media_links << "\n";
        if (!ev->event_feedback.empty())
            report << "**What you liked best about event:** " << ev->event_feedback << "\n";
        if (!ev->description.empty())
            report << "\n## Description\n" << ev->description << "\n";
        if (!ev->notes.empty())
            report << "\n## Notes\n" << ev->notes << "\n";

        std::string forum_id = discord.get_event_reports_forum_id();
        if (forum_id.empty()) forum_id = discord.get_events_forum_channel_id();

        std::string thread_id = discord.publish_report_to_forum(
            forum_id, ev->notes_discord_post_id, "Report: " + ev->title, report.str());

        if (!thread_id.empty() && thread_id != ev->notes_discord_post_id) {
            events.repo().update_notes_discord_post_id(ev->id, thread_id);
        }

        audit.log_system("event.publish_report", "event", ev->id, ev->title,
                          "Published via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
        crow::json::wvalue body_out;
        body_out["id"]              = ev->id;
        body_out["discord_thread_id"] = thread_id;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // POST /api/v1/events/<id>/convert-to-meeting - create a meeting from this event's data,
    // copy its attendance, then cancel (delete) the event. Mirrors the browser's
    // /events/<id>/convert-to-meeting route. Admin scope (destructive, deletes the event).
    CROW_ROUTE(app, "/api/v1/events/<int>/convert-to-meeting").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { envelope_error(res, 404, "event not found", "not_found"); return res; }

        try {
            Meeting m;
            m.title       = ev->title;
            m.description = ev->description;
            m.location    = ev->location;
            m.start_time  = ev->start_time;
            m.end_time    = ev->end_time;
            m.status      = "scheduled";
            m.scope       = ev->scope;
            m.chapter_id  = ev->chapter_id;

            Meeting created = meetings.create(m);

            // Mirrors EventRoutes.cpp exactly: one flat check-in per event_day_attendance
            // row (i.e. per day attended), not deduplicated by member - same as the
            // browser route's existing, shipped behavior.
            auto attendees = event_day_attendance.find_by_event(id);
            for (const auto& a : attendees) {
                attendance_flat.check_in(a.member_id, "meeting", created.id, a.notes, false);
            }

            events.cancel(static_cast<int64_t>(id));

            audit.log_system("event.convert_to_meeting", "event", id, ev->title,
                              "Converted to meeting " + std::to_string(created.id) +
                              " via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            crow::json::wvalue body_out;
            body_out["meeting_id"] = created.id;
            write_json(res, 200, envelope_ok(std::move(body_out)));
        } catch (const std::exception& ex) {
            std::cerr << "[EventsApiRoutes] POST /api/v1/events/" << id << "/convert-to-meeting error: " << ex.what() << "\n";
            envelope_error(res, 400, "could not convert event to meeting", "validation_error");
        }
        return res;
    });

    // DELETE /api/v1/events/<id> - cancel (admin scope: destructive, has Discord/calendar side effects)
    CROW_ROUTE(app, "/api/v1/events/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { envelope_error(res, 404, "event not found", "not_found"); return res; }

        try {
            events.cancel(static_cast<int64_t>(id));
            audit.log_system("event.delete", "event", ev->id, ev->title,
                              "Cancelled via " + actor_label(app.template get_context<ApiKeyMiddleware>(req).api_key));
            res.code = 200;
            res.add_header("Content-Type", "application/json");
            res.write(R"({"data":{"success":true}})");
        } catch (const std::exception& ex) {
            std::cerr << "[EventsApiRoutes] DELETE /api/v1/events/" << id << " error: " << ex.what() << "\n";
            envelope_error(res, 500, "could not cancel event", "internal_error");
        }
        return res;
    });
}
