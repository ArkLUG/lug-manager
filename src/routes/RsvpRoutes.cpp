#include "routes/RsvpRoutes.hpp"
#include "routes/EventAccess.hpp"
#include "utils/LocalTime.hpp"
#include "middleware/ApiKeyMiddleware.hpp"
#include "repositories/NotificationPrefs.hpp"
#include <crow/mustache.h>

bool rsvp_open(const LugEvent& ev) {
    if (ev.status == "cancelled") return false;
    std::tm now = local_tm(std::time(nullptr));
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M", &now);
    std::string now_iso = buf;
    std::string today = now_iso.substr(0, 10);
    std::string last_day = (ev.end_time.empty() ? ev.start_time : ev.end_time).substr(0, 10);
    if (!last_day.empty() && last_day < today) return false;
    if (!ev.signup_deadline.empty()) {
        // A date-only deadline means "through the end of that day".
        std::string dl = ev.signup_deadline.size() <= 10 ? ev.signup_deadline + "T23:59"
                                                          : ev.signup_deadline.substr(0, 16);
        if (now_iso > dl) return false;
    }
    return true;
}

namespace {

std::string render_panel(const crow::request& req, LugApp& app, const LugEvent& ev,
                         RsvpRepository& rsvps, ChapterMemberRepository& chapter_members,
                         const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    crow::mustache::context ctx;
    int going = rsvps.count(ev.id, "going");
    int waiting = rsvps.count(ev.id, "waitlist");
    ctx["event_id"]     = ev.id;
    ctx["going"]        = going;
    ctx["waitlist"]     = waiting;
    ctx["has_waitlist"] = waiting > 0;
    ctx["has_max"]      = ev.max_attendees > 0;
    ctx["max"]          = ev.max_attendees;
    ctx["is_full"]      = ev.max_attendees > 0 && going >= ev.max_attendees;
    ctx["open"]         = rsvp_open(ev);
    ctx["flash"]        = flash;
    auto mine = rsvps.status_of(ev.id, a.member_id);
    ctx["is_going"]     = mine && *mine == "going";
    ctx["is_waitlisted"]= mine && *mine == "waitlist";
    ctx["has_rsvp"]     = mine.has_value();
    if (mine && *mine == "waitlist") ctx["waitlist_pos"] = rsvps.waitlist_position(ev.id, a.member_id);

    bool manage = can_manage_event(req, app, ev, chapter_members);
    ctx["can_manage"] = manage;
    if (manage) {
        crow::json::wvalue arr;
        auto list = rsvps.list(ev.id);
        for (size_t i = 0; i < list.size(); ++i) {
            arr[i]["member_id"]   = list[i].member_id;
            arr[i]["name"]        = list[i].member_display_name;
            arr[i]["is_waitlist"] = list[i].status == "waitlist";
            arr[i]["event_id"]    = ev.id;
        }
        ctx["rsvps"] = std::move(arr);
        ctx["has_rsvps"] = !list.empty();
    }
    return crow::mustache::load("events/_rsvp.html").render(ctx).dump();
}

// Tell a member who just moved off the waitlist (unless they opted out).
void notify_promoted(const LugEvent& ev, int64_t member_id, Notifier& notifier, const std::string& tz) {
    notifier.notify(member_id, "waitlist", "You're in: " + ev.title,
        "\U0001F389 A spot opened up for **" + ev.title + "** (" + DiscordClient::friendly_time(ev.start_time, tz) +
        ") - you're off the waitlist and now going. If you can't make it, please cancel your RSVP so the next person gets the spot.",
        /*async=*/true);
}

} // namespace

void register_rsvp_routes(LugApp& app, EventService& events,
                          std::shared_ptr<RsvpRepository> rsvps,
                          ChapterMemberRepository& chapter_members, AuditService& audit,
                          std::shared_ptr<Notifier> notifier) {

    // GET /events/<id>/rsvp - RSVP panel fragment (event detail page)
    CROW_ROUTE(app, "/events/<int>/rsvp")([&app, &events, rsvps, &chapter_members](
            const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *ev, *rsvps, chapter_members));
        return res;
    });

    // POST /events/<id>/rsvp - toggle the current member's RSVP
    CROW_ROUTE(app, "/events/<int>/rsvp").methods("POST"_method)(
        [&app, &events, rsvps, &chapter_members, &audit, notifier](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        res.add_header("Content-Type", "text/html; charset=utf-8");

        std::string flash;
        if (rsvps->status_of(ev->id, a.member_id)) {
            auto promoted = rsvps->cancel(ev->id, a.member_id, ev->max_attendees);
            audit.log(req, app, "event.rsvp_cancel", "event", ev->id, ev->title, "Cancelled RSVP");
            if (promoted) {
                audit.log(req, app, "event.rsvp_promoted", "event", ev->id, ev->title,
                          "Member " + std::to_string(*promoted) + " moved off the waitlist");
                notify_promoted(*ev, *promoted, *notifier, notifier->timezone());
            }
            flash = "Your RSVP was cancelled.";
        } else if (!rsvp_open(*ev)) {
            res.code = 409;
            flash = "RSVPs are closed for this event.";
        } else {
            std::string st = rsvps->rsvp(ev->id, a.member_id, ev->max_attendees);
            audit.log(req, app, "event.rsvp", "event", ev->id, ev->title,
                      st == "going" ? "RSVP'd going" : "Joined waitlist");
            flash = st == "going" ? "You're going!" : "The event is full - you're on the waitlist.";
        }
        res.add_header("HX-Trigger", "rsvpUpdated");
        res.write(render_panel(req, app, *ev, *rsvps, chapter_members, flash));
        return res;
    });

    // POST /events/<id>/rsvp/<member_id>/remove - managers remove someone
    CROW_ROUTE(app, "/events/<int>/rsvp/<int>/remove").methods("POST"_method)(
        [&app, &events, rsvps, &chapter_members, &audit, notifier](const crow::request& req, int id, int member_id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto promoted = rsvps->cancel(ev->id, member_id, ev->max_attendees);
        audit.log(req, app, "event.rsvp_remove", "event", ev->id, ev->title,
                  "Removed RSVP of member " + std::to_string(member_id));
        if (promoted) {
            audit.log(req, app, "event.rsvp_promoted", "event", ev->id, ev->title,
                      "Member " + std::to_string(*promoted) + " moved off the waitlist");
            notify_promoted(*ev, *promoted, *notifier, notifier->timezone());
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "rsvpUpdated");
        res.write(render_panel(req, app, *ev, *rsvps, chapter_members));
        return res;
    });

    // GET /api/v1/events/<id>/rsvps - read scope
    CROW_ROUTE(app, "/api/v1/events/<int>/rsvps").methods("GET"_method)(
        [&app, &events, rsvps](const crow::request& req, int id) {
        crow::response res;
        if (!require_api_scope(req, res, app, "read")) return res;
        auto ev = events.get(id);
        res.add_header("Content-Type", "application/json");
        if (!ev) {
            res.code = 404;
            res.write(R"({"error":"event not found","code":"not_found"})");
            return res;
        }
        crow::json::wvalue body;
        auto list = rsvps->list(ev->id);
        crow::json::wvalue arr = crow::json::wvalue::list();
        for (size_t i = 0; i < list.size(); ++i) {
            arr[i]["member_id"]    = list[i].member_id;
            arr[i]["display_name"] = list[i].member_display_name;
            arr[i]["status"]       = list[i].status;
            arr[i]["created_at"]   = list[i].created_at;
        }
        body["data"]["event_id"]      = ev->id;
        body["data"]["max_attendees"] = ev->max_attendees;
        body["data"]["going"]         = rsvps->count(ev->id, "going");
        body["data"]["waitlist"]      = rsvps->count(ev->id, "waitlist");
        body["data"]["rsvps"]         = std::move(arr);
        res.write(body.dump());
        return res;
    });
}
