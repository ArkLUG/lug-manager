#include "routes/community/GalleryRoutes.hpp"
#include "utils/text/Plural.hpp"
#include "live/LiveHub.hpp"
#include "utils/LocalTime.hpp"
#include "routes/events/EventAccess.hpp"
#include "services/events/AttendanceService.hpp"
#include "utils/web/ParseId.hpp"
#include <crow/multipart.h>
#include <crow/mustache.h>
#include <regex>

namespace {

std::string add_days(const std::string& ymd, int days) {
    std::tm t{};
    if (std::sscanf(ymd.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return ymd;
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_hour = 12; t.tm_mday += days;
    timegm(&t);
    char b[16];
    std::strftime(b, sizeof(b), "%Y-%m-%d", &t);
    return b;
}

std::string part(crow::multipart::message& msg, const char* name) {
    return msg.get_part_by_name(name).body;
}

// ── Event gallery ───────────────────────────────────────────────────────────
std::string render_gallery(const crow::request& req, LugApp& app, SqliteDatabase& db, const LugEvent& ev,
                           ChapterMemberRepository& cm, const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    bool manage = can_manage_event(req, app, ev, cm);
    crow::mustache::context ctx;
    ctx["event_id"] = ev.id;
    ctx["flash"] = flash;
    auto st = db.prepare("SELECT p.id, p.file, p.caption, p.member_id, COALESCE(m.display_name,'') FROM event_photos p "
                         "LEFT JOIN members m ON m.id = p.member_id WHERE p.event_id=? ORDER BY p.created_at, p.id");
    st.bind(1, ev.id);
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    while (st.step()) {
        arr[i]["id"] = st.col_int(0);
        arr[i]["file"] = st.col_text(1);
        arr[i]["caption"] = st.col_text(2);
        arr[i]["by"] = st.col_text(4);
        arr[i]["event_id"] = ev.id;
        arr[i]["can_delete"] = manage || st.col_int(3) == a.member_id;
        ++i;
    }
    ctx["photos"] = std::move(arr);
    ctx["has_photos"] = i > 0;
    return crow::mustache::load("events/_gallery.html").render(ctx).dump();
}

// ── Challenges ──────────────────────────────────────────────────────────────
struct Challenge { int64_t id = 0; std::string title, description, starts_on, ends_on; bool announced = false; };

std::optional<Challenge> get_challenge(SqliteDatabase& db, int64_t id) {
    auto st = db.prepare("SELECT id, title, description, starts_on, ends_on, announced FROM challenges WHERE id=?");
    st.bind(1, (int64_t)id);
    if (!st.step()) return std::nullopt;
    return Challenge{st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_bool(5)};
}

struct Phase { bool submit = false, vote = false, results = false; std::string vote_until; };
Phase phase_of(const Challenge& c) {
    std::string today = AttendanceService::today_ymd();
    Phase p;
    p.vote_until = add_days(c.ends_on, 7);
    p.submit = today >= c.starts_on && today <= c.ends_on;
    p.vote = today >= c.starts_on && today <= p.vote_until;
    p.results = today > p.vote_until;
    return p;
}

std::string render_challenge(const crow::request& req, LugApp& app, SqliteDatabase& db, const Challenge& c,
                             const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    Phase ph = phase_of(c);
    crow::mustache::context ctx;
    ctx["id"] = c.id; ctx["title"] = c.title; ctx["description"] = c.description;
    ctx["starts_on"] = friendly_date(c.starts_on); ctx["ends_on"] = friendly_date(c.ends_on); ctx["vote_until"] = friendly_date(ph.vote_until);
    ctx["submit_open"] = ph.submit; ctx["vote_open"] = ph.vote; ctx["results"] = ph.results;
    ctx["is_admin"] = a.can("challenges.manage"); ctx["announced"] = c.announced; ctx["flash"] = flash;

    int64_t my_vote = 0;
    {
        auto v = db.prepare("SELECT entry_id FROM challenge_votes WHERE challenge_id=? AND member_id=?");
        v.bind(1, c.id); v.bind(2, a.member_id);
        if (v.step()) my_vote = v.col_int(0);
    }
    auto st = db.prepare(
        "SELECT e.id, e.title, e.file, e.member_id, COALESCE(m.display_name,''), "
        "(SELECT COUNT(*) FROM challenge_votes v WHERE v.entry_id = e.id) AS votes "
        "FROM challenge_entries e LEFT JOIN members m ON m.id = e.member_id WHERE e.challenge_id=? "
        + std::string(ph.results ? "ORDER BY votes DESC, e.created_at" : "ORDER BY e.created_at"));
    st.bind(1, c.id);
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    bool has_mine = false;
    while (st.step()) {
        bool mine = st.col_int(3) == a.member_id;
        has_mine = has_mine || mine;
        arr[i]["id"] = st.col_int(0); arr[i]["challenge_id"] = c.id;
        arr[i]["title"] = st.col_text(1); arr[i]["file"] = st.col_text(2);
        arr[i]["by"] = st.col_text(4);
        // Vote counts stay hidden until voting closes (no bandwagoning).
        arr[i]["votes"] = ph.results ? st.col_int(5) : 0;
        arr[i]["votes_text"] = count_of(ph.results ? st.col_int(5) : 0, "vote", "votes");
        arr[i]["show_votes"] = ph.results;
        arr[i]["winner"] = ph.results && i == 0 && st.col_int(5) > 0;
        arr[i]["can_vote"] = ph.vote && !mine;
        arr[i]["voted"] = my_vote == st.col_int(0);
        arr[i]["can_delete"] = (mine && ph.submit) || a.can("challenges.manage");
        ++i;
    }
    ctx["entries"] = std::move(arr);
    ctx["has_entries"] = i > 0;
    ctx["can_submit"] = ph.submit && !has_mine;
    return crow::mustache::load("challenges/_detail.html").render(ctx).dump();
}

crow::response page(const crow::request& req, LugApp& app, const std::string& body, const std::string& title) {
    return html_page(req, app, body, title, "active_challenges");
}

} // namespace

void register_gallery_routes(LugApp& app, SqliteDatabase& db, std::shared_ptr<PhotoStore> photos,
                             EventService& events, ChapterMemberRepository& chapter_members,
                             AuditService& audit) {

    // GET /uploads/<name> - members only (photos may show members, incl. minors)
    CROW_ROUTE(app, "/uploads/<string>")([&app, photos](const crow::request& req, const std::string& name) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        std::string bytes;
        if (!photos->read(name, bytes)) { res.code = 404; return res; }
        res.add_header("Content-Type", PhotoStore::content_type(name));
        res.add_header("Cache-Control", "private, max-age=86400");
        res.add_header("Content-Security-Policy", "default-src 'none'; sandbox");
        res.add_header("X-Content-Type-Options", "nosniff");
        res.write(bytes);
        return res;
    });

    // ── Event gallery ──
    CROW_ROUTE(app, "/events/<int>/photos")([&app, &db, &events, &chapter_members](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_gallery(req, app, db, *ev, chapter_members));
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/photos").methods("POST"_method)(
        [&app, &db, photos, &events, &chapter_members, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        int64_t me = app.get_context<AuthMiddleware>(req).auth.member_id;
        {
            auto cnt = db.prepare("SELECT COUNT(*) FROM event_photos WHERE event_id=? AND member_id=?");
            cnt.bind(1, ev->id); cnt.bind(2, me);
            if (cnt.step() && cnt.col_int(0) >= 20) {
                res.code = 409;
                res.write(render_gallery(req, app, db, *ev, chapter_members, "You've reached 20 photos for this event."));
                return res;
            }
        }
        crow::multipart::message msg(req);
        std::string err;
        std::string name = photos->save(part(msg, "photo"), err);
        if (name.empty()) {
            res.code = 400;
            res.write(render_gallery(req, app, db, *ev, chapter_members, err));
            return res;
        }
        auto ins = db.prepare("INSERT INTO event_photos (event_id, member_id, file, caption) VALUES (?,?,?,?)");
        ins.bind(1, ev->id); ins.bind(2, me); ins.bind(3, name); ins.bind(4, part(msg, "caption").substr(0, 200));
        ins.step();
        audit.log(req, app, "event.photo_upload", "event", ev->id, ev->title, "Uploaded a photo");
        res.write(render_gallery(req, app, db, *ev, chapter_members, "Photo added."));
        return res;
    });

    CROW_ROUTE(app, "/events/<int>/photos/<int>/delete").methods("POST"_method)(
        [&app, &db, photos, &events, &chapter_members, &audit](const crow::request& req, int id, int pid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        auto st = db.prepare("SELECT member_id, file FROM event_photos WHERE id=? AND event_id=?");
        st.bind(1, (int64_t)pid); st.bind(2, ev->id);
        if (!st.step()) { res.code = 404; return res; }
        int64_t owner = st.col_int(0);
        std::string file = st.col_text(1);
        if (owner != app.get_context<AuthMiddleware>(req).auth.member_id && !can_manage_event(req, app, *ev, chapter_members)) {
            res.code = 403;
            return res;
        }
        { auto del = db.prepare("DELETE FROM event_photos WHERE id=?"); del.bind(1, (int64_t)pid); del.step(); }
        photos->remove(file);
        audit.log(req, app, "event.photo_delete", "event", ev->id, ev->title, "Deleted a photo");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_gallery(req, app, db, *ev, chapter_members));
        return res;
    });

    // ── Challenges ──
    CROW_ROUTE(app, "/challenges")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        crow::mustache::context ctx;
        auto st = db.prepare("SELECT c.id, c.title, c.starts_on, c.ends_on, "
                             "(SELECT COUNT(*) FROM challenge_entries e WHERE e.challenge_id=c.id) "
                             "FROM challenges c ORDER BY c.starts_on DESC");
        crow::json::wvalue arr = crow::json::wvalue::list();
        int i = 0;
        while (st.step()) {
            Challenge c{st.col_int(0), st.col_text(1), "", st.col_text(2), st.col_text(3)};
            Phase ph = phase_of(c);
            arr[i]["id"] = c.id; arr[i]["title"] = c.title; arr[i]["starts_on"] = friendly_date(c.starts_on); arr[i]["ends_on"] = friendly_date(c.ends_on);
            arr[i]["entries"] = st.col_int(4);
            arr[i]["status"] = ph.submit ? "Open for entries" : ph.vote ? "Voting" : ph.results ? "Finished" : "Upcoming";
            ++i;
        }
        ctx["challenges"] = std::move(arr);
        ctx["has_challenges"] = i > 0;
        ctx["is_admin"] = app.get_context<AuthMiddleware>(req).auth.can("challenges.manage");
        ctx["today"] = AttendanceService::today_ymd();
        return page(req, app, crow::mustache::load("challenges/_list.html").render(ctx).dump(), "Build Challenges");
    });

    CROW_ROUTE(app, "/challenges").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:challenges.manage")) return res;
        auto p = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = p.get(k); return v ? std::string(v) : ""; };
        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
        std::string title = gp("title").substr(0, 150), s = gp("starts_on"), e = gp("ends_on");
        if (title.empty() || !std::regex_match(s, ymd) || !std::regex_match(e, ymd) || e < s) {
            res.code = 400;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<p class=\"text-sm text-red-600\">Give it a title and a start/end date (end after start).</p>");
            return res;
        }
        auto ins = db.prepare("INSERT INTO challenges (title, description, starts_on, ends_on, created_by) VALUES (?,?,?,?,?) RETURNING id");
        ins.bind(1, title); ins.bind(2, gp("description").substr(0, 2000)); ins.bind(3, s); ins.bind(4, e);
        ins.bind(5, app.get_context<AuthMiddleware>(req).auth.member_id);
        ins.step();
        int64_t id = ins.col_int(0);
        audit.log(req, app, "challenge.create", "challenge", id, title, s + " to " + e);
        res.add_header("HX-Redirect", "/challenges/" + std::to_string(id));
        return res;
    });

    CROW_ROUTE(app, "/challenges/<int>")([&app, &db](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto c = get_challenge(db, id);
        if (!c) { res.code = 404; return res; }
        return page(req, app, render_challenge(req, app, db, *c), c->title);
    });

    CROW_ROUTE(app, "/challenges/<int>/entries").methods("POST"_method)(
        [&app, &db, photos, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto c = get_challenge(db, id);
        if (!c) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!phase_of(*c).submit) {
            res.code = 409;
            res.write(render_challenge(req, app, db, *c, "Entries are closed."));
            return res;
        }
        crow::multipart::message msg(req);
        std::string title = part(msg, "title").substr(0, 150);
        if (title.empty()) {
            res.code = 400;
            res.write(render_challenge(req, app, db, *c, "Give your build a name."));
            return res;
        }
        std::string err;
        std::string name = photos->save(part(msg, "photo"), err);
        if (name.empty()) {
            res.code = 400;
            res.write(render_challenge(req, app, db, *c, err));
            return res;
        }
        auto ins = db.prepare("INSERT OR IGNORE INTO challenge_entries (challenge_id, member_id, title, file) VALUES (?,?,?,?) RETURNING id");
        ins.bind(1, c->id); ins.bind(2, app.get_context<AuthMiddleware>(req).auth.member_id); ins.bind(3, title); ins.bind(4, name);
        if (!ins.step()) {
            photos->remove(name);
            res.code = 409;
            res.write(render_challenge(req, app, db, *c, "You already have an entry in this challenge."));
            return res;
        }
        audit.log(req, app, "challenge.entry", "challenge", c->id, c->title, "Entered: " + title);
        res.write(render_challenge(req, app, db, *c, "Entry submitted - good luck!"));
        return res;
    });

    CROW_ROUTE(app, "/challenges/<int>/entries/<int>/vote").methods("POST"_method)(
        [&app, &db](const crow::request& req, int id, int eid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto c = get_challenge(db, id);
        if (!c) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        int64_t me = app.get_context<AuthMiddleware>(req).auth.member_id;
        int64_t owner = 0;
        {
            auto st = db.prepare("SELECT member_id FROM challenge_entries WHERE id=? AND challenge_id=?");
            st.bind(1, (int64_t)eid); st.bind(2, c->id);
            if (!st.step()) { res.code = 404; return res; }
            owner = st.col_int(0);
        }
        if (!phase_of(*c).vote || owner == me) {
            res.code = 409;
            res.write(render_challenge(req, app, db, *c, owner == me ? "You can't vote for your own build." : "Voting is closed."));
            return res;
        }
        // One vote per member per challenge: voting again moves it; voting the same entry removes it.
        int64_t current = 0;
        {
            auto v = db.prepare("SELECT entry_id FROM challenge_votes WHERE challenge_id=? AND member_id=?");
            v.bind(1, c->id); v.bind(2, me);
            if (v.step()) current = v.col_int(0);
        }
        { auto del = db.prepare("DELETE FROM challenge_votes WHERE challenge_id=? AND member_id=?"); del.bind(1, c->id); del.bind(2, me); del.step(); }
        if (current != eid) {
            auto ins = db.prepare("INSERT INTO challenge_votes (challenge_id, entry_id, member_id) VALUES (?,?,?)");
            ins.bind(1, c->id); ins.bind(2, (int64_t)eid); ins.bind(3, me);
            ins.step();
        }
        live::changed_by(req, "challenge", c->id);
        res.write(render_challenge(req, app, db, *c));
        return res;
    });

    CROW_ROUTE(app, "/challenges/<int>/entries/<int>/delete").methods("POST"_method)(
        [&app, &db, photos, &audit](const crow::request& req, int id, int eid) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto c = get_challenge(db, id);
        if (!c) { res.code = 404; return res; }
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        auto st = db.prepare("SELECT member_id, file FROM challenge_entries WHERE id=? AND challenge_id=?");
        st.bind(1, (int64_t)eid); st.bind(2, c->id);
        if (!st.step()) { res.code = 404; return res; }
        bool own = st.col_int(0) == a.member_id;
        std::string file = st.col_text(1);
        if (!a.can("challenges.manage") && !(own && phase_of(*c).submit)) { res.code = 403; return res; }
        { auto del = db.prepare("DELETE FROM challenge_entries WHERE id=?"); del.bind(1, (int64_t)eid); del.step(); }
        photos->remove(file);
        audit.log(req, app, "challenge.entry_delete", "challenge", c->id, c->title, "Removed entry #" + std::to_string(eid));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_challenge(req, app, db, *c));
        return res;
    });

    // POST /challenges/<id>/announce - admin: post the winner to the LUG channel
    CROW_ROUTE(app, "/challenges/<int>/announce").methods("POST"_method)(
        [&app, &db, &events, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:challenges.manage")) return res;
        auto c = get_challenge(db, id);
        if (!c) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!phase_of(*c).results) {
            res.code = 409;
            res.write(render_challenge(req, app, db, *c, "Voting hasn't finished yet."));
            return res;
        }
        auto st = db.prepare("SELECT e.title, COALESCE(m.display_name,''), "
                             "(SELECT COUNT(*) FROM challenge_votes v WHERE v.entry_id=e.id) AS n "
                             "FROM challenge_entries e LEFT JOIN members m ON m.id = e.member_id "
                             "WHERE e.challenge_id=? ORDER BY n DESC, e.created_at LIMIT 1");
        st.bind(1, c->id);
        std::string flash = "No entries to announce.";
        if (st.step() && st.col_int(2) > 0) {
            chat::Values v{{"challenge", c->title}, {"winner", st.col_text(1)}, {"entry", st.col_text(0)},
                           {"votes", std::to_string(st.col_int(2))}};
            bool ok = events.chat() && events.chat()->post_to(chat::Place::Announcements, 0, "challenge.winner", v,
                                                               "challenge", c->id, "challenges") > 0;
            if (ok) { auto u = db.prepare("UPDATE challenges SET announced=1 WHERE id=?"); u.bind(1, c->id); u.step(); }
            audit.log(req, app, "challenge.announce", "challenge", c->id, c->title, ok ? "Posted to chat" : "Chat post failed");
            flash = ok ? "Winner announced." : "Couldn't post it (check the chat settings and the announcements channel).";
            c->announced = ok;
        }
        res.write(render_challenge(req, app, db, *c, flash));
        return res;
    });
}
