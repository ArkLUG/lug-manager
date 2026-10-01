#include "routes/attendance/AttendanceRoutes.hpp"
#include "utils/LocalTime.hpp"
#include "services/members/PerkProgress.hpp"
#include "utils/text/HtmlEscape.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <ctime>
#include <algorithm>
#include <map>

// Returns true if the current user is admin, event lead, or has event_manager/lead
// chapter role for the entity's chapter.
static bool can_manage_attendance(const crow::request& req, LugApp& app,
                                   EventService& events, MeetingService& meetings,
                                   ChapterMemberRepository& chapter_members,
                                   const std::string& entity_type, int64_t entity_id) {
    auto& auth = app.get_context<AuthMiddleware>(req);
    if (auth.auth.role == "admin") return true;

    int64_t chapter_id = 0;
    if (entity_type == "event" && auth.auth.member_id > 0) {
        auto ev = events.get(entity_id);
        if (ev) {
            if (ev->event_lead_id == auth.auth.member_id) return true;
            chapter_id = ev->chapter_id;
        }
    } else if (entity_type == "meeting" && auth.auth.member_id > 0) {
        auto mtg = meetings.get(entity_id);
        if (mtg) chapter_id = mtg->chapter_id;
    }

    // Check chapter role: event_manager or lead can manage attendance
    if (chapter_id > 0 && auth.auth.member_id > 0) {
        auto role = chapter_members.get_chapter_role(auth.auth.member_id, chapter_id);
        if (role && chapter_role_rank(*role) >= chapter_role_rank("event_manager"))
            return true;
    }
    return false;
}

static bool is_entity_type(const std::string& t) {
    return t == "event" || t == "meeting";
}

// Build the attendance list context and render the template fragment.
// Events use a per-day grouped template; meetings use the flat list.
// Minors (KFOL/TFOL) in an attendance list, with their consent/photo status,
// so organisers see "no photos" / "consent missing" right where they check in.
static MemberRepository* g_member_repo_for_badges = nullptr;
static void add_minor_badges(crow::json::wvalue& rows, const std::vector<int64_t>& ids) {
    if (!g_member_repo_for_badges || ids.empty() || !Features::on("guardians")) return;
    auto flags = g_member_repo_for_badges->minor_flags(ids);
    for (size_t i = 0; i < ids.size(); ++i) {
        auto it = flags.find(ids[i]);
        bool minor = it != flags.end();
        rows[i]["is_minor"] = minor;
        rows[i]["no_consent"] = minor && !it->second.first;
        rows[i]["no_photos"] = minor && !it->second.second;
    }
}

static std::string render_attendance_list(AttendanceService& attendance,
                                           const std::string& entity_type, int64_t entity_id,
                                           bool can_manage, bool is_meeting) {
    if (entity_type == "event") {
        auto days = attendance.event_day_repo().find_by_event(entity_id);
        std::string today = AttendanceService::today_ymd();

        crow::mustache::context ctx;
        ctx["entity_type"]   = entity_type;
        ctx["entity_id"]     = static_cast<int>(entity_id);
        ctx["is_admin"]      = can_manage;

        int distinct = attendance.event_day_attendance_repo().count_distinct_members_by_event(entity_id);
        ctx["distinct_count"] = distinct;
        ctx["one_member"]     = (distinct == 1);

        crow::json::wvalue days_arr;
        for (size_t i = 0; i < days.size(); ++i) {
            const auto& d = days[i];
            days_arr[i]["day_id"]     = d.id;
            days_arr[i]["day_number"] = d.day_number;
            days_arr[i]["day_date"]   = friendly_date(d.day_date);
            days_arr[i]["is_today"]   = (d.day_date == today);
            days_arr[i]["entity_id"]  = static_cast<int>(entity_id);
            days_arr[i]["is_admin"]   = can_manage;

            auto day_rows = attendance.event_day_attendance_repo().find_by_day(d.id);
            crow::json::wvalue att_arr;
            for (size_t j = 0; j < day_rows.size(); ++j) {
                const auto& r = day_rows[j];
                att_arr[j]["id"]                     = r.id;
                att_arr[j]["member_id"]              = r.member_id;
                att_arr[j]["member_display_name"]    = r.member_display_name;
                att_arr[j]["member_discord_username"]= r.member_discord_username;
                att_arr[j]["has_discord_username"]   = !r.member_discord_username.empty();
                att_arr[j]["checked_in_at"]          = r.checked_in_at;
                att_arr[j]["is_admin"]               = can_manage;
                att_arr[j]["entity_id"]              = static_cast<int>(entity_id);
                att_arr[j]["day_number"]             = d.day_number;
            }
            {
                std::vector<int64_t> ids;
                for (const auto& r : day_rows) ids.push_back(r.member_id);
                add_minor_badges(att_arr, ids);
            }
            days_arr[i]["attendees"] = std::move(att_arr);
        }
        ctx["days"] = std::move(days_arr);
        auto tmpl = crow::mustache::load("events/_attendance.html");
        return tmpl.render(ctx).dump();
    }

    auto attendees = attendance.get_attendees(entity_type, entity_id);
    crow::mustache::context ctx;
    ctx["entity_type"]      = entity_type;
    ctx["entity_id"]        = static_cast<int>(entity_id);
    ctx["attendance_count"] = static_cast<int>(attendees.size());
    ctx["is_admin"]         = can_manage;
    ctx["is_meeting"]       = is_meeting;

    crow::json::wvalue arr;
    for (size_t i = 0; i < attendees.size(); ++i) {
        arr[i]["id"]                     = attendees[i].id;
        arr[i]["member_id"]              = attendees[i].member_id;
        arr[i]["member_display_name"]    = attendees[i].member_display_name;
        arr[i]["member_discord_username"]= attendees[i].member_discord_username;
        arr[i]["has_discord_username"]   = !attendees[i].member_discord_username.empty();
        arr[i]["checked_in_at"]          = attendees[i].checked_in_at;
        arr[i]["notes"]                  = attendees[i].notes;
        arr[i]["is_virtual"]             = attendees[i].is_virtual;
        // Propagate into each row so Mustache can access inside {{#attendees}}
        arr[i]["is_admin"]               = can_manage;
        arr[i]["is_meeting"]             = is_meeting;
        arr[i]["entity_type"]            = entity_type;
        arr[i]["entity_id"]              = static_cast<int>(entity_id);
    }
    {
        std::vector<int64_t> ids;
        for (const auto& a : attendees) ids.push_back(a.member_id);
        add_minor_badges(arr, ids);
    }
    ctx["attendees"] = std::move(arr);
    auto tmpl = crow::mustache::load("meetings/_attendance.html");
    return tmpl.render(ctx).dump();
}

// Helper to look up the title of an event or meeting by type and ID
static std::string get_entity_title(EventService& events, MeetingService& meetings,
                                     const std::string& entity_type, int64_t entity_id) {
    if (entity_type == "event") {
        auto ev = events.get(entity_id);
        return ev ? ev->title : "";
    } else if (entity_type == "meeting") {
        auto mtg = meetings.get(entity_id);
        return mtg ? mtg->title : "";
    }
    return "";
}

void register_attendance_routes(LugApp& app, AttendanceService& attendance,
                                EventService& events, MeetingService& meetings,
                                MemberService& members,
                                ChapterMemberRepository& chapter_members,
                                PerkLevelRepository& perks, AuditService& audit) {
    g_member_repo_for_badges = &members.repo();

    // GET /attendance/overview - admin/lead attendance overview for all members
    // Query param: ?year=YYYY (defaults to current year)
    CROW_ROUTE(app, "/attendance/overview")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        if (!require_auth(req, res, app, "admin")) return res;

        // Parse query params
        const int current_year = local_year();

        auto qs = crow::query_string(req.url_params);
        auto gp = [&](const char* k) -> std::string { const char* v = qs.get(k); return v ? v : ""; };

        AttendanceRepository::OverviewParams params;
        params.year = current_year;
        { std::string y = gp("year"); if (!y.empty()) try { params.year = std::stoi(y); } catch (...) {} }
        params.search = gp("search");
        { std::string sc = gp("sort"); if (!sc.empty()) params.sort_col = sc; }
        { std::string sd = gp("dir");  if (!sd.empty()) params.sort_dir = sd; }
        params.hide_inactive = (gp("hide_inactive") == "1");

        int page = 1;
        { std::string p = gp("page"); if (!p.empty()) try { page = std::stoi(p); } catch (...) {} }
        if (page < 1) page = 1;
        constexpr int per_page = 25;

        int total_count = attendance.count_overview(params);
        int total_pages = (total_count + per_page - 1) / per_page;
        if (total_pages < 1) total_pages = 1;
        if (page > total_pages) page = total_pages;
        params.limit = per_page;
        params.offset = (page - 1) * per_page;

        auto summaries = attendance.get_overview_paginated(params);

        // Year dropdown
        auto years = attendance.get_attendance_years();
        if (std::find(years.begin(), years.end(), current_year) == years.end())
            years.insert(years.begin(), current_year);

        // Perk levels for tier computation (year-specific)
        auto perk_levels = perks.find_by_year(params.year);

        crow::mustache::context ctx;
        ctx["is_admin"]       = true;
        ctx["selected_year"]  = params.year;
        ctx["has_perks"]      = !perk_levels.empty();
        ctx["search"]         = params.search;
        ctx["sort"]           = params.sort_col;
        ctx["dir"]            = params.sort_dir;
        ctx["hide_inactive"]  = params.hide_inactive;
        // Sort direction flags for column header toggle links
        ctx["sort_name_asc"]  = (params.sort_col == "display_name" && params.sort_dir == "asc");
        ctx["sort_mtg_asc"]   = (params.sort_col == "meeting_count" && params.sort_dir == "asc");
        ctx["sort_evt_asc"]   = (params.sort_col == "event_count" && params.sort_dir == "asc");
        ctx["sort_tot_asc"]   = (params.sort_col == "total" && params.sort_dir == "asc");
        ctx["sort_last_asc"]  = (params.sort_col == "last_attendance" && params.sort_dir == "asc");
        ctx["page"]           = page;
        ctx["total_pages"]    = total_pages;
        ctx["total_count"]    = total_count;
        ctx["has_prev"]       = (page > 1);
        ctx["has_next"]       = (page < total_pages);
        ctx["prev_page"]      = page - 1;
        ctx["next_page"]      = page + 1;

        crow::json::wvalue year_arr;
        for (size_t i = 0; i < years.size(); ++i) {
            year_arr[i]["year"]     = years[i];
            year_arr[i]["selected"] = (years[i] == params.year);
        }
        ctx["years"] = std::move(year_arr);

        crow::json::wvalue perk_arr;
        for (size_t i = 0; i < perk_levels.size(); ++i) {
            perk_arr[i]["name"]     = perk_levels[i].name;
            perk_arr[i]["meetings"] = perk_levels[i].meeting_attendance_required;
            perk_arr[i]["events"]   = perk_levels[i].event_attendance_required;
            perk_arr[i]["paid"]     = perk_levels[i].requires_paid_dues;
            perk_arr[i]["min_fol"]  = perk_levels[i].min_fol_status;
            perk_arr[i]["has_fol_req"] = (perk_levels[i].min_fol_status != "kfol");
        }
        ctx["perk_levels"] = std::move(perk_arr);

        // Build member rows with tier
        int active_count = 0;
        crow::json::wvalue arr;
        for (size_t i = 0; i < summaries.size(); ++i) {
            auto& s = summaries[i];
            int total = s.meeting_count + s.event_count;
            // Perk eligibility only counts in-person meeting attendance -
            // virtual never counts toward a tier, same rule as "verified
            // member" status.
            int meeting_count_in_person = s.meeting_count - s.meeting_virtual_count;

            std::string tier_name;
            for (const auto& lvl : perk_levels) {
                if (meets_perk_level(lvl, meeting_count_in_person, s.event_count, s.is_paid, s.fol_status)) {
                    tier_name = lvl.name;
                }
            }

            arr[i]["member_id"]             = s.member_id;
            arr[i]["display_name"]          = s.display_name;
            arr[i]["first_name"]            = s.first_name;
            arr[i]["last_name"]             = s.last_name;
            arr[i]["full_name"]             = s.first_name + " " + s.last_name;
            arr[i]["discord_username"]      = s.discord_username;
            arr[i]["has_discord_username"]  = !s.discord_username.empty();
            arr[i]["meeting_count"]         = s.meeting_count;
            arr[i]["meeting_virtual_count"] = s.meeting_virtual_count;
            arr[i]["meeting_in_person"]     = s.meeting_count - s.meeting_virtual_count;
            arr[i]["event_count"]           = s.event_count;
            arr[i]["total"]                 = total;
            arr[i]["has_attendance"]        = (total > 0);
            arr[i]["is_paid"]               = s.is_paid;
            // Trim to date-only for display
            std::string last_date = s.last_attendance.size() >= 10 ? s.last_attendance.substr(0, 10) : s.last_attendance;
            arr[i]["last_attendance"]       = last_date;
            arr[i]["has_last"]              = !last_date.empty();
            arr[i]["tier_name"]             = tier_name;
            arr[i]["has_tier"]              = !tier_name.empty();
            arr[i]["selected_year"]         = params.year;
            if (total > 0) ++active_count;
        }
        ctx["members"]       = std::move(arr);
        ctx["member_count"]  = total_count;
        ctx["active_count"]  = active_count;

        return html_page(req, app, crow::mustache::load("attendance/_overview.html").render(ctx).dump(), "Attendance Overview", "active_attendance_overview");
    });

    // GET /attendance - personal attendance history for current user
    CROW_ROUTE(app, "/attendance")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& ctx_auth = app.get_context<AuthMiddleware>(req);
        auto history   = attendance.get_member_history(ctx_auth.auth.member_id);

        crow::mustache::context ctx;
        ctx["title"]     = "My Attendance";
        ctx["member_id"] = ctx_auth.auth.member_id;

        crow::json::wvalue arr;
        const std::string this_year = std::to_string(local_year());
        int year_meetings = 0, year_events = 0;
        for (size_t i = 0; i < history.size(); ++i) {
            arr[i]["entity_type"]  = history[i].entity_type;
            arr[i]["entity_id"]    = history[i].entity_id;
            arr[i]["checked_in_at"]= history[i].checked_in_at;
            arr[i]["notes"]        = history[i].notes;
            arr[i]["has_notes"]    = !history[i].notes.empty();
            arr[i]["is_virtual"]   = history[i].is_virtual;
            arr[i]["type_meeting"] = (history[i].entity_type == "meeting");
            arr[i]["type_event"]   = (history[i].entity_type == "event");
            arr[i]["days_attended"]     = history[i].days_attended;
            arr[i]["multi_day"]         = history[i].days_attended > 1;
            arr[i]["entity_url"]   = "/" + history[i].entity_type + "s/" +
                                     std::to_string(history[i].entity_id);

            // Look up entity title
            std::string title, start;
            if (history[i].entity_type == "meeting") {
                auto m = meetings.get(history[i].entity_id);
                if (m) { title = m->title; start = m->start_time; }
            } else {
                auto e = events.get(history[i].entity_id);
                if (e) { title = e->title; start = e->start_time; }
            }
            arr[i]["when"] = friendly_date(start.empty() ? history[i].checked_in_at : start);
            if ((start.empty() ? history[i].checked_in_at : start).substr(0, 4) == this_year)
                ++(history[i].entity_type == "meeting" ? year_meetings : year_events);
            arr[i]["entity_title"] = title.empty() ? (history[i].entity_type + " #" + std::to_string(history[i].entity_id)) : title;
            arr[i]["entity_label"] = history[i].entity_type == "meeting" ? "Meeting" : "Event";
        }
        ctx["history"]       = std::move(arr);
        ctx["history_count"] = static_cast<int>(history.size());
        ctx["this_year"]     = this_year;
        ctx["year_meetings"] = year_meetings;
        ctx["year_events"]   = year_events;
        ctx["has_history"]   = !history.empty();

        return html_page(req, app, crow::mustache::load("attendance/_content.html").render(ctx).dump(), "My Attendance", "active_attendance");
    });

    // GET /attendance/count/<type>/<id> - returns live count (for HTMX polling)
    CROW_ROUTE(app, "/attendance/count/<str>/<int>")(
        [&](const crow::request& req, std::string entity_type, int entity_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        // entity_type is echoed into HTML below - only ever accept the two known values.
        if (!is_entity_type(entity_type)) { res.code = 404; return res; }

        int count = attendance.get_count(entity_type, static_cast<int64_t>(entity_id));
        std::string eid = std::to_string(entity_id);
        res.write(
            "<span id=\"attendance-count-" + entity_type + "-" + eid + "\""
            " class=\"text-xs text-gray-400\""
            " hx-get=\"/attendance/count/" + entity_type + "/" + eid + "\""
            " hx-trigger=\"attendanceUpdated from:body\""
            " hx-swap=\"outerHTML\">"
            "<span class=\"font-semibold\">" + std::to_string(count) + "</span> checked in"
            "</span>");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // GET /attendance/list/<type>/<id> - returns attendee list fragment
    CROW_ROUTE(app, "/attendance/list/<str>/<int>")(
        [&](const crow::request& req, std::string entity_type, int entity_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        if (!is_entity_type(entity_type)) { res.code = 404; return res; }

        bool can_manage = can_manage_attendance(req, app, events, meetings, chapter_members,
                                                entity_type, static_cast<int64_t>(entity_id));
        res.write(render_attendance_list(attendance, entity_type,
                                         static_cast<int64_t>(entity_id),
                                         can_manage, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        return res;
    });

    // POST /attendance/admin/checkin - admin/event lead adds a member to an entity
    CROW_ROUTE(app, "/attendance/admin/checkin").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) -> std::string {
            const char* v = params.get(k); return v ? std::string(v) : "";
        };
        std::string entity_type = gp("entity_type");
        std::string entity_id_s = gp("entity_id");
        bool is_virtual = gp("is_virtual") == "1";

        // Support multiple member_id values (multi-select)
        auto member_ids = params.get_list("member_id", false);

        int64_t entity_id = 0;
        try { entity_id = std::stoll(entity_id_s); } catch (...) {}
        if (!is_entity_type(entity_type) || entity_id <= 0 || member_ids.empty()) {
            res.code = 400;
            res.write(R"(<span class="text-red-500 text-xs">Select at least one member</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        if (!can_manage_attendance(req, app, events, meetings, chapter_members, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            return res;
        }

        int skipped = 0;
        for (auto* mid_str : member_ids) {
            if (!mid_str || !mid_str[0]) continue;
            try {
                int64_t member_id = std::stoll(mid_str);
                // false = nothing written: duplicate, or (events) today isn't an event day
                if (!attendance.check_in(member_id, entity_type, entity_id, "", is_virtual)) {
                    ++skipped;
                    continue;
                }
                auto checkin_mbr = members.get(member_id);
                std::string checkin_mbr_name = checkin_mbr ? checkin_mbr->display_name : std::to_string(member_id);
                audit.log(req, app, "attendance.checkin", entity_type, entity_id,
                          get_entity_title(events, meetings, entity_type, entity_id),
                          "Admin checked in " + checkin_mbr_name);
            } catch (...) { ++skipped; }
        }

        if (skipped > 0) {
            res.write("<p class=\"text-amber-600 text-xs mb-2\">" + std::to_string(skipped) +
                      (entity_type == "event"
                           ? " not added - already checked in, or today isn't one of this event's days"
                             " (add them to a specific day instead).</p>"
                           : " not added - already checked in.</p>"));
        }
        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // POST /attendance/admin/<id>/remove - admin/event lead removes an attendance record.
    // Permission is checked against the entity the record actually belongs to,
    // never a caller-supplied entity (which would let a manager of one event
    // touch records of any other).
    CROW_ROUTE(app, "/attendance/admin/<int>/remove").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto rec = attendance.repo().find_by_id(static_cast<int64_t>(id));
        if (!rec) {
            res.code = 404;
            res.write(R"(<span class="text-red-500 text-xs">Record not found</span>)");
            return res;
        }
        const std::string& entity_type = rec->entity_type;
        int64_t entity_id = rec->entity_id;
        if (!can_manage_attendance(req, app, events, meetings, chapter_members, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            return res;
        }

        attendance.remove_by_id(rec->id);
        audit.log(req, app, "attendance.remove", entity_type, entity_id,
                  get_entity_title(events, meetings, entity_type, entity_id),
                  "Admin removed attendance record");
        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // POST /attendance/admin/<id>/toggle-virtual - admin/event lead toggles virtual flag
    CROW_ROUTE(app, "/attendance/admin/<int>/toggle-virtual").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        auto rec = attendance.repo().find_by_id(static_cast<int64_t>(id));
        if (!rec) {
            res.code = 404;
            res.write(R"(<span class="text-red-500 text-xs">Record not found</span>)");
            return res;
        }
        const std::string& entity_type = rec->entity_type;
        int64_t entity_id = rec->entity_id;
        if (!can_manage_attendance(req, app, events, meetings, chapter_members, entity_type, entity_id)) {
            res.code = 403;
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            return res;
        }

        // Flip the stored value rather than trusting a client-sent "current".
        attendance.set_virtual(rec->id, !rec->is_virtual);
        audit.log(req, app, "attendance.toggle_virtual", entity_type, entity_id,
                  get_entity_title(events, meetings, entity_type, entity_id), "Toggled virtual");
        res.write(render_attendance_list(attendance, entity_type, entity_id,
                                         true, entity_type == "meeting"));
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // GET /attendance/member/<id>/detail?year=YYYY&page=N — member's attended events/meetings
    CROW_ROUTE(app, "/attendance/member/<int>/detail")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        // Used by the admin-only overview; members may only see their own history.
        {
            auto& a = app.get_context<AuthMiddleware>(req).auth;
            if (!a.is_admin() && a.member_id != static_cast<int64_t>(id)) {
                res.code = 403;
                res.write("Forbidden");
                return res;
            }
        }

        auto qs = crow::query_string(req.url_params);
        auto gp = [&](const char* k) -> std::string { const char* v = qs.get(k); return v ? v : ""; };

        int year = local_year();
        { std::string y = gp("year"); if (!y.empty()) try { year = std::stoi(y); } catch (...) {} }

        int page = 1;
        { std::string p = gp("page"); if (!p.empty()) try { page = std::stoi(p); } catch (...) {} }
        if (page < 1) page = 1;
        constexpr int per_page = 15;

        auto& att_repo = attendance; // service wraps repo
        int total = att_repo.repo().count_member_attendance_detail(static_cast<int64_t>(id), year);
        int total_pages = (total + per_page - 1) / per_page;
        if (total_pages < 1) total_pages = 1;
        if (page > total_pages) page = total_pages;

        auto details = att_repo.repo().get_member_attendance_detail(
            static_cast<int64_t>(id), year, per_page, (page - 1) * per_page);

        // Build HTML fragment
        std::ostringstream html;
        html << "<div class=\"text-xs space-y-1 py-2\">";
        if (details.empty()) {
            html << "<p class=\"text-gray-400\">No attendance for " << year << ".</p>";
        }
        for (const auto& d : details) {
            std::string type_badge = d.entity_type == "meeting"
                ? "<span class=\"px-1.5 py-0.5 rounded bg-blue-100 text-blue-700 font-medium\">Mtg</span>"
                : "<span class=\"px-1.5 py-0.5 rounded bg-purple-100 text-purple-700 font-medium\">Evt</span>";
            html << "<div class=\"flex items-center gap-2\">"
                 << type_badge
                 << "<span class=\"text-gray-700\">" << html_escape(d.title) << "</span>"
                 << "<span class=\"text-gray-400\">" << html_escape(d.date) << "</span>";
            if (d.is_virtual) html << "<span class=\"px-1 py-0.5 rounded bg-indigo-100 text-indigo-600\">Virtual</span>";
            html << "</div>";
        }
        // Pagination
        if (total_pages > 1) {
            html << "<div class=\"flex items-center gap-2 pt-1\">";
            if (page > 1)
                html << "<button hx-get=\"/attendance/member/" << id << "/detail?year=" << year << "&page=" << (page - 1)
                     << "\" hx-target=\"#member-detail-" << id << "\" hx-swap=\"innerHTML\""
                     << " class=\"px-2 py-0.5 border border-gray-300 rounded text-gray-600 hover:bg-gray-50\">Prev</button>";
            html << "<span class=\"text-gray-400\">Page " << page << "/" << total_pages << "</span>";
            if (page < total_pages)
                html << "<button hx-get=\"/attendance/member/" << id << "/detail?year=" << year << "&page=" << (page + 1)
                     << "\" hx-target=\"#member-detail-" << id << "\" hx-swap=\"innerHTML\""
                     << " class=\"px-2 py-0.5 border border-gray-300 rounded text-gray-600 hover:bg-gray-50\">Next</button>";
            html << "</div>";
        }
        html << "</div>";

        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(html.str());
        return res;
    });

    // POST /attendance/admin/event-day/<day_id>/checkin — admin/lead adds one or more
    // members directly to a specific event day.
    CROW_ROUTE(app, "/attendance/admin/event-day/<int>/checkin").methods("POST"_method)(
        [&](const crow::request& req, int day_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto day = attendance.event_day_repo().find_by_id(static_cast<int64_t>(day_id));
        if (!day) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Day not found</span>)");
            return res;
        }
        int64_t event_id = day->event_id;

        if (!can_manage_attendance(req, app, events, meetings, chapter_members, "event", event_id)) {
            res.code = 403;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            return res;
        }

        auto params = crow::query_string("?" + req.body);
        auto member_ids = params.get_list("member_id", false);
        for (auto* mid_str : member_ids) {
            if (!mid_str || !mid_str[0]) continue;
            try {
                int64_t member_id = std::stoll(mid_str);
                if (!attendance.check_in_to_day(member_id, static_cast<int64_t>(day_id)))
                    continue; // already on this day - nothing to audit
                auto mbr = members.get(member_id);
                std::string mbr_name = mbr ? mbr->display_name : std::to_string(member_id);
                audit.log(req, app, "attendance.event_day_checkin", "event", event_id,
                          get_entity_title(events, meetings, "event", event_id),
                          "Admin added " + mbr_name + " to day " + std::to_string(day->day_number));
            } catch (...) {}
        }

        res.write(render_attendance_list(attendance, "event", event_id,
                                         true, false));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });

    // POST /attendance/admin/event-day-att/<id>/remove
    CROW_ROUTE(app, "/attendance/admin/event-day-att/<int>/remove").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto row = attendance.event_day_attendance_repo().find_by_id(static_cast<int64_t>(id));
        if (!row) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Record not found</span>)");
            return res;
        }
        auto day = attendance.event_day_repo().find_by_id(row->event_day_id);
        int64_t event_id = day ? day->event_id : 0;

        if (!can_manage_attendance(req, app, events, meetings, chapter_members, "event", event_id)) {
            res.code = 403;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<span class="text-red-500 text-xs">Forbidden</span>)");
            return res;
        }

        attendance.event_day_attendance_repo().remove_by_id(static_cast<int64_t>(id));
        audit.log(req, app, "attendance.event_day_remove", "event", event_id,
                  get_entity_title(events, meetings, "event", event_id),
                  "Admin removed event day attendance");
        res.write(render_attendance_list(attendance, "event", event_id, true, false));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "attendanceUpdated");
        return res;
    });
}
