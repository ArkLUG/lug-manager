#include "routes/ShiftRoutes.hpp"
#include "routes/EventAccess.hpp"
#include "utils/ParseId.hpp"
#include <crow/mustache.h>
#include <regex>

namespace {

std::string fmt_when(const std::string& s, const std::string& e) {
    // "2026-06-01T09:00" .. "2026-06-01T12:00" -> "Jun 1, 9:00 AM – 12:00 PM"
    static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int y, mo, d, h, mi, h2 = 0, mi2 = 0, y2, mo2, d2;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5 || mo < 1 || mo > 12) return s;
    std::sscanf(e.c_str(), "%d-%d-%dT%d:%d", &y2, &mo2, &d2, &h2, &mi2);
    auto t = [](int hh, int mm) {
        char b[16]; std::snprintf(b, sizeof(b), "%d:%02d %s", hh % 12 == 0 ? 12 : hh % 12, mm, hh >= 12 ? "PM" : "AM");
        return std::string(b);
    };
    return std::string(mon[mo - 1]) + " " + std::to_string(d) + ", " + t(h, mi) + " – " + t(h2, mi2);
}

std::string render(const crow::request& req, LugApp& app, const LugEvent& ev, ShiftRepository& shifts,
                   ChapterMemberRepository& cm, const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    bool manage = can_manage_event(req, app, ev, cm);
    crow::mustache::context ctx;
    ctx["event_id"] = ev.id;
    ctx["can_manage"] = manage;
    ctx["flash"] = flash;
    ctx["default_day"] = ev.start_time.substr(0, 10);
    auto list = shifts.for_event(ev.id);
    crow::json::wvalue arr = crow::json::wvalue::list();
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& s = list[i];
        bool mine = false;
        std::string names;
        for (const auto& v : s.volunteers) {
            if (v.first == a.member_id) mine = true;
            names += (names.empty() ? "" : ", ") + v.second;
        }
        arr[i]["id"] = s.id;
        arr[i]["event_id"] = ev.id;
        arr[i]["title"] = s.title;
        arr[i]["when"] = fmt_when(s.starts_at, s.ends_at);
        arr[i]["notes"] = s.notes;
        arr[i]["taken"] = s.taken;
        arr[i]["slots"] = s.slots;
        arr[i]["full"] = s.taken >= s.slots;
        arr[i]["mine"] = mine;
        arr[i]["can_join"] = !mine && s.taken < s.slots;
        arr[i]["names"] = names;
        arr[i]["can_manage"] = manage;
    }
    ctx["shifts"] = std::move(arr);
    ctx["has_shifts"] = !list.empty();
    return crow::mustache::load("events/_shifts.html").render(ctx).dump();
}

} // namespace

void register_shift_routes(LugApp& app, EventService& events, std::shared_ptr<ShiftRepository> shifts,
                           ChapterMemberRepository& chapter_members, AuditService& audit) {
    CROW_ROUTE(app, "/events/<int>/shifts")([&app, &events, shifts, &chapter_members](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, *ev, *shifts, chapter_members));
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/shifts").methods("POST"_method)(
        [&app, &events, shifts, &chapter_members, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto p = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = p.get(k); return v ? std::string(v) : ""; };
        EventShift s;
        s.event_id = ev->id;
        s.title = gp("title").substr(0, 100);
        s.notes = gp("notes").substr(0, 500);
        std::string day = gp("day"), from = gp("from"), to = gp("to");
        s.starts_at = day + "T" + from;
        s.ends_at = day + "T" + to;
        s.slots = static_cast<int>(std::min<int64_t>(parse_id(gp("slots")), 100));
        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})"), hm(R"(([01]\d|2[0-3]):[0-5]\d)");
        if (s.title.empty() || !std::regex_match(day, ymd) || !std::regex_match(from, hm) ||
            !std::regex_match(to, hm) || to <= from || s.slots < 1) {
            res.code = 400;
            res.write(render(req, app, *ev, *shifts, chapter_members,
                             "Give the shift a name, a day, a start/end time and at least one slot."));
            return res;
        }
        shifts->create(s);
        audit.log(req, app, "event.shift_create", "event", ev->id, ev->title, "Added shift: " + s.title);
        res.write(render(req, app, *ev, *shifts, chapter_members));
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/shifts/<int>/delete").methods("POST"_method)(
        [&app, &events, shifts, &chapter_members, &audit](const crow::request& req, int id, int sid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        if (shifts->remove(sid, ev->id))
            audit.log(req, app, "event.shift_delete", "event", ev->id, ev->title, "Removed shift #" + std::to_string(sid));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, *ev, *shifts, chapter_members));
        return res;
    });

    // Toggle the current member's signup for a shift
    CROW_ROUTE(app, "/events/<int>/shifts/<int>/signup").methods("POST"_method)(
        [&app, &events, shifts, &chapter_members, &audit](const crow::request& req, int id, int sid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        auto sh = shifts->find(sid);
        if (!ev || !sh || sh->event_id != ev->id) { res.code = 404; return res; }
        int64_t me = app.get_context<AuthMiddleware>(req).auth.member_id;
        std::string flash;
        if (shifts->is_signed_up(sid, me)) {
            shifts->withdraw(sid, me);
            audit.log(req, app, "event.shift_withdraw", "event", ev->id, ev->title, "Left shift: " + sh->title);
        } else if (shifts->sign_up(sid, me)) {
            audit.log(req, app, "event.shift_signup", "event", ev->id, ev->title, "Signed up: " + sh->title);
            flash = "Thanks for volunteering!";
        } else {
            res.code = 409;
            flash = "That shift is full.";
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, *ev, *shifts, chapter_members, flash));
        return res;
    });
}
