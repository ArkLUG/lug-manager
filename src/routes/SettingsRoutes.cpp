#include "routes/SettingsRoutes.hpp"
#include <crow/mustache.h>
#include <sstream>

void register_settings_routes(LugApp& app, SettingsRepository& settings,
                               DiscordClient& discord, MemberSyncService& member_sync,
                               CalendarGenerator& calendar, GoogleCalendarClient& gcal,
                               EventService& events, MeetingService& meetings,
                               MemberService& members) {

    // GET /api/discord/channel-options
    // Returns <option> elements for all text channels in the configured guild.
    // Query param: selected=<channel_id>  (optional, marks that option as selected)
    // Query param: guild_id=<id>          (optional, uses this guild instead of the stored one)
    CROW_ROUTE(app, "/api/discord/channel-options")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            return res;
        }

        std::string selected;
        std::string override_guild;
        {
            auto qs = crow::query_string(req.url_params);
            const char* s = qs.get("selected");
            if (s) selected = s;
            const char* g = qs.get("guild_id");
            if (g) override_guild = g;
        }

        // Temporarily use an override guild_id if provided (for live preview when user types)
        std::string saved_guild = discord.get_guild_id();
        if (!override_guild.empty() && override_guild != saved_guild) {
            discord.reconfigure(override_guild, "");
        }

        auto channels = discord.fetch_text_channels();

        // Restore original guild if we overrode it
        if (!override_guild.empty() && override_guild != saved_guild) {
            discord.reconfigure(saved_guild, "");
        }

        std::ostringstream html;
        html << "<option value=\"\">-- Select a channel --</option>\n";
        for (auto& ch : channels) {
            html << "<option value=\"" << ch.id << "\"";
            if (ch.id == selected) html << " selected";
            html << ">#" << ch.name << "</option>\n";
        }

        if (channels.empty()) {
            html.str("");
            html << "<option value=\"\">No text channels found (check guild ID &amp; bot permissions)</option>";
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /api/discord/forum-options?selected=<channel_id>
    // Returns <option> elements for all forum channels in the configured guild.
    CROW_ROUTE(app, "/api/discord/forum-options")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.code = 403; return res; }

        std::string selected;
        {
            auto qs = crow::query_string(req.url_params);
            const char* s = qs.get("selected");
            if (s) selected = s;
        }

        auto channels = discord.fetch_forum_channels();
        std::ostringstream html;
        html << "<option value=\"\">-- Select a forum channel --</option>\n";
        for (auto& ch : channels) {
            html << "<option value=\"" << ch.id << "\"";
            if (ch.id == selected) html << " selected";
            html << ">#" << ch.name << "</option>\n";
        }
        if (channels.empty()) {
            html.str("");
            html << "<option value=\"\">No forum channels found (check guild ID &amp; bot permissions)</option>";
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // POST /api/discord/test-announcement - send a test message to the LUG-wide channel
    CROW_ROUTE(app, "/api/discord/test-announcement").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            return res;
        }

        std::string channel_id = discord.get_lug_channel_id();
        if (channel_id.empty()) {
            res.write(R"(<span class="text-red-600">No announcements channel configured.</span>)");
            return res;
        }

        try {
            discord.post_message(channel_id, "🧱 **LUG Manager test announcement** — bot is connected and posting correctly!");
            res.write(R"(<span class="text-green-600 font-medium">✓ Test message sent successfully!</span>)");
        } catch (const std::exception& e) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(e.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // GET /api/discord/role-options - returns <option> elements for all guild roles
    CROW_ROUTE(app, "/api/discord/role-options")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.code = 403; return res; }

        std::string selected;
        {
            auto qs = crow::query_string(req.url_params);
            const char* s = qs.get("selected"); if (s) selected = s;
        }

        auto roles = discord.fetch_guild_roles();
        std::ostringstream html;
        html << "<option value=\"\">-- No role --</option>\n";
        for (auto& r : roles)
            html << "<option value=\"" << r.id << "\""
                 << (r.id == selected ? " selected" : "") << ">@" << r.name << "</option>\n";
        if (roles.empty())
            html.str("<option value=\"\">No roles found (check guild ID &amp; bot permissions)</option>");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // POST /api/discord/sync-members - import guild members that have a mapped LUG role
    CROW_ROUTE(app, "/api/discord/sync-members").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            return res;
        }

        SyncResult r = member_sync.sync_from_guild();

        std::ostringstream html;
        if (!r.error_message.empty()) {
            html << "<span class=\"text-red-600 font-medium\">Error: "
                 << r.error_message << "</span>";
        } else {
            html << "<span class=\"text-green-700 font-medium\">"
                 << "Sync complete &mdash; "
                 << r.imported << " imported, "
                 << r.updated  << " updated, "
                 << r.skipped  << " skipped";
            if (r.errors > 0)
                html << ", <span class=\"text-red-600\">" << r.errors << " errors</span>";
            html << "</span>";
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /settings - admin settings page
    CROW_ROUTE(app, "/settings")([&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.redirect("/dashboard");
            return res;
        }

        std::string guild_id           = settings.get("discord_guild_id",    discord.get_guild_id());
        std::string lug_channel        = settings.get("discord_announcements_channel_id",
                                                       discord.get_lug_channel_id());
        std::string forum_channel      = settings.get("discord_events_forum_channel_id",
                                                       discord.get_events_forum_channel_id());
        std::string announce_role_id   = settings.get("discord_announcement_role_id",
                                                       discord.get_announcement_role_id());
        std::string non_lug_role_id    = settings.get("discord_non_lug_event_role_id",
                                                       discord.get_non_lug_event_role_id());
        std::string timezone           = settings.get("lug_timezone", discord.get_timezone());
        std::string calendar_name     = settings.get("ical_calendar_name", "LUG Events");
        std::string suppress_pings    = settings.get("discord_suppress_pings");
        std::string suppress_updates  = settings.get("discord_suppress_updates");
        std::string gcal_sa_path      = settings.get("google_service_account_json_path");
        std::string gcal_cal_id       = settings.get("google_calendar_id");

        auto build_options = [](const std::vector<DiscordChannel>& channels,
                                const std::string& selected,
                                const std::string& empty_label,
                                const std::string& none_msg) -> std::string {
            std::ostringstream oss;
            oss << "<option value=\"\">" << empty_label << "</option>\n";
            for (auto& ch : channels) {
                oss << "<option value=\"" << ch.id << "\"";
                if (ch.id == selected) oss << " selected";
                oss << ">#" << ch.name << "</option>\n";
            }
            if (channels.empty()) { oss.str(""); oss << "<option value=\"\">" << none_msg << "</option>"; }
            return oss.str();
        };

        auto build_role_options = [](const std::vector<DiscordRole>& roles,
                                     const std::string& selected) -> std::string {
            std::ostringstream oss;
            oss << "<option value=\"\">-- No role --</option>\n";
            for (auto& r : roles)
                oss << "<option value=\"" << r.id << "\""
                    << (r.id == selected ? " selected" : "") << ">@" << r.name << "</option>\n";
            if (roles.empty())
                return "<option value=\"\">No roles found (check guild ID &amp; bot permissions)</option>";
            return oss.str();
        };

        std::string no_guild = "Enter a Guild ID first, then refresh";
        std::string channel_options = guild_id.empty() ? "<option value=\"\">" + no_guild + "</option>"
            : build_options(discord.fetch_text_channels(), lug_channel,
                            "-- Select a channel --",
                            "No text channels found (check guild ID &amp; bot permissions)");
        std::string forum_options = guild_id.empty() ? "<option value=\"\">" + no_guild + "</option>"
            : build_options(discord.fetch_forum_channels(), forum_channel,
                            "-- Select a forum channel --",
                            "No forum channels found (check guild ID &amp; bot permissions)");
        auto all_roles = guild_id.empty() ? std::vector<DiscordRole>{} : discord.fetch_guild_roles();
        std::string role_options = guild_id.empty()
            ? "<option value=\"\">" + no_guild + "</option>"
            : build_role_options(all_roles, announce_role_id);
        std::string non_lug_role_options = guild_id.empty()
            ? "<option value=\"\">" + no_guild + "</option>"
            : build_role_options(all_roles, non_lug_role_id);

        crow::mustache::context mctx;
        mctx["guild_id"]              = guild_id;
        mctx["lug_channel_id"]        = lug_channel;
        mctx["forum_channel_id"]      = forum_channel;
        mctx["channel_options"]       = channel_options;
        mctx["forum_options"]         = forum_options;
        mctx["role_options"]          = role_options;
        mctx["non_lug_role_options"]  = non_lug_role_options;
        mctx["timezone"]              = timezone;
        mctx["calendar_name"]        = calendar_name;
        mctx["google_sa_path"]       = gcal_sa_path;
        mctx["google_calendar_id"]   = gcal_cal_id;
        mctx["suppress_pings"]       = (suppress_pings == "1");
        mctx["suppress_updates"]     = (suppress_updates == "1");
        mctx["gcal_configured"]      = gcal.is_configured();

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("settings/_content.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("settings/_content.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = "Settings";
            layout_ctx["active_settings"] = true;
            layout_ctx["is_admin"]        = true;
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /settings - save guild and LUG-wide channel
    CROW_ROUTE(app, "/settings").methods("POST"_method)(
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

        std::string guild_id       = get_param("discord_guild_id");
        std::string lug_channel    = get_param("discord_announcements_channel_id");
        std::string forum_channel  = get_param("discord_events_forum_channel_id");
        std::string announce_role  = get_param("discord_announcement_role_id");
        std::string non_lug_role   = get_param("discord_non_lug_event_role_id");
        std::string timezone       = get_param("lug_timezone");
        std::string cal_name       = get_param("ical_calendar_name");
        std::string suppress_pings   = get_param("discord_suppress_pings");
        std::string suppress_updates = get_param("discord_suppress_updates");
        std::string gcal_sa_path   = get_param("google_service_account_json_path");
        std::string gcal_cal_id    = get_param("google_calendar_id");

        if (!guild_id.empty())    settings.set("discord_guild_id",    guild_id);
        if (!lug_channel.empty()) settings.set("discord_announcements_channel_id", lug_channel);
        settings.set("discord_events_forum_channel_id", forum_channel); // allow clearing
        settings.set("discord_announcement_role_id",    announce_role); // allow clearing
        settings.set("discord_non_lug_event_role_id",   non_lug_role);  // allow clearing
        if (!timezone.empty())    settings.set("lug_timezone",         timezone);
        if (!cal_name.empty())    settings.set("ical_calendar_name",   cal_name);

        // Checkboxes: absent from form = unchecked = "0"
        settings.set("discord_suppress_pings", suppress_pings == "1" ? "1" : "0");
        settings.set("discord_suppress_updates", suppress_updates == "1" ? "1" : "0");

        // Apply immediately so Discord calls use the new values
        discord.reconfigure(guild_id, lug_channel, forum_channel, announce_role, non_lug_role, timezone);
        discord.set_suppress_pings(suppress_pings == "1");
        discord.set_suppress_updates(suppress_updates == "1");
        if (!timezone.empty()) calendar.set_timezone(timezone);
        settings.set("google_service_account_json_path", gcal_sa_path);
        settings.set("google_calendar_id",               gcal_cal_id);
        if (!gcal_sa_path.empty() && !gcal_cal_id.empty())
            gcal.reconfigure(gcal_sa_path, gcal_cal_id, timezone);

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.add_header("HX-Redirect", "/settings");
            res.code = 200;
        } else {
            res.redirect("/settings");
        }
        return res;
    });

    // POST /api/google-calendar/import - import upcoming events from Google Calendar
    CROW_ROUTE(app, "/api/google-calendar/import").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        if (!gcal.is_configured()) {
            res.write(R"(<span class="text-red-600">Google Calendar not configured.</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        try {
            auto gcal_events = gcal.fetch_upcoming_events(100);
            int ev_imported = 0, mtg_imported = 0, skipped = 0;
            for (auto& ge : gcal_events) {
                if (events.exists_by_google_calendar_id(ge.google_id) ||
                    meetings.exists_by_google_calendar_id(ge.google_id)) {
                    ++skipped;
                    continue;
                }
                if (ge.is_all_day) {
                    // All-day → Event
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
                    // Timed → Meeting
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
            std::ostringstream html;
            html << "<span class=\"text-green-700 font-medium\">"
                 << ev_imported << " events, " << mtg_imported << " meetings imported, "
                 << skipped << " already existed</span>";
            res.write(html.str());
        } catch (const std::exception& ex) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(ex.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /api/google-calendar/sync-all - push all events and meetings to Google Calendar
    CROW_ROUTE(app, "/api/google-calendar/sync-all").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        if (!gcal.is_configured()) {
            res.write(R"(<span class="text-red-600">Google Calendar not configured.</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        try {
            auto ev_result  = events.sync_all_to_google_calendar();
            auto mtg_result = meetings.sync_all_to_google_calendar();

            std::ostringstream html;
            html << "<span class=\"text-green-700 font-medium\">"
                 << "Events: " << ev_result.created << " created, " << ev_result.synced << " updated"
                 << (ev_result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(ev_result.errors) + " errors</span>" : "")
                 << " · Meetings: " << mtg_result.created << " created, " << mtg_result.synced << " updated"
                 << (mtg_result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(mtg_result.errors) + " errors</span>" : "")
                 << "</span>";
            res.write(html.str());
        } catch (const std::exception& ex) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(ex.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /api/discord/sync-all - force-push all events and meetings to Discord
    CROW_ROUTE(app, "/api/discord/sync-all").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        try {
            auto ev_result  = events.sync_all_to_discord();
            auto mtg_result = meetings.sync_all_to_discord();
            auto nick_result = members.sync_nicknames_to_discord();

            std::ostringstream html;
            html << "<span class=\"text-green-700 font-medium\">"
                 << "Events: " << ev_result.synced << " synced"
                 << (ev_result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(ev_result.errors) + " errors</span>" : "")
                 << " · Meetings: " << mtg_result.synced << " synced"
                 << (mtg_result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(mtg_result.errors) + " errors</span>" : "")
                 << " · Nicknames: " << nick_result.synced << " updated"
                 << (nick_result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(nick_result.errors) + " errors</span>" : "")
                 << "</span>";
            res.write(html.str());
        } catch (const std::exception& ex) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(ex.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /api/members/regenerate-nicknames - regenerate display names from first/last
    CROW_ROUTE(app, "/api/members/regenerate-nicknames").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto result = members.regenerate_all_nicknames();
        std::ostringstream html;
        html << "<span class=\"text-green-700 font-medium\">"
             << result.updated << " nicknames regenerated, "
             << result.skipped << " skipped (no first/last name or unchanged)"
             << "</span>";
        res.write(html.str());
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /api/discord/sync-nicknames - sync all member nicknames to Discord
    CROW_ROUTE(app, "/api/discord/sync-nicknames").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        try {
            auto result = members.sync_nicknames_to_discord();
            std::ostringstream html;
            html << "<span class=\"text-green-700 font-medium\">"
                 << "Nicknames: " << result.synced << " updated, "
                 << result.skipped << " skipped"
                 << (result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(result.errors) + " errors</span>" : "")
                 << "</span>";
            res.write(html.str());
        } catch (const std::exception& ex) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(ex.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });
}
