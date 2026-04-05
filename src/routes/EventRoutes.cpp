#include "routes/EventRoutes.hpp"
#include "utils/MarkdownRenderer.hpp"
#include "utils/AuditDiff.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <stdexcept>
#include <set>
#include <map>
#include <sstream>
#include <cstdio>

static std::string ev_normalize_datetime(const std::string& dt) {
    if (dt.size() == 10) return dt + "T00:00:00"; // date-only "YYYY-MM-DD"
    if (dt.size() == 16) return dt + ":00";        // datetime-local without seconds
    return dt;
}

static const char* kEvMonthNames[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

static std::string event_status_color(const std::string& status) {
    if (status == "tentative") return "yellow";
    if (status == "open")      return "green";
    if (status == "closed")    return "yellow";
    if (status == "cancelled") return "red";
    if (status == "completed") return "gray";
    return "blue";
}

static crow::mustache::context build_event_list_ctx(
        const std::vector<LugEvent>& event_list,
        AttendanceService& attendance,
        ChapterMemberRepository& chapter_members,
        ChapterService& chapters_svc,
        bool is_admin,
        bool can_create,
        int64_t current_member_id,
        const std::string& title_str = "Events") {
    crow::mustache::context ctx;
    ctx["title"]      = title_str;
    ctx["is_admin"]   = is_admin;
    ctx["can_create"] = can_create;

    // Pre-fetch user's chapter memberships for per-event can_manage check
    std::map<int64_t, std::string> user_chapter_roles;
    if (!is_admin && current_member_id > 0) {
        auto memberships = chapter_members.find_by_member(current_member_id);
        for (const auto& cm : memberships)
            user_chapter_roles[cm.chapter_id] = cm.chapter_role;
    }

    // Build chapter name lookup
    std::map<int64_t, std::string> chapter_names;
    for (const auto& ch : chapters_svc.list_all())
        chapter_names[ch.id] = ch.name;

    crow::json::wvalue arr;
    for (size_t i = 0; i < event_list.size(); ++i) {
        const auto& e = event_list[i];
        arr[i]["id"]               = e.id;
        arr[i]["title"]            = e.title;
        arr[i]["description"]      = e.description;
        arr[i]["location"]         = e.location;
        arr[i]["start_time"]       = e.start_time;
        arr[i]["end_time"]         = e.end_time;

        // Parse date parts from ISO "YYYY-MM-DD..."
        if (e.start_time.size() >= 10) {
            try {
                int mo = std::stoi(e.start_time.substr(5, 2));
                arr[i]["month"] = std::string(kEvMonthNames[std::clamp(mo, 1, 12) - 1]);
                arr[i]["day"]   = std::to_string(std::stoi(e.start_time.substr(8, 2)));
                arr[i]["year"]  = e.start_time.substr(0, 4);
            } catch (...) {}
        }
        arr[i]["status"]           = e.status;
        arr[i]["status_color"]     = event_status_color(e.status);
        if (!e.signup_deadline.empty()) arr[i]["signup_deadline"] = e.signup_deadline;
        arr[i]["max_attendees"]    = e.max_attendees;
        arr[i]["discord_thread_id"]= e.discord_thread_id;
        arr[i]["scope"]            = e.scope;
        arr[i]["scope_lug_wide"]   = (e.scope == "lug_wide");
        arr[i]["scope_non_lug"]    = (e.scope == "non_lug");
        {
            auto cn = chapter_names.find(e.chapter_id);
            arr[i]["chapter_name"] = (cn != chapter_names.end()) ? cn->second : "";
        }
        arr[i]["event_lead_id"]    = e.event_lead_id;
        arr[i]["event_lead_name"]  = e.event_lead_name;
        arr[i]["has_event_lead"]   = (e.event_lead_id > 0);
        arr[i]["is_tentative"]     = (e.status == "tentative");
        arr[i]["is_confirmed"]     = (e.status == "confirmed");
        arr[i]["is_cancelled"]     = (e.status == "cancelled");
        arr[i]["is_open"]          = (e.status == "open");
        arr[i]["is_closed"]        = (e.status == "closed");
        arr[i]["has_discord_thread"]= !e.discord_thread_id.empty();
        arr[i]["is_admin"]         = is_admin;
        arr[i]["has_notes"]        = !e.notes.empty();
        arr[i]["notes_html"]       = e.notes.empty() ? "" : render_markdown(e.notes);
        arr[i]["has_report"]       = !e.notes_discord_post_id.empty();

        // Per-event can_manage: admin always, or chapter event_manager/lead
        bool event_can_manage = is_admin;
        if (!is_admin && e.chapter_id > 0) {
            auto it = user_chapter_roles.find(e.chapter_id);
            if (it != user_chapter_roles.end())
                event_can_manage = chapter_role_rank(it->second) >= chapter_role_rank("event_manager");
        }
        arr[i]["can_manage"] = event_can_manage;

        int count = attendance.get_count("event", e.id);
        arr[i]["attendance_count"] = count;

        bool checked_in = current_member_id > 0 &&
            attendance.is_checked_in(current_member_id, "event", e.id);
        arr[i]["is_checked_in"] = checked_in;

        // Spots left
        if (e.max_attendees > 0) {
            int spots_left = e.max_attendees - count;
            arr[i]["spots_left"]   = spots_left > 0 ? spots_left : 0;
            arr[i]["has_max"]      = true;
            arr[i]["is_full"]      = (spots_left <= 0);
        } else {
            arr[i]["has_max"] = false;
            arr[i]["is_full"] = false;
        }
    }
    ctx["events"] = std::move(arr);
    return ctx;
}

static constexpr int kEvPerPage = 10;

static std::string render_event_page(const crow::request& req,
                                      LugApp& app,
                                      EventService& events,
                                      AttendanceService& attendance,
                                      ChapterMemberRepository& chapter_members,
                                      ChapterService& chapters,
                                      bool all_events = false) {
    auto& auth     = app.get_context<AuthMiddleware>(req);
    bool is_admin  = auth.auth.role == "admin";
    int64_t mbr_id = auth.auth.member_id;
    // can_create: admin or anyone with event_manager/lead role in any chapter
    bool can_create = is_admin;
    if (!is_admin && mbr_id > 0) {
        auto memberships = chapter_members.find_by_member(mbr_id);
        for (const auto& cm : memberships)
            if (chapter_role_rank(cm.chapter_role) >= chapter_role_rank("event_manager"))
                { can_create = true; break; }
    }

    auto qs = crow::query_string(req.url_params);
    const char* s_raw = qs.get("search");
    std::string search = s_raw ? s_raw : "";
    int page = 1;
    { const char* p = qs.get("page"); if (p) try { page = std::stoi(p); } catch (...) {} }
    if (page < 1) page = 1;

    // Sort params
    const char* sc = qs.get("sort");
    const char* sd = qs.get("dir");
    std::string sort_col = sc ? sc : "start_time";
    std::string sort_dir = sd ? sd : "ASC";
    // Validate
    static const std::set<std::string> kEvSortCols = {"start_time","title","status","location","scope"};
    if (!kEvSortCols.count(sort_col)) sort_col = "start_time";
    if (sort_dir != "ASC" && sort_dir != "DESC") sort_dir = "ASC";

    bool upcoming_only = !all_events;
    int total = events.count_filtered(search, upcoming_only);
    int total_pages = (total + kEvPerPage - 1) / kEvPerPage;
    if (total_pages < 1) total_pages = 1;
    if (page > total_pages) page = total_pages;
    int offset = (page - 1) * kEvPerPage;

    auto event_list = events.list_paginated(search, kEvPerPage, offset, upcoming_only, sort_col, sort_dir);
    auto ctx = build_event_list_ctx(event_list, attendance, chapter_members, chapters, is_admin, can_create, mbr_id);

    ctx["show_all"]    = all_events;
    ctx["search"]      = search;
    ctx["page"]        = page;
    ctx["total_pages"] = total_pages;
    ctx["total_count"] = total;
    ctx["has_prev"]    = (page > 1);
    ctx["has_next"]    = (page < total_pages);
    ctx["prev_page"]   = page - 1;
    ctx["next_page"]   = page + 1;
    ctx["sort"]             = sort_col;
    ctx["dir"]              = sort_dir;
    ctx["next_dir"]         = (sort_dir == "ASC") ? "DESC" : "ASC";
    ctx["sort_is_date"]     = (sort_col == "start_time");
    ctx["sort_is_title"]    = (sort_col == "title");
    ctx["sort_is_location"] = (sort_col == "location");
    ctx["dir_is_asc"]       = (sort_dir == "ASC");

    bool is_htmx = req.get_header_value("HX-Request") == "true";
    if (is_htmx) {
        auto tmpl = crow::mustache::load("events/_content.html");
        return tmpl.render(ctx).dump();
    }

    auto content_tmpl = crow::mustache::load("events/_content.html");
    std::string content = content_tmpl.render(ctx).dump();
    crow::mustache::context layout_ctx;
    layout_ctx["content"]       = content;
    layout_ctx["page_title"]    = all_events ? "All Events" : "Events";
    layout_ctx["active_events"] = true;
    layout_ctx["is_admin"]      = is_admin;
        set_layout_auth(req, app, layout_ctx);
    auto layout = crow::mustache::load("layout.html");
    return layout.render(layout_ctx).dump();
}

void register_event_routes(LugApp& app, EventService& events, AttendanceService& attendance,
                            ChapterMemberRepository& chapter_members, DiscordClient& discord,
                            MemberService& members, MeetingService& meetings,
                            ChapterService& chapters, AuditService& audit) {

    // GET /api/discord/forum-threads - returns <option> elements for active forum threads
    CROW_ROUTE(app, "/api/discord/forum-threads")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") { res.code = 403; return res; }

        auto threads = discord.fetch_forum_threads();
        std::ostringstream html;
        html << "<option value=\"\">-- Select a thread --</option>\n";
        for (auto& t : threads)
            html << "<option value=\"" << t.id << "\">#" << t.name << "</option>\n";
        if (threads.empty())
            html.str("<option value=\"\">No active threads found in forum channel</option>");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /api/member-options?selected=<member_id>
    // Returns <option> elements for all members (used by event lead select).
    CROW_ROUTE(app, "/api/member-options")(
        [&](const crow::request& req) {
        crow::response res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (!ctx.auth.authenticated) { res.code = 401; return res; }

        std::string selected;
        const char* s = req.url_params.get("selected");
        if (s) selected = s;
        std::string placeholder = "-- Select member --";
        const char* p = req.url_params.get("placeholder");
        if (p) placeholder = p;

        auto all = members.list_all();
        std::ostringstream html;
        html << "<option value=\"\">" << placeholder << "</option>\n";
        for (auto& m : all) {
            html << "<option value=\"" << m.id << "\"";
            if (selected == std::to_string(m.id)) html << " selected";
            html << ">" << m.display_name << "</option>\n";
        }
        if (all.empty())
            html.str("<option value=\"\">No members found</option>");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // GET /events - list upcoming events
    CROW_ROUTE(app, "/events")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_event_page(req, app, events, attendance, chapter_members, chapters, false));
        return res;
    });

    // GET /events/all - list all events (admin)
    CROW_ROUTE(app, "/events/all")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_event_page(req, app, events, attendance, chapter_members, chapters, true));
        return res;
    });

    // GET /events/new - new event form (admin, chapter lead, or event manager)
    CROW_ROUTE(app, "/events/new")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        // Non-admins must have at least one chapter membership to create events
        if (ctx.auth.role != "admin") {
            auto memberships = chapter_members.find_by_member(ctx.auth.member_id);
            bool has_chapter_role = false;
            for (auto& m : memberships)
                if (m.chapter_role == "lead" || m.chapter_role == "event_manager")
                    has_chapter_role = true;
            if (!has_chapter_role) {
                res.code = 403;
                res.add_header("Content-Type", "text/html; charset=utf-8");
                res.write(R"(<div class="text-red-500 p-4">Only chapter leads and event managers can create events.</div>)");
                return res;
            }
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("events/_form.html");
        crow::mustache::context mctx;
        {
            auto member_list = members.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select event lead --</option>\n";
            for (auto& m : member_list)
                opts << "<option value=\"" << m.id << "\">" << m.display_name << "</option>\n";
            mctx["member_options"] = opts.str();
        }
        {
            auto roles = discord.fetch_guild_roles();
            std::ostringstream opts;
            for (auto& r : roles)
                opts << "<option value=\"" << r.id << "\">@" << r.name << "</option>\n";
            mctx["role_ping_options"] = opts.str();
        }
        {
            auto ch_list = chapters.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select chapter --</option>\n";
            for (auto& ch : ch_list)
                opts << "<option value=\"" << ch.id << "\">" << ch.name << "</option>\n";
            mctx["chapter_options"] = opts.str();
        }
        mctx["action"]            = "/events";
        mctx["method"]            = "POST";
        mctx["title"]             = "Create New Event";
        mctx["is_new"]            = true;
        mctx["has_forum_channel"] = !discord.get_events_forum_channel_id().empty();
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /events/<id>/edit - edit event form (admin or chapter event_manager/lead)
    CROW_ROUTE(app, "/events/<int>/edit")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Event not found.</div>");
            return res;
        }
        if (!can_manage_chapter_content(req, res, app, ev->chapter_id, chapter_members)) return res;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("events/_form.html");
        crow::mustache::context mctx;

        // Build member options with current lead pre-selected
        {
            auto member_list = members.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select event lead --</option>\n";
            for (auto& m : member_list) {
                opts << "<option value=\"" << m.id << "\"";
                if (m.id == ev->event_lead_id) opts << " selected";
                opts << ">" << m.display_name << "</option>\n";
            }
            mctx["member_options"] = opts.str();
        }
        // Build role ping options with currently selected roles pre-selected
        {
            // Parse current ping role IDs into a set for fast lookup
            std::set<std::string> selected_ids;
            {
                std::istringstream ss(ev->discord_ping_role_ids);
                std::string rid;
                while (std::getline(ss, rid, ','))
                    if (!rid.empty()) selected_ids.insert(rid);
            }
            auto roles = discord.fetch_guild_roles();
            std::ostringstream opts;
            for (auto& r : roles) {
                opts << "<option value=\"" << r.id << "\"";
                if (selected_ids.count(r.id)) opts << " selected";
                opts << ">@" << r.name << "</option>\n";
            }
            mctx["role_ping_options"] = opts.str();
        }

        // Trim datetime to date-only (YYYY-MM-DD) for date inputs
        auto trim_date = [](const std::string& dt) -> std::string {
            return dt.size() >= 10 ? dt.substr(0, 10) : dt;
        };

        mctx["action"]             = "/events/" + std::to_string(ev->id);
        mctx["title"]              = "Edit Event";
        mctx["is_edit"]            = true;
        mctx["title_val"]          = ev->title;
        mctx["description"]        = ev->description;
        mctx["location"]           = ev->location;
        mctx["start_time_input"]   = trim_date(ev->start_time);
        mctx["end_time_input"]     = trim_date(ev->end_time);
        mctx["signup_deadline_input"] = trim_date(ev->signup_deadline);
        mctx["chapter_id_str"]     = ev->chapter_id > 0 ? std::to_string(ev->chapter_id) : "";
        {
            auto ch_list = chapters.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select chapter --</option>\n";
            for (auto& ch : ch_list)
                opts << "<option value=\"" << ch.id << "\""
                     << (ch.id == ev->chapter_id ? " selected" : "")
                     << ">" << ch.name << "</option>\n";
            mctx["chapter_options"] = opts.str();
        }
        mctx["scope_chapter"]      = (ev->scope == "chapter" || ev->scope.empty());
        mctx["scope_lug_wide"]     = (ev->scope == "lug_wide");
        mctx["scope_non_lug"]      = (ev->scope == "non_lug");
        mctx["has_forum_channel"]  = !discord.get_events_forum_channel_id().empty();
        mctx["has_thread"]         = !ev->discord_thread_id.empty();
        mctx["discord_thread_id"]  = ev->discord_thread_id;
        mctx["suppress_discord"]   = ev->suppress_discord;
        mctx["suppress_calendar"]  = ev->suppress_calendar;
        mctx["notes"]              = ev->notes;
        mctx["entrance_fee"]       = ev->entrance_fee;
        mctx["public_kids"]        = ev->public_kids;
        mctx["public_teens"]       = ev->public_teens;
        mctx["public_adults"]      = ev->public_adults;
        mctx["social_media_links"] = ev->social_media_links;
        mctx["event_feedback"]     = ev->event_feedback;

        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /events/<id> - event detail page
    CROW_ROUTE(app, "/events/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Event not found.</div>");
            return res;
        }

        auto& auth     = app.get_context<AuthMiddleware>(req);
        bool is_admin  = auth.auth.role == "admin";
        int64_t mbr_id = auth.auth.member_id;

        auto attendees  = attendance.get_attendees("event", static_cast<int64_t>(id));
        bool checked_in = mbr_id > 0 &&
            attendance.is_checked_in(mbr_id, "event", static_cast<int64_t>(id));
        int count = static_cast<int>(attendees.size());

        crow::mustache::context ctx;
        ctx["id"]               = ev->id;
        ctx["title"]            = ev->title;
        ctx["description"]      = ev->description;
        ctx["location"]         = ev->location;
        ctx["start_time"]       = ev->start_time;
        ctx["end_time"]         = ev->end_time;
        // Date-only for display (trim time portion)
        ctx["start_date"]       = ev->start_time.size() >= 10 ? ev->start_time.substr(0, 10) : ev->start_time;
        ctx["end_date"]         = ev->end_time.size() >= 10 ? ev->end_time.substr(0, 10) : ev->end_time;
        ctx["status"]           = ev->status;
        ctx["status_color"]     = event_status_color(ev->status);
        if (!ev->signup_deadline.empty()) ctx["signup_deadline"] = ev->signup_deadline;
        ctx["max_attendees"]    = ev->max_attendees;
        ctx["discord_thread_id"]= ev->discord_thread_id;
        ctx["is_tentative"]     = (ev->status == "tentative");
        ctx["is_confirmed"]     = (ev->status == "confirmed");
        ctx["is_cancelled"]     = (ev->status == "cancelled");
        ctx["is_open"]          = (ev->status == "open");
        ctx["is_closed"]        = (ev->status == "closed");
        ctx["scope"]            = ev->scope;
        ctx["scope_lug_wide"]   = (ev->scope == "lug_wide");
        ctx["scope_non_lug"]    = (ev->scope == "non_lug");
        ctx["scope_chapter"]    = (ev->scope == "chapter" || ev->scope.empty());
        if (ev->chapter_id > 0) {
            auto ch = chapters.get(ev->chapter_id);
            if (ch) ctx["chapter_name"] = ch->name;
        }
        ctx["event_lead_name"]  = ev->event_lead_name;
        ctx["has_event_lead"]   = (ev->event_lead_id > 0);
        ctx["has_notes"]        = !ev->notes.empty();
        ctx["notes_html"]       = ev->notes.empty() ? "" : render_markdown(ev->notes);
        ctx["entrance_fee"]     = ev->entrance_fee;
        ctx["has_entrance_fee"] = !ev->entrance_fee.empty();
        ctx["is_admin"]         = is_admin;
        ctx["is_checked_in"]    = checked_in;
        ctx["member_id"]        = mbr_id;
        ctx["attendance_count"] = count;
        ctx["has_discord_thread"]= !ev->discord_thread_id.empty();
        ctx["page_title"]       = ev->title;
        // can_manage: admin, event lead, or chapter event_manager/lead
        {
            bool cm = is_admin || (ev->event_lead_id > 0 && ev->event_lead_id == mbr_id);
            if (!cm && ev->chapter_id > 0) {
                auto role_opt = chapter_members.get_chapter_role(mbr_id, ev->chapter_id);
                if (role_opt) cm = chapter_role_rank(*role_opt) >= chapter_role_rank("event_manager");
            }
            ctx["can_manage"] = cm;
        }

        if (ev->max_attendees > 0) {
            ctx["has_max"]   = true;
            ctx["spots_left"]= std::max(0, ev->max_attendees - count);
            ctx["is_full"]   = (ev->max_attendees - count <= 0);
        } else {
            ctx["has_max"] = false;
            ctx["is_full"] = false;
        }

        crow::json::wvalue att_arr;
        for (size_t i = 0; i < attendees.size(); ++i) {
            att_arr[i]["member_display_name"]    = attendees[i].member_display_name;
            att_arr[i]["member_discord_username"]= attendees[i].member_discord_username;
            att_arr[i]["checked_in_at"]          = attendees[i].checked_in_at;
            att_arr[i]["notes"]                  = attendees[i].notes;
        }
        ctx["attendees"] = std::move(att_arr);

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        auto content_tmpl = crow::mustache::load("events/_detail.html");
        std::string content = content_tmpl.render(ctx).dump();

        if (is_htmx) {
            res.write(content);
        } else {
            crow::mustache::context layout_ctx;
            layout_ctx["content"]       = content;
            layout_ctx["page_title"]    = ev->title;
            layout_ctx["active_events"] = true;
            layout_ctx["is_admin"]      = is_admin;
        set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /events - create event (form POST)
    CROW_ROUTE(app, "/events").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        // Chapter permission check (non-admins must be lead/event_manager for the chapter)
        auto& auth_ctx = app.get_context<AuthMiddleware>(req);
        if (auth_ctx.auth.role != "admin") {
            std::string ch_str = get_param("chapter_id");
            int64_t chapter_id = ch_str.empty() ? 0 : std::stoll(ch_str);
            if (chapter_id == 0 || !can_manage_chapter_content(req, res, app, chapter_id, chapter_members)) {
                if (chapter_id == 0) {
                    res.code = 403;
                    res.add_header("Content-Type", "text/html; charset=utf-8");
                    res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">A chapter must be selected.</div>)");
                }
                return res;
            }
        }

        LugEvent e;
        e.title            = get_param("title");
        e.description      = get_param("description");
        e.location         = get_param("location");
        e.start_time       = ev_normalize_datetime(get_param("start_time"));
        e.end_time         = ev_normalize_datetime(get_param("end_time"));
        e.signup_deadline  = ev_normalize_datetime(get_param("signup_deadline"));
        e.scope            = get_param("scope").empty() ? "chapter" : get_param("scope");
        e.status           = "open";
        { std::string ch = get_param("chapter_id");
          if (!ch.empty()) try { e.chapter_id = std::stoll(ch); } catch (...) {} }
        // If the user chose an existing forum thread, pre-set it so EventService skips creation
        { std::string mode = get_param("thread_mode");
          if (mode == "existing") e.discord_thread_id = get_param("existing_thread_id"); }
        { std::string lead = get_param("event_lead_id");
          if (!lead.empty()) try { e.event_lead_id = std::stoll(lead); } catch (...) {} }
        {
            auto role_vals = params.get_list("discord_ping_role_ids", false);
            std::string csv;
            for (auto* r : role_vals)
                if (r && r[0]) { if (!csv.empty()) csv += ","; csv += r; }
            e.discord_ping_role_ids = csv; // empty = no extra pings
        }

        const char* max_str = params.get("max_attendees");
        if (max_str && std::string(max_str) != "") {
            try { e.max_attendees = std::stoi(max_str); } catch (...) { e.max_attendees = 0; }
        }
        e.suppress_discord  = (get_param("suppress_discord") == "on" || get_param("suppress_discord") == "1");
        e.suppress_calendar = (get_param("suppress_calendar") == "on" || get_param("suppress_calendar") == "1");
        e.notes             = get_param("notes");
        e.entrance_fee      = get_param("entrance_fee");
        try { e.public_kids   = std::stoi(get_param("public_kids")); } catch (...) {}
        try { e.public_teens  = std::stoi(get_param("public_teens")); } catch (...) {}
        try { e.public_adults = std::stoi(get_param("public_adults")); } catch (...) {}
        e.social_media_links = get_param("social_media_links");
        e.event_feedback     = get_param("event_feedback");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (e.title.empty()) {
            res.code = 400;
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Title is required.</div>)");
            return res;
        }


        try {
            events.create(e);
            audit.log(req, app, "event.create", "event", e.id, e.title, "Created event");
            res.add_header("HX-Trigger", "closeModal");
            res.add_header("HX-Redirect", "/events");
            res.code = 200;
            res.write(render_event_page(req, app, events, attendance, chapter_members, chapters, false));
        } catch (const std::exception& ex) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Error: )")
                + ex.what() + "</div>");
        }
        return res;
    });

    // PUT /events/<id> - update event (admin or chapter event_manager/lead)
    CROW_ROUTE(app, "/events/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev_before = events.get(static_cast<int64_t>(id));
        if (!ev_before) { res.code = 404; res.write(R"({"error":"not found"})"); res.end(); return res; }
        if (!can_manage_chapter_content(req, res, app, ev_before->chapter_id, chapter_members)) return res;

        std::string content_type = req.get_header_value("Content-Type");
        bool is_form = content_type.find("application/x-www-form-urlencoded") != std::string::npos;

        LugEvent updates;

        if (is_form) {
            auto params = crow::query_string("?" + req.body);
            auto gp = [&](const char* k) -> std::string {
                const char* v = params.get(k);
                return v ? std::string(v) : "";
            };
            std::string title = gp("title");
            if (!title.empty())                updates.title           = title;
            std::string desc = gp("description");
            if (!desc.empty())                 updates.description     = desc;
            std::string loc = gp("location");
            if (!loc.empty())                  updates.location        = loc;
            std::string st = gp("start_time");
            if (!st.empty())                   updates.start_time      = ev_normalize_datetime(st);
            std::string et = gp("end_time");
            if (!et.empty())                   updates.end_time        = ev_normalize_datetime(et);
            std::string sd = gp("signup_deadline");
            if (!sd.empty())                   updates.signup_deadline = ev_normalize_datetime(sd);
            std::string scope = gp("scope");
            if (!scope.empty())                updates.scope           = scope;
            std::string ch = gp("chapter_id");
            if (!ch.empty()) try { updates.chapter_id = std::stoll(ch); } catch (...) {}
            std::string lead = gp("event_lead_id");
            if (!lead.empty()) try { updates.event_lead_id = std::stoll(lead); } catch (...) {}
            {
                // Multi-select: always apply (absent = user cleared all selections)
                auto role_vals = params.get_list("discord_ping_role_ids", false);
                std::string csv;
                for (auto* r : role_vals)
                    if (r && r[0]) { if (!csv.empty()) csv += ","; csv += r; }
                updates.discord_ping_role_ids = csv;
            }
            updates.suppress_discord  = (gp("suppress_discord") == "on" || gp("suppress_discord") == "1");
            updates.suppress_calendar = (gp("suppress_calendar") == "on" || gp("suppress_calendar") == "1");
            updates.notes             = gp("notes");
            updates.entrance_fee      = gp("entrance_fee");
            try { updates.public_kids   = std::stoi(gp("public_kids")); } catch (...) {}
            try { updates.public_teens  = std::stoi(gp("public_teens")); } catch (...) {}
            try { updates.public_adults = std::stoi(gp("public_adults")); } catch (...) {}
            updates.social_media_links = gp("social_media_links");
            updates.event_feedback     = gp("event_feedback");
            // Thread selection on edit
            { std::string mode = gp("thread_mode");
              if (mode == "existing") {
                  std::string tid = gp("existing_thread_id");
                  if (!tid.empty()) updates.discord_thread_id = tid;
              } else if (mode == "new") {
                  // Fetch current event to build thread name
                  auto cur = events.get(static_cast<int64_t>(id));
                  if (cur) {
                      LugEvent merged = *cur;
                      if (!updates.title.empty()) merged.title = updates.title;
                      if (!updates.location.empty()) merged.location = updates.location;
                      if (!updates.start_time.empty()) merged.start_time = updates.start_time;
                      if (!updates.end_time.empty()) merged.end_time = updates.end_time;
                      std::string thread_name = merged.title;
                      std::string new_tid = discord.sync_create_forum_thread_for_event(thread_name, merged);
                      if (!new_tid.empty()) updates.discord_thread_id = new_tid;
                  }
              }
            }

            res.add_header("Content-Type", "text/html; charset=utf-8");
            if (updates.title.empty()) {
                res.code = 400;
                std::cerr << "[EventRoutes] PUT /events/" << id << " failed: title empty\n";
                res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Title is required.</div>)");
                return res;
            }
            try {
                auto ev_after = events.update(static_cast<int64_t>(id), updates);
                {
                    AuditDiff diff;
                    diff.field("title", ev_before->title, ev_after.title);
                    diff.field("description", ev_before->description, ev_after.description);
                    diff.field("location", ev_before->location, ev_after.location);
                    diff.field("start_time", ev_before->start_time, ev_after.start_time);
                    diff.field("end_time", ev_before->end_time, ev_after.end_time);
                    diff.field("status", ev_before->status, ev_after.status);
                    diff.field("scope", ev_before->scope, ev_after.scope);
                    diff.field("chapter_id", ev_before->chapter_id, ev_after.chapter_id);
                    diff.field("event_lead_id", ev_before->event_lead_id, ev_after.event_lead_id);
                    diff.field("signup_deadline", ev_before->signup_deadline, ev_after.signup_deadline);
                    diff.field("max_attendees", ev_before->max_attendees, ev_after.max_attendees);
                    diff.field("entrance_fee", ev_before->entrance_fee, ev_after.entrance_fee);
                    diff.field("suppress_discord", ev_before->suppress_discord, ev_after.suppress_discord);
                    diff.field("suppress_calendar", ev_before->suppress_calendar, ev_after.suppress_calendar);
                    diff.field("notes", ev_before->notes, ev_after.notes);
                    diff.field("public_kids", ev_before->public_kids, ev_after.public_kids);
                    diff.field("public_teens", ev_before->public_teens, ev_after.public_teens);
                    diff.field("public_adults", ev_before->public_adults, ev_after.public_adults);
                    diff.field("social_media_links", ev_before->social_media_links, ev_after.social_media_links);
                    diff.field("event_feedback", ev_before->event_feedback, ev_after.event_feedback);
                    audit.log(req, app, "event.update", "event", static_cast<int64_t>(id), ev_after.title,
                              diff.has_changes() ? diff.str() : "No field changes");
                }
                res.add_header("HX-Trigger", "closeModal");
                res.add_header("HX-Redirect", "/events");
                res.code = 200;
                res.write(render_event_page(req, app, events, attendance, chapter_members, chapters, false));
            } catch (const std::exception& ex) {
                res.code = 400;
                std::cerr << "[EventRoutes] PUT /events/" << id << " error: " << ex.what() << "\n";
                res.write(std::string(
                    R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Error: )")
                    + ex.what() + "</div>");
            }
        } else {
            auto body = crow::json::load(req.body);
            if (!body) {
                res.code = 400;
                res.write(R"({"error":"Invalid JSON"})");
                res.add_header("Content-Type", "application/json");
                return res;
            }
            if (body.has("title"))           updates.title           = body["title"].s();
            if (body.has("description"))     updates.description     = body["description"].s();
            if (body.has("location"))        updates.location        = body["location"].s();
            if (body.has("start_time"))      updates.start_time      = ev_normalize_datetime(body["start_time"].s());
            if (body.has("end_time"))        updates.end_time        = ev_normalize_datetime(body["end_time"].s());
            if (body.has("signup_deadline")) updates.signup_deadline = ev_normalize_datetime(body["signup_deadline"].s());
            if (body.has("max_attendees"))   updates.max_attendees   = static_cast<int>(body["max_attendees"].i());
            if (body.has("scope"))           updates.scope           = body["scope"].s();
            if (body.has("event_lead_id"))        updates.event_lead_id        = body["event_lead_id"].i();
            if (body.has("discord_ping_role_ids")) updates.discord_ping_role_ids = body["discord_ping_role_ids"].s();
            try {
                auto updated = events.update(static_cast<int64_t>(id), updates);
                audit.log(req, app, "event.update", "event", updated.id, updated.title, "Updated event (JSON)");
                crow::json::wvalue resp;
                resp["id"]      = updated.id;
                resp["title"]   = updated.title;
                resp["success"] = true;
                res.write(resp.dump());
                res.add_header("Content-Type", "application/json");
            } catch (const std::exception& ex) {
                res.code = 400;
                res.write(std::string(R"({"error":")") + ex.what() + "\"}");
                res.add_header("Content-Type", "application/json");
            }
        }
        return res;
    });

    // POST /events/<id>/discord-sync - force-push current event data to Discord
    CROW_ROUTE(app, "/events/<int>/discord-sync").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) {
            res.code = 404;
            res.write(R"(<span class="text-red-500 text-xs">Event not found</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        try {
            events.update(static_cast<int64_t>(id), *ev);
            audit.log(req, app, "event.discord_sync", "event", static_cast<int64_t>(id), ev->title, "Discord sync");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-green-600 text-xs">Synced</span>)");
        } catch (const std::exception& ex) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(std::string(R"(<span class="text-red-500 text-xs">Error: )") + ex.what() + "</span>");
        }
        return res;
    });

    // POST /events/<id>/cancel - cancel event (admin or chapter event_manager/lead)
    CROW_ROUTE(app, "/events/<int>/cancel").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        {
            auto ev = events.get(static_cast<int64_t>(id));
            if (!ev) { res.code = 404; res.write(R"({"error":"not found"})"); return res; }
            if (!can_manage_chapter_content(req, res, app, ev->chapter_id, chapter_members)) return res;
        }

        try {
            events.cancel(static_cast<int64_t>(id));
            audit.log(req, app, "event.delete", "event", static_cast<int64_t>(id), "", "Cancelled event");
            res.add_header("HX-Redirect", "/events");
            res.code = 200;
        } catch (const std::exception& ex) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + ex.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // POST /events/<id>/status - update event status (admin or chapter event_manager/lead)
    CROW_ROUTE(app, "/events/<int>/status").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        {
            auto ev = events.get(static_cast<int64_t>(id));
            if (!ev) { res.code = 404; res.write(R"({"error":"not found"})"); return res; }
            if (!can_manage_chapter_content(req, res, app, ev->chapter_id, chapter_members)) return res;
        }

        auto params = crow::query_string("?" + req.body);
        const char* status_raw = params.get("status");
        std::string status;
        if (status_raw) {
            status = status_raw;
        } else {
            auto body = crow::json::load(req.body);
            if (body && body.has("status")) status = body["status"].s();
        }

        if (status.empty()) {
            res.code = 400;
            res.write(R"({"error":"status required"})");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        try {
            events.update_status(static_cast<int64_t>(id), status);
            audit.log(req, app, "event.status", "event", static_cast<int64_t>(id), "", "Status changed to " + status);
            res.add_header("HX-Redirect", "/events");
            res.code = 200;
        } catch (const std::exception& ex) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + ex.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // POST /events/<id>/checkin - check in current user
    CROW_ROUTE(app, "/events/<int>/checkin").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth     = app.get_context<AuthMiddleware>(req);
        int64_t mbr_id = auth.auth.member_id;

        auto params = crow::query_string("?" + req.body);
        const char* notes_raw = params.get("notes");
        std::string notes = notes_raw ? std::string(notes_raw) : "";

        bool already = attendance.is_checked_in(mbr_id, "event", static_cast<int64_t>(id));
        if (already) {
            attendance.check_out(mbr_id, "event", static_cast<int64_t>(id));
            audit.log(req, app, "event.self_checkout", "event", static_cast<int64_t>(id), "", "Self check-out");
            res.write(R"(<button hx-post="/events/)" + std::to_string(id) +
                R"(/checkin" hx-swap="outerHTML" hx-target="this")"
                R"( class="inline-flex items-center gap-1 px-3 py-1.5 bg-green-600 text-white text-sm font-medium rounded hover:bg-green-700">)"
                "Sign Up / Check In</button>");
        } else {
            bool ok = attendance.check_in(mbr_id, "event", static_cast<int64_t>(id), notes);
            if (!ok) {
                res.code = 400;
                res.write(R"(<div class="text-red-500 text-sm">Could not check in (event may be full or closed).</div>)");
                res.add_header("Content-Type", "text/html");
                return res;
            }
            audit.log(req, app, "event.self_checkin", "event", static_cast<int64_t>(id), "", "Self check-in");
            res.write(R"(<button hx-post="/events/)" + std::to_string(id) +
                R"(/checkin" hx-swap="outerHTML" hx-target="this")"
                R"( class="inline-flex items-center gap-1 px-3 py-1.5 bg-gray-500 text-white text-sm font-medium rounded hover:bg-gray-600">)"
                "Cancel Sign-Up</button>");
        }
        res.add_header("HX-Trigger", "attendanceUpdated");
        res.add_header("Content-Type", "text/html");
        return res;
    });

    // POST /events/<id>/convert-to-meeting - convert event to meeting (admin only)
    CROW_ROUTE(app, "/events/<int>/convert-to-meeting").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-sm">Event not found</span>)");
            return res;
        }

        try {
            // Create a meeting from the event data
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

            // Copy attendance records from event to meeting
            auto attendees = attendance.get_attendees("event", static_cast<int64_t>(id));
            for (auto& a : attendees) {
                attendance.check_in(a.member_id, "meeting", created.id, a.notes, a.is_virtual);
            }

            // Delete the event (cleans up Discord, Google Calendar, attendance)
            events.cancel(static_cast<int64_t>(id));

            audit.log(req, app, "event.convert_to_meeting", "event", static_cast<int64_t>(id), ev->title, "Converted to meeting");
            res.add_header("HX-Redirect", "/meetings");
            res.code = 200;
        } catch (const std::exception& ex) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(std::string(R"(<span class="text-red-500 text-sm">Error: )") + ex.what() + "</span>");
        }
        return res;
    });

    // POST /events/<id>/publish-report - publish notes+attendance to Discord forum
    CROW_ROUTE(app, "/events/<int>/publish-report").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto ev = events.get(static_cast<int64_t>(id));
        if (!ev) { res.code = 404; res.write("Not found"); return res; }
        if (!can_manage_chapter_content(req, res, app, ev->chapter_id, chapter_members)) return res;

        // Build report in required format
        auto attendees = attendance.get_attendees("event", ev->id);

        // Get chapter name
        std::string chapter_name;
        if (ev->chapter_id > 0) {
            auto ch = chapters.get(ev->chapter_id);
            if (ch) chapter_name = ch->name;
        }

        // Group attendees by date (day portion of checked_in_at)
        std::map<std::string, std::vector<std::string>> by_day;
        for (const auto& a : attendees) {
            std::string day = a.checked_in_at.size() >= 10 ? a.checked_in_at.substr(0, 10) : a.checked_in_at;
            by_day[day].push_back(a.member_display_name);
        }

        std::ostringstream report;
        report << "**Event name:** " << ev->title << "\n";
        report << "**Start date:** " << ev->start_time.substr(0, 10) << "\n";
        report << "**End date:** " << ev->end_time.substr(0, 10) << "\n";
        if (!ev->entrance_fee.empty())
            report << "**Entrance fee:** " << ev->entrance_fee << "\n";

        // Multi-day attendance
        int day_num = 1;
        for (const auto& [day, names] : by_day) {
            report << "**Member names day" << day_num << " (" << day << "):** ";
            for (size_t i = 0; i < names.size(); ++i) {
                if (i > 0) report << ", ";
                report << names[i];
            }
            report << "\n";
            ++day_num;
        }
        if (by_day.empty() && !attendees.empty()) {
            report << "**Members:** ";
            for (size_t i = 0; i < attendees.size(); ++i) {
                if (i > 0) report << ", ";
                report << attendees[i].member_display_name;
            }
            report << "\n";
        }

        report << "**Public kids:** " << ev->public_kids << "\n";
        report << "**Public teens:** " << ev->public_teens << "\n";
        report << "**Public adults:** " << ev->public_adults << "\n";
        if (!ev->social_media_links.empty())
            report << "**Social media links, ArkLUG mentions, announcements for show:** " << ev->social_media_links << "\n";
        if (!ev->event_feedback.empty())
            report << "**What you liked best about event:** " << ev->event_feedback << "\n";
        if (!ev->notes.empty())
            report << "\n## Notes\n" << ev->notes << "\n";

        // Use dedicated event reports forum, fall back to events forum
        std::string forum_id = discord.get_event_reports_forum_id();
        if (forum_id.empty()) forum_id = discord.get_events_forum_channel_id();

        std::string thread_id = discord.publish_report_to_forum(
            forum_id, ev->notes_discord_post_id,
            "Report: " + ev->title, report.str());

        if (!thread_id.empty() && thread_id != ev->notes_discord_post_id) {
            events.repo().update_notes_discord_post_id(ev->id, thread_id);
        }

        audit.log(req, app, "event.publish_report", "event", ev->id, "", "Published report");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(R"(<span class="text-green-600 text-xs">Report published!</span>)");
        return res;
    });
}
