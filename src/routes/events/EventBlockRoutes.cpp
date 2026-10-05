#include "routes/events/EventBlockRoutes.hpp"
#include "repositories/events/EventBlocks.hpp"
#include "routes/events/EventAccess.hpp"
#include "utils/LocalTime.hpp"
#include "utils/web/FormBody.hpp"
#include <crow/mustache.h>
#include <regex>

namespace {

bool is_ymd(const std::string& s) { static const std::regex re(R"(\d{4}-\d{2}-\d{2})"); return std::regex_match(s, re); }
bool is_hm(const std::string& s) { static const std::regex re(R"(([01]\d|2[0-3]):[0-5]\d)"); return std::regex_match(s, re); }

std::string hm12(const std::string& hm) {   // "16:30" -> "4:30 PM"
    int h = std::atoi(hm.substr(0, 2).c_str()), m = std::atoi(hm.substr(3, 2).c_str());
    char b[16];
    std::snprintf(b, sizeof(b), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, m, h >= 12 ? "PM" : "AM");
    return b;
}

// The days of the event's public dates, YYYY-MM-DD each.
std::vector<std::string> public_days(const LugEvent& ev) {
    std::vector<std::string> out;
    std::string d = ev.start_time.substr(0, 10), last = (ev.end_time.size() >= 10 ? ev.end_time : ev.start_time).substr(0, 10);
    for (int guard = 0; d <= last && guard < 60; ++guard) {
        out.push_back(d);
        std::tm t{};
        std::sscanf(d.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
        t.tm_year -= 1900; t.tm_mon -= 1; t.tm_mday += 1; t.tm_hour = 12;
        timegm(&t);
        char b[16];
        std::snprintf(b, sizeof(b), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        d = b;
    }
    return out;
}

std::string render(const crow::request& req, LugApp& app, SqliteDatabase& db, const LugEvent& ev,
                   ChapterMemberRepository& cm, const std::string& flash = "", bool error = false) {
    const bool manage = can_manage_event(req, app, ev, cm);
    auto blocks = event_blocks::list(db, ev.id);
    crow::mustache::context ctx;
    ctx["event_id"] = ev.id;
    ctx["can_manage"] = manage;
    if (!flash.empty()) { ctx["flash"] = flash; ctx["flash_error"] = error; }
    ctx["default_day"] = ev.start_time.substr(0, 10);
    crow::json::wvalue days = crow::json::wvalue::list();
    int di = -1, bi = 0;
    std::string current;
    for (const auto& b : blocks) {
        if (b.day != current) {
            current = b.day; ++di; bi = 0;
            days[di]["day"] = friendly_date(b.day);
            days[di]["blocks"] = crow::json::wvalue::list();
        }
        auto& j = days[di]["blocks"][bi++];
        j["id"] = b.id; j["event_id"] = ev.id;
        j["kind"] = b.kind; j["kind_label"] = event_blocks::kind_label(b.kind);
        j["public"] = b.is_public(); j["setup"] = b.kind == "setup"; j["teardown"] = b.kind == "teardown";
        j["time"] = hm12(b.starts) + " – " + hm12(b.ends);
        j["label"] = b.label;
        j["can_manage"] = manage;
    }
    ctx["days"] = std::move(days);
    ctx["has_blocks"] = !blocks.empty();
    return crow::mustache::load("events/_blocks.html").render(ctx).dump();
}

} // namespace

void register_event_block_routes(LugApp& app, SqliteDatabase& db, EventService& events,
                                 ChapterMemberRepository& chapter_members, AuditService& audit) {
    // Re-sync days, Discord and calendars after a change (no "updated" post)
    auto resync = [&events](const LugEvent& ev) { events.update(ev.id, ev, false, /*notify=*/false); };

    CROW_ROUTE(app, "/events/<int>/blocks")([&app, &db, &events, &chapter_members](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, db, *ev, chapter_members));
        return res;
    });

    // POST /events/<id>/blocks - add one block
    CROW_ROUTE(app, "/events/<int>/blocks").methods("POST"_method)(
        [&app, &db, &events, &chapter_members, &audit, resync](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        FormBody f(req);
        const std::string day = f.get("day", 10), kind = f.get("kind", 10), from = f.get("from", 5), to = f.get("to", 5);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        // Within a few days of the show: setup up to 14 days before, teardown up to 7 after
        const std::string first = ev->start_time.substr(0, 10), last = ev->end_time.substr(0, 10);
        if (!is_ymd(day) || !event_blocks::valid_kind(kind) || !is_hm(from) || !is_hm(to) || to <= from) {
            res.code = 400;
            res.write(render(req, app, db, *ev, chapter_members, "Pick a day, what it's for, and a start before the end.", true));
            return res;
        }
        if (local_iso_to_epoch(day + "T12:00") < local_iso_to_epoch(first + "T12:00") - 14 * 86400 ||
            local_iso_to_epoch(day + "T12:00") > local_iso_to_epoch(last + "T12:00") + 7 * 86400 ||
            (kind == "public" && (day < first || day > last))) {
            res.code = 400;
            res.write(render(req, app, db, *ev, chapter_members,
                             kind == "public" ? "Public hours go on the show's own dates (change the dates to add a day)."
                                              : "Setup and teardown go within two weeks before or a week after the show.", true));
            return res;
        }
        auto ins = db.prepare("INSERT INTO event_blocks (event_id, day_date, kind, starts_at, ends_at, label) VALUES (?,?,?,?,?,?)");
        ins.bind(1, static_cast<int64_t>(ev->id)); ins.bind(2, day); ins.bind(3, kind); ins.bind(4, from); ins.bind(5, to);
        ins.bind(6, f.get("label", 80));
        ins.step();
        audit.log(req, app, "event.block_add", "event", ev->id, ev->title,
                  std::string(event_blocks::kind_label(kind)) + " " + day + " " + from + "-" + to);
        resync(*ev);
        res.write(render(req, app, db, *ev, chapter_members));
        return res;
    });

    // POST /events/<id>/blocks/fill - the same public hours on every show day
    // (replaces the public blocks; setup and teardown stay)
    CROW_ROUTE(app, "/events/<int>/blocks/fill").methods("POST"_method)(
        [&app, &db, &events, &chapter_members, &audit, resync](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        FormBody f(req);
        const std::string from = f.get("from", 5), to = f.get("to", 5);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!is_hm(from) || !is_hm(to) || to <= from) {
            res.code = 400;
            res.write(render(req, app, db, *ev, chapter_members, "Give the opening and closing times.", true));
            return res;
        }
        {
            Transaction tx(db);
            auto del = db.prepare("DELETE FROM event_blocks WHERE event_id=? AND kind='public'");
            del.bind(1, static_cast<int64_t>(ev->id));
            del.step();
            for (const auto& day : public_days(*ev)) {
                auto ins = db.prepare("INSERT INTO event_blocks (event_id, day_date, kind, starts_at, ends_at) VALUES (?,?, 'public', ?,?)");
                ins.bind(1, static_cast<int64_t>(ev->id)); ins.bind(2, day); ins.bind(3, from); ins.bind(4, to);
                ins.step();
            }
            tx.commit();
        }
        audit.log(req, app, "event.block_fill", "event", ev->id, ev->title, "Public hours every day " + from + "-" + to);
        resync(*ev);
        res.write(render(req, app, db, *ev, chapter_members));
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/blocks/<int>/delete").methods("POST"_method)(
        [&app, &db, &events, &chapter_members, &audit, resync](const crow::request& req, int id, int bid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto del = db.prepare("DELETE FROM event_blocks WHERE id=? AND event_id=? RETURNING day_date, kind, starts_at, ends_at");
        del.bind(1, static_cast<int64_t>(bid)); del.bind(2, static_cast<int64_t>(ev->id));
        if (del.step()) {
            audit.log(req, app, "event.block_remove", "event", ev->id, ev->title,
                      std::string(event_blocks::kind_label(del.col_text(1))) + " " + del.col_text(0) + " " + del.col_text(2) + "-" + del.col_text(3));
            del.reset();
            resync(*ev);
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, db, *ev, chapter_members));
        return res;
    });
}
