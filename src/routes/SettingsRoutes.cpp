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

    // GET /api/discord/voice-channel-options?selected=<channel_id>
    // Returns <option> elements for all voice channels in the configured guild.
    // Any authenticated user can fetch (needed for meeting form).
    CROW_ROUTE(app, "/api/discord/voice-channel-options")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        std::string selected;
        {
            auto qs = crow::query_string(req.url_params);
            const char* s = qs.get("selected");
            if (s) selected = s;
        }

        auto channels = discord.fetch_voice_channels();
        std::ostringstream html;
        html << "<option value=\"\">-- No voice channel --</option>\n";
        for (auto& ch : channels) {
            html << "<option value=\"" << ch.id << "\"";
            if (ch.id == selected) html << " selected";
            html << ">" << ch.name << "</option>\n";
        }
        if (channels.empty()) {
            html.str("");
            html << "<option value=\"\">No voice channels found</option>";
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
            html << "<div>"
                 << "<span class=\"text-green-700 font-medium\">"
                 << "Sync complete &mdash; "
                 << r.imported << " imported, "
                 << r.updated  << " updated, "
                 << r.skipped  << " skipped";
            if (r.errors > 0)
                html << ", <span class=\"text-red-600\">" << r.errors << " errors</span>";
            html << "</span>";

            if (!r.changes.empty()) {
                html << "<details class=\"mt-3\">"
                     << "<summary class=\"cursor-pointer text-sm text-indigo-600 hover:text-indigo-800 font-medium\">"
                     << "Show details (" << r.changes.size() << " change" << (r.changes.size() != 1 ? "s" : "") << ")"
                     << "</summary>"
                     << "<div class=\"mt-2 border border-gray-200 rounded-lg overflow-hidden\">"
                     << "<table class=\"min-w-full text-xs\">"
                     << "<thead class=\"bg-gray-50\">"
                     << "<tr><th class=\"px-3 py-2 text-left text-gray-600\">Member</th>"
                     << "<th class=\"px-3 py-2 text-left text-gray-600\">Change</th>"
                     << "<th class=\"px-3 py-2 text-left text-gray-600\">From</th>"
                     << "<th class=\"px-3 py-2 text-left text-gray-600\">To</th>"
                     << "<th class=\"px-3 py-2\"></th></tr></thead><tbody>";

                for (const auto& c : r.changes) {
                    html << "<tr class=\"border-t border-gray-100\" id=\"sync-change-" << c.member_id << "-" << c.field << "\">";
                    html << "<td class=\"px-3 py-2 text-gray-900\">" << c.member_name << "</td>";

                    if (c.change_type == "created") {
                        html << "<td class=\"px-3 py-2\"><span class=\"inline-flex items-center px-1.5 py-0.5 rounded bg-green-100 text-green-700\">new member</span></td>"
                             << "<td class=\"px-3 py-2 text-gray-400\">&mdash;</td>"
                             << "<td class=\"px-3 py-2 text-gray-400\">&mdash;</td>"
                             << "<td class=\"px-3 py-2 text-right\">"
                             << "<button hx-post=\"/api/discord/revert-sync-change\" "
                             << "hx-vals='{\"member_id\": " << c.member_id << ", \"change_type\": \"created\"}' "
                             << "hx-target=\"#sync-change-" << c.member_id << "-\" "
                             << "hx-swap=\"outerHTML\" "
                             << "class=\"text-red-600 hover:text-red-800 font-medium\">Delete</button></td>";
                    } else {
                        std::string label = c.field;
                        if (c.field == "discord_username") label = "username";
                        else if (c.field == "display_name") label = "display name";
                        else if (c.field == "chapter_role") label = "chapter role";

                        html << "<td class=\"px-3 py-2\"><span class=\"inline-flex items-center px-1.5 py-0.5 rounded bg-blue-100 text-blue-700\">" << label << "</span></td>"
                             << "<td class=\"px-3 py-2 text-gray-500 font-mono\">" << c.old_value << "</td>"
                             << "<td class=\"px-3 py-2 text-gray-900 font-mono\">" << c.new_value << "</td>"
                             << "<td class=\"px-3 py-2 text-right\">"
                             << "<button hx-post=\"/api/discord/revert-sync-change\" "
                             << "hx-vals='{\"member_id\": " << c.member_id
                             << ", \"change_type\": \"" << c.change_type
                             << "\", \"field\": \"" << c.field
                             << "\", \"old_value\": \"" << c.old_value << "\"}' "
                             << "hx-target=\"#sync-change-" << c.member_id << "-" << c.field << "\" "
                             << "hx-swap=\"outerHTML\" "
                             << "hx-confirm=\"Revert " << label << " for " << c.member_name << "?\" "
                             << "class=\"text-amber-600 hover:text-amber-800 font-medium\">Revert</button></td>";
                    }
                    html << "</tr>";
                }

                html << "</tbody></table></div></details>";
            }
            html << "</div>";
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // POST /api/discord/revert-sync-change - revert a single member sync change
    CROW_ROUTE(app, "/api/discord/revert-sync-change").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            res.code = 403;
            res.write(R"(<span class="text-red-600">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto body = crow::query_string("?" + req.body);
        int64_t member_id = 0;
        std::string change_type, field, old_value;

        if (auto v = body.get("member_id"))  member_id = std::stoll(v);
        if (auto v = body.get("change_type")) change_type = v;
        if (auto v = body.get("field"))       field = v;
        if (auto v = body.get("old_value"))   old_value = v;

        if (member_id == 0) {
            res.write(R"(<tr class="border-t border-gray-100"><td colspan="5" class="px-3 py-2 text-red-600">Invalid member ID</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        auto member = members.get(member_id);

        if (change_type == "created") {
            // Revert creation = delete the member
            members.delete_member(member_id);
            res.write(R"(<tr class="border-t border-gray-100 bg-red-50"><td colspan="5" class="px-3 py-2 text-red-700 text-center">Member deleted</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        if (!member) {
            res.write(R"(<tr class="border-t border-gray-100"><td colspan="5" class="px-3 py-2 text-red-600">Member not found</td></tr>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        Member m = *member;
        if (field == "discord_username")    m.discord_username = old_value;
        else if (field == "display_name")   m.display_name = old_value;
        else if (field == "role")           m.role = old_value;

        members.update(member_id, m);

        std::string label = field;
        if (field == "discord_username") label = "username";
        else if (field == "display_name") label = "display name";

        res.write("<tr class=\"border-t border-gray-100 bg-amber-50\">"
                  "<td colspan=\"5\" class=\"px-3 py-2 text-amber-700 text-center\">"
                  "Reverted " + label + " for " + m.display_name + " back to &ldquo;" + old_value + "&rdquo;"
                  "</td></tr>");
        res.add_header("Content-Type", "text/html; charset=utf-8");
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
        std::string event_reports_forum = settings.get("discord_event_reports_forum_channel_id");
        std::string meeting_reports_forum = settings.get("discord_meeting_reports_forum_channel_id");

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
        mctx["event_reports_forum_id"]   = event_reports_forum;
        mctx["meeting_reports_forum_id"] = meeting_reports_forum;

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
        set_layout_auth(req, app, layout_ctx);
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
        {
            std::string ev_reports = get_param("discord_event_reports_forum_channel_id");
            std::string mtg_reports = get_param("discord_meeting_reports_forum_channel_id");
            settings.set("discord_event_reports_forum_channel_id", ev_reports);
            settings.set("discord_meeting_reports_forum_channel_id", mtg_reports);
            discord.set_event_reports_forum_id(ev_reports);
            discord.set_meeting_reports_forum_id(mtg_reports);
        }
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
        html << "<div><span class=\"text-green-700 font-medium\">"
             << result.updated << " nicknames regenerated, "
             << result.skipped << " skipped (no first/last name or unchanged)"
             << "</span>";

        if (!result.changes.empty()) {
            html << "<details class=\"mt-3\">"
                 << "<summary class=\"cursor-pointer text-sm text-indigo-600 hover:text-indigo-800 font-medium\">"
                 << "Show details (" << result.changes.size() << " change" << (result.changes.size() != 1 ? "s" : "") << ")"
                 << "</summary>"
                 << "<div class=\"mt-2 border border-gray-200 rounded-lg overflow-hidden\">"
                 << "<table class=\"min-w-full text-xs\">"
                 << "<thead class=\"bg-gray-50\">"
                 << "<tr><th class=\"px-3 py-2 text-left text-gray-600\">Old Name</th>"
                 << "<th class=\"px-3 py-2 text-left text-gray-600\">New Name</th>"
                 << "<th class=\"px-3 py-2\"></th></tr></thead><tbody>";

            for (const auto& c : result.changes) {
                html << "<tr class=\"border-t border-gray-100\" id=\"nick-change-" << c.member_id << "\">"
                     << "<td class=\"px-3 py-2 text-gray-500 font-mono\">" << c.old_name << "</td>"
                     << "<td class=\"px-3 py-2 text-gray-900 font-mono\">" << c.new_name << "</td>"
                     << "<td class=\"px-3 py-2 text-right\">"
                     << "<button hx-post=\"/api/discord/revert-sync-change\" "
                     << "hx-vals='{\"member_id\": " << c.member_id
                     << ", \"change_type\": \"updated\", \"field\": \"display_name\""
                     << ", \"old_value\": \"" << c.old_name << "\"}' "
                     << "hx-target=\"#nick-change-" << c.member_id << "\" "
                     << "hx-swap=\"outerHTML\" "
                     << "hx-confirm=\"Revert nickname for " << c.new_name << "?\" "
                     << "class=\"text-amber-600 hover:text-amber-800 font-medium\">Revert</button></td>"
                     << "</tr>";
            }

            html << "</tbody></table></div></details>";
        }
        html << "</div>";
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
            html << "<div><span class=\"text-green-700 font-medium\">"
                 << "Nicknames: " << result.synced << " updated, "
                 << result.skipped << " skipped"
                 << (result.errors > 0 ? ", <span class=\"text-red-600\">" + std::to_string(result.errors) + " errors</span>" : "")
                 << "</span>";
            if (!result.error_details.empty()) {
                html << "<ul class=\"mt-2 text-xs text-red-600 list-disc list-inside\">";
                for (auto& e : result.error_details)
                    html << "<li>" << e << "</li>";
                html << "</ul>";
            }
            html << "</div>";
            res.write(html.str());
        } catch (const std::exception& ex) {
            res.write("<span class=\"text-red-600\">Error: " + std::string(ex.what()) + "</span>");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });
}
