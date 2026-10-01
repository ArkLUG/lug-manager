#include "routes/DisplayRoutes.hpp"
#include "routes/EventAccess.hpp"
#include "routes/RsvpRoutes.hpp"
#include "utils/ParseId.hpp"
#include "utils/Csv.hpp"
#include <crow/mustache.h>
#include <sstream>

namespace {

std::string inches_label(int w, int d) {
    if (w <= 0 || d <= 0) return "";
    return std::to_string(w) + "\" × " + std::to_string(d) + "\"";
}

std::string render_panel(const crow::request& req, LugApp& app, const LugEvent& ev,
                         DisplayRequestRepository& displays,
                         ChapterMemberRepository& chapter_members,
                         const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    bool manage = can_manage_event(req, app, ev, chapter_members);
    bool accepting = displays.accepts_displays(ev.id);

    crow::mustache::context ctx;
    ctx["event_id"]  = ev.id;
    ctx["accepting"] = accepting;
    ctx["open"]      = accepting && rsvp_open(ev);
    ctx["can_manage"]= manage;
    ctx["flash"]     = flash;

    auto rows = manage ? displays.list_for_event(ev.id) : displays.list_for_member(ev.id, a.member_id);
    crow::json::wvalue arr = crow::json::wvalue::list();
    long approved_sq_in = 0;
    int approved = 0, pending = 0, power = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        arr[i]["id"]          = r.id;
        arr[i]["event_id"]    = ev.id;
        arr[i]["title"]       = r.title;
        arr[i]["description"] = r.description;
        arr[i]["member"]      = r.member_display_name;
        arr[i]["size"]        = inches_label(r.width_in, r.depth_in);
        arr[i]["needs_power"] = r.needs_power;
        arr[i]["notes"]       = r.notes;
        arr[i]["table"]       = r.table_assignment;
        arr[i]["is_pending"]  = r.status == "pending";
        arr[i]["is_approved"] = r.status == "approved";
        arr[i]["is_declined"] = r.status == "declined";
        arr[i]["can_manage"]  = manage;
        arr[i]["can_withdraw"]= manage || (r.member_id == a.member_id && r.status == "pending");
        if (r.status == "approved") {
            ++approved;
            approved_sq_in += static_cast<long>(r.width_in) * r.depth_in;
            if (r.needs_power) ++power;
        } else if (r.status == "pending") {
            ++pending;
        }
    }
    ctx["requests"]     = std::move(arr);
    ctx["has_requests"] = !rows.empty();
    ctx["approved"]     = approved;
    ctx["pending"]      = pending;
    ctx["power"]        = power;
    char sqft[32];
    std::snprintf(sqft, sizeof(sqft), "%.1f", approved_sq_in / 144.0);
    ctx["approved_sqft"] = std::string(sqft);
    return crow::mustache::load("events/_displays.html").render(ctx).dump();
}


} // namespace

void register_display_routes(LugApp& app, EventService& events,
                             std::shared_ptr<DisplayRequestRepository> displays,
                             ChapterMemberRepository& chapter_members, AuditService& audit) {

    // GET /events/<id>/displays - panel fragment
    CROW_ROUTE(app, "/events/<int>/displays")([&app, &events, displays, &chapter_members](
            const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *ev, *displays, chapter_members));
        return res;
    });

    // POST /events/<id>/displays - submit a display request
    CROW_ROUTE(app, "/events/<int>/displays").methods("POST"_method)(
        [&app, &events, displays, &chapter_members, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!displays->accepts_displays(ev->id) || !rsvp_open(*ev)) {
            res.code = 409;
            res.write(render_panel(req, app, *ev, *displays, chapter_members,
                                   "Display requests are closed for this event."));
            return res;
        }
        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = params.get(k); return v ? std::string(v) : ""; };
        DisplayRequest r;
        r.event_id    = ev->id;
        r.member_id   = app.get_context<AuthMiddleware>(req).auth.member_id;
        r.title       = gp("title").substr(0, 200);
        r.description = gp("description").substr(0, 2000);
        r.notes       = gp("notes").substr(0, 1000);
        r.width_in    = static_cast<int>(std::min<int64_t>(parse_id(gp("width_in")), 10000));
        r.depth_in    = static_cast<int>(std::min<int64_t>(parse_id(gp("depth_in")), 10000));
        r.needs_power = gp("needs_power") == "on" || gp("needs_power") == "1";
        if (r.title.empty() || r.width_in <= 0 || r.depth_in <= 0) {
            res.code = 400;
            res.write(render_panel(req, app, *ev, *displays, chapter_members,
                                   "Give your display a name and a footprint (width and depth in inches)."));
            return res;
        }
        displays->create(r);
        audit.log(req, app, "event.display_request", "event", ev->id, ev->title, "Requested display: " + r.title);
        res.write(render_panel(req, app, *ev, *displays, chapter_members, "Request submitted."));
        return res;
    });

    // POST /events/<id>/displays/<rid>/review - approve/decline + table (managers)
    CROW_ROUTE(app, "/events/<int>/displays/<int>/review").methods("POST"_method)(
        [&app, &events, displays, &chapter_members, &audit](const crow::request& req, int id, int rid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        auto r = displays->find(rid);
        if (!ev || !r || r->event_id != ev->id) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = params.get(k); return v ? std::string(v) : ""; };
        std::string status = gp("status");
        if (status != "pending" && status != "approved" && status != "declined") status = r->status;
        std::string table = params.get("table_assignment") ? gp("table_assignment").substr(0, 100)
                                                          : r->table_assignment;
        displays->review(rid, status, table);
        audit.log(req, app, "event.display_review", "event", ev->id, ev->title,
                  r->title + " -> " + status + (table.empty() ? "" : " (table " + table + ")"));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *ev, *displays, chapter_members));
        return res;
    });

    // POST /events/<id>/displays/<rid>/delete - owner (while pending) or manager
    CROW_ROUTE(app, "/events/<int>/displays/<int>/delete").methods("POST"_method)(
        [&app, &events, displays, &chapter_members, &audit](const crow::request& req, int id, int rid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        auto r = displays->find(rid);
        if (!ev || !r || r->event_id != ev->id) { res.code = 404; return res; }
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        bool own_pending = r->member_id == a.member_id && r->status == "pending";
        if (!own_pending && !can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        displays->remove(rid);
        audit.log(req, app, "event.display_delete", "event", ev->id, ev->title, "Removed display: " + r->title);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *ev, *displays, chapter_members));
        return res;
    });

    // POST /events/<id>/displays/toggle - open/close requests (managers)
    CROW_ROUTE(app, "/events/<int>/displays/toggle").methods("POST"_method)(
        [&app, &events, displays, &chapter_members, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        bool on = !displays->accepts_displays(ev->id);
        displays->set_accepts_displays(ev->id, on);
        audit.log(req, app, "event.display_toggle", "event", ev->id, ev->title,
                  on ? "Opened display requests" : "Closed display requests");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *ev, *displays, chapter_members));
        return res;
    });

    // GET /events/<id>/displays.csv - managers
    CROW_ROUTE(app, "/events/<int>/displays.csv")([&app, &events, displays, &chapter_members](
            const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        std::ostringstream csv;
        csv << "Status,Table,Member,Display,Width (in),Depth (in),Needs power,Description,Notes\r\n";
        for (const auto& r : displays->list_for_event(ev->id)) {
            csv << csv_field(r.status) << ',' << csv_field(r.table_assignment) << ','
                << csv_field(r.member_display_name) << ',' << csv_field(r.title) << ','
                << r.width_in << ',' << r.depth_in << ',' << (r.needs_power ? "yes" : "no") << ','
                << csv_field(r.description) << ',' << csv_field(r.notes) << "\r\n";
        }
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"event-" + std::to_string(ev->id) + "-displays.csv\"");
        res.write(csv.str());
        return res;
    });
}
