#include "routes/api/SettingsApiRoutes.hpp"
#include "routes/api/ApiCommon.hpp"
#include <crow.h>

// All verbs require admin scope: settings can include operationally sensitive values
// (e.g. discord_guild_id). See plan Section 0.4's flagged deviation.
//
// No list-all-settings endpoint: SettingsRepository has no enumerate method; key lookups
// are always by known key name in the existing codebase (see plan Section 1.7, item 10).

void register_settings_api_routes(LugApp& app, SettingsRepository& settings,
                                   EventService& events, MeetingService& meetings,
                                   GoogleCalendarClient& gcal, AuditService& audit) {

    // GET /api/v1/settings/<key>
    CROW_ROUTE(app, "/api/v1/settings/<string>").methods("GET"_method)(
        [&](const crow::request& req, std::string key) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        std::string value = settings.get(key, "");
        crow::json::wvalue body;
        body["key"]   = key;
        body["value"] = value;
        write_json(res, 200, envelope_ok(std::move(body)));
        return res;
    });

    // PUT /api/v1/settings/<key>
    CROW_ROUTE(app, "/api/v1/settings/<string>").methods("PUT"_method)(
        [&](const crow::request& req, std::string key) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        auto body = crow::json::load(req.body);
        if (!body || !body.has("value")) {
            envelope_error(res, 400, "value is required", "invalid_request");
            return res;
        }

        std::string value = body["value"].s();
        settings.set(key, value);
        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("settings.update", "settings", 0, key,
                          "Set via " + actor_label(ctx.api_key));

        crow::json::wvalue body_out;
        body_out["key"]   = key;
        body_out["value"] = value;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });

    // POST /api/v1/settings/gcal-import - pull upcoming events from the configured
    // Google Calendar and import any not already linked (by google_calendar_event_id).
    // Mirrors the browser's "Import from Google Calendar" button on the Settings page
    // exactly - same fetch, same all-day-vs-timed -> event-vs-meeting split, same
    // create_imported() call (no Discord/calendar side effects on import, since the
    // event already lives on the calendar being imported from).
    CROW_ROUTE(app, "/api/v1/settings/gcal-import").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;

        if (!gcal.is_configured()) {
            envelope_error(res, 400, "Google Calendar is not configured", "not_configured");
            return res;
        }

        int ev_imported = 0, mtg_imported = 0, skipped = 0;
        try {
            auto gcal_events = gcal.fetch_upcoming_events(100);
            for (auto& ge : gcal_events) {
                if (events.exists_by_google_calendar_id(ge.google_id) ||
                    meetings.exists_by_google_calendar_id(ge.google_id)) {
                    ++skipped;
                    continue;
                }
                if (ge.is_all_day) {
                    LugEvent ev;
                    ev.title       = ge.title;
                    ev.description = ge.description;
                    ev.location    = ge.location;
                    ev.start_time  = ge.start_time;
                    ev.end_time    = ge.end_time;
                    ev.status      = "confirmed";
                    ev.scope       = "lug_wide";
                    ev.google_calendar_event_id = ge.google_id;
                    events.create_imported(ev);
                    ++ev_imported;
                } else {
                    Meeting m;
                    m.title       = ge.title;
                    m.description = ge.description;
                    m.location    = ge.location;
                    m.start_time  = ge.start_time;
                    m.end_time    = ge.end_time;
                    m.status      = "scheduled";
                    m.scope       = "lug_wide";
                    m.google_calendar_event_id = ge.google_id;
                    meetings.create_imported(m);
                    ++mtg_imported;
                }
            }
        } catch (const std::exception& ex) {
            envelope_error(res, 502, std::string("Google Calendar fetch failed: ") + ex.what(), "upstream_error");
            return res;
        }

        auto& ctx = app.template get_context<ApiKeyMiddleware>(req);
        audit.log_system("sync.gcal_import", "settings", 0, "",
                          std::to_string(ev_imported) + " events, " + std::to_string(mtg_imported) +
                          " meetings imported, " + std::to_string(skipped) + " skipped via " +
                          actor_label(ctx.api_key));

        crow::json::wvalue body_out;
        body_out["events_imported"]   = ev_imported;
        body_out["meetings_imported"] = mtg_imported;
        body_out["skipped"]           = skipped;
        write_json(res, 200, envelope_ok(std::move(body_out)));
        return res;
    });
}
