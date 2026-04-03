#include "routes/MeetingRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <cstdio>

static std::string normalize_datetime(const std::string& dt) {
    if (dt.size() == 16) return dt + ":00";
    return dt;
}

static const char* kMonthNames[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

// Format "YYYY-MM-DDTHH:MM:SS" → "H:MM AM/PM"
static std::string fmt_time(const std::string& iso) {
    if (iso.size() < 16) return iso;
    try {
        int h = std::stoi(iso.substr(11, 2));
        int m = std::stoi(iso.substr(14, 2));
        std::string ampm = h >= 12 ? "PM" : "AM";
        int h12 = h % 12; if (h12 == 0) h12 = 12;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d:%02d %s", h12, m, ampm.c_str());
        return buf;
    } catch (...) {}
    return iso.substr(11, 5);
}

static std::string meeting_status_color(const std::string& status) {
    if (status == "scheduled") return "blue";
    if (status == "cancelled") return "red";
    if (status == "completed") return "gray";
    return "blue";
}

static crow::mustache::context build_meeting_list_ctx(
        const std::vector<Meeting>& meeting_list,
        AttendanceService& attendance,
        bool is_admin,
        int64_t current_member_id,
        const std::string& title_str = "Meetings") {
    crow::mustache::context ctx;
    ctx["title"]    = title_str;
    ctx["is_admin"] = is_admin;

    crow::json::wvalue arr;
    for (size_t i = 0; i < meeting_list.size(); ++i) {
        const auto& m = meeting_list[i];
        arr[i]["id"]           = m.id;
        arr[i]["title"]        = m.title;
        arr[i]["description"]  = m.description;
        arr[i]["location"]     = m.location;
        arr[i]["start_time"]   = m.start_time;
        arr[i]["end_time"]     = m.end_time;

        // Parse date parts from ISO "YYYY-MM-DDTHH:MM:SS"
        if (m.start_time.size() >= 10) {
            try {
                int mo = std::stoi(m.start_time.substr(5, 2));
                arr[i]["month"] = std::string(kMonthNames[std::clamp(mo, 1, 12) - 1]);
                arr[i]["day"]   = std::to_string(std::stoi(m.start_time.substr(8, 2)));
                arr[i]["year"]  = m.start_time.substr(0, 4);
            } catch (...) {}
        }
        arr[i]["start_time_fmt"] = fmt_time(m.start_time);
        arr[i]["end_time_fmt"]   = fmt_time(m.end_time);
        arr[i]["status"]       = m.status;
        arr[i]["status_color"] = meeting_status_color(m.status);
        arr[i]["is_cancelled"]   = (m.status == "cancelled");
        arr[i]["is_completed"]   = (m.status == "completed");
        arr[i]["is_scheduled"]   = (m.status == "scheduled");
        arr[i]["scope"]          = m.scope;
        arr[i]["scope_lug_wide"] = (m.scope == "lug_wide");
        arr[i]["scope_non_lug"]  = (m.scope == "non_lug");
        arr[i]["discord_event_id"] = m.discord_event_id;

        int count = attendance.get_count("meeting", m.id);
        arr[i]["attendance_count"] = count;

        bool checked_in = current_member_id > 0 &&
            attendance.is_checked_in(current_member_id, "meeting", m.id);
        arr[i]["is_checked_in"] = checked_in;
    }
    ctx["meetings"] = std::move(arr);
    return ctx;
}

static constexpr int kPerPage = 10;

static std::string render_meeting_page(const crow::request& req,
                                        LugApp& app,
                                        MeetingService& meetings,
                                        AttendanceService& attendance) {
    auto& auth = app.get_context<AuthMiddleware>(req);
    bool is_admin = auth.auth.role == "admin";
    int64_t member_id = auth.auth.member_id;

    // Pagination + search params
    auto qs = crow::query_string(req.url_params);
    const char* s_raw = qs.get("search");
    std::string search = s_raw ? s_raw : "";
    int page = 1;
    { const char* p = qs.get("page"); if (p) try { page = std::stoi(p); } catch (...) {} }
    if (page < 1) page = 1;

    int total = meetings.count_filtered(search);
    int total_pages = (total + kPerPage - 1) / kPerPage;
    if (total_pages < 1) total_pages = 1;
    if (page > total_pages) page = total_pages;
    int offset = (page - 1) * kPerPage;

    auto meeting_list = meetings.list_paginated(search, kPerPage, offset);
    auto ctx = build_meeting_list_ctx(meeting_list, attendance, is_admin, member_id);

    ctx["search"]      = search;
    ctx["page"]        = page;
    ctx["total_pages"] = total_pages;
    ctx["total_count"] = total;
    ctx["has_prev"]    = (page > 1);
    ctx["has_next"]    = (page < total_pages);
    ctx["prev_page"]   = page - 1;
    ctx["next_page"]   = page + 1;

    bool is_htmx = req.get_header_value("HX-Request") == "true";
    if (is_htmx) {
        auto tmpl = crow::mustache::load("meetings/_content.html");
        return tmpl.render(ctx).dump();
    }

    auto content_tmpl = crow::mustache::load("meetings/_content.html");
    std::string content = content_tmpl.render(ctx).dump();
    crow::mustache::context layout_ctx;
    layout_ctx["content"]          = content;
    layout_ctx["page_title"]       = "Meetings";
    layout_ctx["active_meetings"]  = true;
    layout_ctx["is_admin"]         = is_admin;
    auto layout = crow::mustache::load("layout.html");
    return layout.render(layout_ctx).dump();
}

void register_meeting_routes(LugApp& app, MeetingService& meetings, AttendanceService& attendance,
                              ChapterMemberRepository& chapter_members, ChapterService& chapters,
                              DiscordClient& discord) {

    // GET /meetings - list meetings (paginated + searchable)
    CROW_ROUTE(app, "/meetings")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_meeting_page(req, app, meetings, attendance));
        return res;
    });

    // GET /meetings/new - new meeting form (admin, chapter lead, or event manager)
    CROW_ROUTE(app, "/meetings/new")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.role != "admin") {
            auto memberships = chapter_members.find_by_member(ctx.auth.member_id);
            bool has_role = false;
            for (auto& m : memberships)
                if (m.chapter_role == "lead" || m.chapter_role == "event_manager")
                    has_role = true;
            if (!has_role) {
                res.code = 403;
                res.add_header("Content-Type", "text/html; charset=utf-8");
                res.write(R"(<div class="text-red-500 p-4">Only chapter leads and event managers can schedule meetings.</div>)");
                return res;
            }
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("meetings/_form.html");
        crow::mustache::context mctx;
        {
            auto ch_list = chapters.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select chapter --</option>\n";
            for (auto& ch : ch_list)
                opts << "<option value=\"" << ch.id << "\">" << ch.name << "</option>\n";
            mctx["chapter_options"] = opts.str();
        }
        mctx["action"]        = "/meetings";
        mctx["title"]         = "Schedule New Meeting";
        mctx["is_new"]        = true;
        mctx["scope_chapter"] = true;
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /meetings/<id>/edit - edit meeting form (admin or chapter event_manager/lead)
    CROW_ROUTE(app, "/meetings/<int>/edit")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Meeting not found.</div>");
            return res;
        }
        if (!can_manage_chapter_content(req, res, app, m->chapter_id, chapter_members)) return res;

        // Trim to "YYYY-MM-DDTHH:MM" for datetime-local inputs
        auto trim_dt = [](const std::string& dt) -> std::string {
            return dt.size() >= 16 ? dt.substr(0, 16) : dt;
        };

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("meetings/_form.html");
        crow::mustache::context mctx;
        mctx["action"]            = "/meetings/" + std::to_string(m->id);
        mctx["title"]             = "Edit Meeting";
        mctx["is_edit"]           = true;
        mctx["title_val"]         = m->title;
        mctx["description"]       = m->description;
        mctx["location"]          = m->location;
        mctx["start_time_input"]  = trim_dt(m->start_time);
        mctx["end_time_input"]    = trim_dt(m->end_time);
        mctx["chapter_id_str"]    = m->chapter_id > 0 ? std::to_string(m->chapter_id) : "";
        {
            auto ch_list = chapters.list_all();
            std::ostringstream opts;
            opts << "<option value=\"\">-- Select chapter --</option>\n";
            for (auto& ch : ch_list)
                opts << "<option value=\"" << ch.id << "\""
                     << (ch.id == m->chapter_id ? " selected" : "")
                     << ">" << ch.name << "</option>\n";
            mctx["chapter_options"] = opts.str();
        }
        mctx["scope_chapter"]     = (m->scope == "chapter" || m->scope.empty());
        mctx["scope_lug_wide"]    = (m->scope == "lug_wide");
        mctx["scope_non_lug"]     = (m->scope == "non_lug");
        mctx["suppress_discord"]  = m->suppress_discord;
        mctx["suppress_calendar"] = m->suppress_calendar;
        mctx["notes"]             = m->notes;
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /meetings/<id> - meeting detail page
    CROW_ROUTE(app, "/meetings/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Meeting not found.</div>");
            return res;
        }

        auto& auth = app.get_context<AuthMiddleware>(req);
        bool is_admin   = auth.auth.role == "admin";
        int64_t mbr_id  = auth.auth.member_id;

        auto attendees = attendance.get_attendees("meeting", static_cast<int64_t>(id));
        bool checked_in = mbr_id > 0 &&
            attendance.is_checked_in(mbr_id, "meeting", static_cast<int64_t>(id));

        crow::mustache::context ctx;
        ctx["id"]           = m->id;
        ctx["title"]        = m->title;
        ctx["description"]  = m->description;
        ctx["location"]     = m->location;
        ctx["start_time"]   = m->start_time;
        ctx["end_time"]     = m->end_time;
        ctx["status"]       = m->status;
        ctx["status_color"] = meeting_status_color(m->status);
        ctx["is_cancelled"] = (m->status == "cancelled");
        ctx["is_completed"] = (m->status == "completed");
        ctx["is_scheduled"] = (m->status == "scheduled");
        ctx["is_admin"]     = is_admin;
        ctx["is_checked_in"]= checked_in;
        ctx["member_id"]    = mbr_id;
        ctx["page_title"]   = m->title;

        crow::json::wvalue att_arr;
        for (size_t i = 0; i < attendees.size(); ++i) {
            att_arr[i]["member_display_name"]    = attendees[i].member_display_name;
            att_arr[i]["member_discord_username"]= attendees[i].member_discord_username;
            att_arr[i]["checked_in_at"]          = attendees[i].checked_in_at;
            att_arr[i]["notes"]                  = attendees[i].notes;
        }
        ctx["attendees"]         = std::move(att_arr);
        ctx["attendance_count"]  = static_cast<int>(attendees.size());

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        std::string content;
        auto content_tmpl = crow::mustache::load("meetings/_detail.html");
        content = content_tmpl.render(ctx).dump();

        if (is_htmx) {
            res.write(content);
        } else {
            crow::mustache::context layout_ctx;
            layout_ctx["content"]         = content;
            layout_ctx["page_title"]      = m->title;
            layout_ctx["active_meetings"] = true;
            layout_ctx["is_admin"]        = is_admin;
            auto layout = crow::mustache::load("layout.html");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /meetings - create meeting (form POST)
    CROW_ROUTE(app, "/meetings").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        // Chapter permission check
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

        Meeting m;
        m.title       = get_param("title");
        m.description = get_param("description");
        m.location    = get_param("location");
        m.start_time  = normalize_datetime(get_param("start_time"));
        m.end_time    = normalize_datetime(get_param("end_time"));
        m.status      = "scheduled";
        m.scope       = get_param("scope").empty() ? "chapter" : get_param("scope");
        { std::string ch = get_param("chapter_id");
          if (!ch.empty()) try { m.chapter_id = std::stoll(ch); } catch (...) {} }
        m.suppress_discord  = (get_param("suppress_discord") == "on" || get_param("suppress_discord") == "1");
        m.suppress_calendar = (get_param("suppress_calendar") == "on" || get_param("suppress_calendar") == "1");
        m.notes             = get_param("notes");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (m.title.empty()) {
            res.code = 400;
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Title is required.</div>)");
            return res;
        }

        try {
            meetings.create(m);
            res.add_header("HX-Trigger", "closeModal");
            res.add_header("HX-Redirect", "/meetings");
            res.code = 200;
            res.write(render_meeting_page(req, app, meetings, attendance));
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Error: )")
                + e.what() + "</div>");
        }
        return res;
    });

    // PUT /meetings/<id> - update meeting (form-encoded or JSON)
    CROW_ROUTE(app, "/meetings/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        {
            auto mtg = meetings.get(static_cast<int64_t>(id));
            if (!mtg) { res.code = 404; res.write(R"({"error":"not found"})"); res.end(); return res; }
            if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;
        }

        std::string content_type = req.get_header_value("Content-Type");
        bool is_form = content_type.find("application/x-www-form-urlencoded") != std::string::npos;

        Meeting updates;

        if (is_form) {
            auto params = crow::query_string("?" + req.body);
            auto gp = [&](const char* k) -> std::string {
                const char* v = params.get(k);
                return v ? std::string(v) : "";
            };
            std::string title = gp("title");
            if (!title.empty())       updates.title       = title;
            std::string desc = gp("description");
            if (!desc.empty())        updates.description = desc;
            std::string loc = gp("location");
            if (!loc.empty())         updates.location    = loc;
            std::string st = gp("start_time");
            if (!st.empty())          updates.start_time  = normalize_datetime(st);
            std::string et = gp("end_time");
            if (!et.empty())          updates.end_time    = normalize_datetime(et);
            std::string scope = gp("scope");
            if (!scope.empty())       updates.scope       = scope;
            std::string ch = gp("chapter_id");
            if (!ch.empty()) try { updates.chapter_id = std::stoll(ch); } catch (...) {}
            updates.suppress_discord  = (gp("suppress_discord") == "on" || gp("suppress_discord") == "1");
            updates.suppress_calendar = (gp("suppress_calendar") == "on" || gp("suppress_calendar") == "1");
            updates.notes             = gp("notes");

            res.add_header("Content-Type", "text/html; charset=utf-8");
            if (updates.title.empty()) {
                res.code = 400;
                res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Title is required.</div>)");
                return res;
            }
            try {
                meetings.update(static_cast<int64_t>(id), updates);
                res.add_header("HX-Trigger", "closeModal");
                res.add_header("HX-Redirect", "/meetings");
                res.code = 200;
                res.write(render_meeting_page(req, app, meetings, attendance));
            } catch (const std::exception& e) {
                res.code = 400;
                res.write(std::string(
                    R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Error: )")
                    + e.what() + "</div>");
            }
        } else {
            auto body = crow::json::load(req.body);
            if (!body) {
                res.code = 400;
                res.write(R"({"error":"Invalid JSON"})");
                res.add_header("Content-Type", "application/json");
                return res;
            }
            if (body.has("title"))       updates.title       = body["title"].s();
            if (body.has("description")) updates.description = body["description"].s();
            if (body.has("location"))    updates.location    = body["location"].s();
            if (body.has("start_time"))  updates.start_time  = normalize_datetime(body["start_time"].s());
            if (body.has("end_time"))    updates.end_time    = normalize_datetime(body["end_time"].s());
            if (body.has("scope"))       updates.scope       = body["scope"].s();
            try {
                auto updated = meetings.update(static_cast<int64_t>(id), updates);
                crow::json::wvalue resp;
                resp["id"]      = updated.id;
                resp["title"]   = updated.title;
                resp["success"] = true;
                res.write(resp.dump());
                res.add_header("Content-Type", "application/json");
            } catch (const std::exception& e) {
                res.code = 400;
                res.write(std::string(R"({"error":")") + e.what() + "\"}");
                res.add_header("Content-Type", "application/json");
            }
        }
        return res;
    });

    // POST /meetings/<id>/discord-sync - force-push current meeting data to Discord
    CROW_ROUTE(app, "/meetings/<int>/discord-sync").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto m = meetings.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Meeting not found</span>)");
            return res;
        }

        try {
            meetings.update(static_cast<int64_t>(id), *m);
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-green-600 text-xs">Synced</span>)");
        } catch (const std::exception& ex) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(std::string(R"(<span class="text-red-500 text-xs">Error: )") + ex.what() + "</span>");
        }
        return res;
    });

    // POST /meetings/<id>/cancel - cancel a meeting
    CROW_ROUTE(app, "/meetings/<int>/cancel").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        {
            auto mtg = meetings.get(static_cast<int64_t>(id));
            if (!mtg) { res.code = 404; res.write(R"({"error":"not found"})"); return res; }
            if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;
        }

        try {
            meetings.cancel(static_cast<int64_t>(id));
            res.add_header("HX-Redirect", "/meetings");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // POST /meetings/<id>/complete - mark meeting as complete
    CROW_ROUTE(app, "/meetings/<int>/complete").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        {
            auto mtg = meetings.get(static_cast<int64_t>(id));
            if (!mtg) { res.code = 404; res.write(R"({"error":"not found"})"); return res; }
            if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;
        }

        try {
            meetings.complete(static_cast<int64_t>(id));
            res.add_header("HX-Trigger", "meetingsUpdated");
            res.code = 200;
            res.write(R"({"success":true})");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + e.what() + "\"}");
        }
        res.add_header("Content-Type", "application/json");
        return res;
    });

    // GET /meetings/<id>/attendance - attendance list fragment
    CROW_ROUTE(app, "/meetings/<int>/attendance")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req);
        bool is_admin = auth.auth.role == "admin";

        auto attendees = attendance.get_attendees("meeting", static_cast<int64_t>(id));

        crow::mustache::context ctx;
        ctx["entity_id"]        = id;
        ctx["entity_type"]      = std::string("meeting");
        ctx["attendance_count"] = static_cast<int>(attendees.size());
        ctx["is_admin"]         = is_admin;
        ctx["is_meeting"]       = true;

        crow::json::wvalue arr;
        for (size_t i = 0; i < attendees.size(); ++i) {
            arr[i]["id"]                     = attendees[i].id;
            arr[i]["member_id"]              = attendees[i].member_id;
            arr[i]["member_display_name"]    = attendees[i].member_display_name;
            arr[i]["member_discord_username"]= attendees[i].member_discord_username;
            arr[i]["checked_in_at"]          = attendees[i].checked_in_at;
            arr[i]["notes"]                  = attendees[i].notes;
            arr[i]["is_virtual"]             = attendees[i].is_virtual;
            arr[i]["is_admin"]               = is_admin;
            arr[i]["is_meeting"]             = true;
            arr[i]["entity_type"]            = std::string("meeting");
            arr[i]["entity_id"]              = id;
        }
        ctx["attendees"] = std::move(arr);

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("meetings/_attendance.html");
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /meetings/<id>/checkin - check in current user (in-person or virtual)
    CROW_ROUTE(app, "/meetings/<int>/checkin").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth    = app.get_context<AuthMiddleware>(req);
        int64_t mbr_id = auth.auth.member_id;

        auto params = crow::query_string("?" + req.body);
        const char* notes_raw = params.get("notes");
        std::string notes = notes_raw ? std::string(notes_raw) : "";
        const char* virt_raw = params.get("virtual");
        bool is_virtual = virt_raw && std::string(virt_raw) == "1";

        std::string sid = std::to_string(id);

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "attendanceUpdated");
        bool already = attendance.is_checked_in(mbr_id, "meeting", static_cast<int64_t>(id));
        if (already) {
            attendance.check_out(mbr_id, "meeting", static_cast<int64_t>(id));
            // Return the segmented check-in button group
            res.write(
                R"(<div id="mtg-checkin-)" + sid + R"(" class="inline-flex rounded-full overflow-hidden border border-gray-300">)"
                R"(<button hx-post="/meetings/)" + sid + R"(/checkin" hx-vals='{"virtual":"0"}' )"
                R"(hx-swap="outerHTML" hx-target="#mtg-checkin-)" + sid + R"(")"
                R"( class="text-xs px-3 py-1 bg-yellow-400 text-gray-900 font-medium hover:bg-yellow-300 transition-colors flex items-center gap-1" title="Check in as attending in person">)"
                R"(<svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z"/></svg>)"
                R"(In-Person</button>)"
                R"(<button hx-post="/meetings/)" + sid + R"(/checkin" hx-vals='{"virtual":"1"}' )"
                R"(hx-swap="outerHTML" hx-target="#mtg-checkin-)" + sid + R"(")"
                R"( class="text-xs px-3 py-1 bg-indigo-50 text-indigo-700 font-medium hover:bg-indigo-100 transition-colors border-l border-gray-300 flex items-center gap-1" title="Check in as attending virtually">)"
                R"(<svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg>)"
                R"(Virtual</button>)"
                R"(</div>)");
        } else {
            attendance.check_in(mbr_id, "meeting", static_cast<int64_t>(id), notes, is_virtual);
            std::string icon = is_virtual
                ? R"(<svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg>)"
                : R"(<svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"/></svg>)";
            std::string label = is_virtual ? "Virtual" : "In-Person";
            std::string bg = is_virtual ? "bg-indigo-500 hover:bg-indigo-600" : "bg-green-600 hover:bg-green-700";
            res.write(
                R"(<div id="mtg-checkin-)" + sid + R"(">)"
                R"(<button hx-post="/meetings/)" + sid + R"(/checkin" )"
                R"(hx-swap="outerHTML" hx-target="#mtg-checkin-)" + sid + R"(")"
                R"( class="text-xs px-3 py-1 )" + bg + R"( text-white rounded-full font-medium transition-colors flex items-center gap-1">)"
                + icon + label + R"( &mdash; Cancel</button>)"
                R"(</div>)");
        }
        return res;
    });

    // POST /meetings/<id>/publish-report - publish notes+attendance to Discord forum
    CROW_ROUTE(app, "/meetings/<int>/publish-report").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto mtg = meetings.get(static_cast<int64_t>(id));
        if (!mtg) { res.code = 404; res.write("Not found"); return res; }
        if (!can_manage_chapter_content(req, res, app, mtg->chapter_id, chapter_members)) return res;

        // Build report with virtual/in-person split
        auto attendees = attendance.get_attendees("meeting", mtg->id);
        std::vector<std::string> in_person, virtual_list;
        for (const auto& a : attendees) {
            if (a.is_virtual) virtual_list.push_back(a.member_display_name);
            else              in_person.push_back(a.member_display_name);
        }

        std::ostringstream report;
        report << "# Meeting Report: " << mtg->title << "\n";
        report << "**Date:** " << mtg->start_time << " - " << mtg->end_time << "\n";
        if (!mtg->location.empty()) report << "**Location:** " << mtg->location << "\n";
        report << "**Attendance:** " << attendees.size()
               << " (" << in_person.size() << " in-person, "
               << virtual_list.size() << " virtual)\n\n";
        if (!in_person.empty()) {
            report << "## In-Person Attendees\n";
            for (const auto& n : in_person) report << "- " << n << "\n";
            report << "\n";
        }
        if (!virtual_list.empty()) {
            report << "## Virtual Attendees\n";
            for (const auto& n : virtual_list) report << "- " << n << "\n";
            report << "\n";
        }
        if (!mtg->notes.empty()) {
            report << "## Notes\n" << mtg->notes << "\n";
        }

        // Get forum channel — TODO: load from settings once discord_meeting_reports_forum_channel_id is configured
        std::string forum_id = discord.get_events_forum_channel_id(); // fallback

        std::string thread_id = discord.publish_report_to_forum(
            forum_id, mtg->notes_discord_post_id,
            "Report: " + mtg->title, report.str());

        if (!thread_id.empty() && thread_id != mtg->notes_discord_post_id) {
            meetings.repo().update_notes_discord_post_id(mtg->id, thread_id);
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(R"(<span class="text-green-600 text-xs">Report published!</span>)");
        return res;
    });
}
