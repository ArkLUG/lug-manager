#include "routes/FanCoLabRoutes.hpp"
#include "services/AttendanceService.hpp"
#include "services/FanCoLab.hpp"
#include "services/Features.hpp"
#include "utils/AssetVersion.hpp"
#include "utils/Csv.hpp"
#include "utils/LocalTime.hpp"
#include "utils/MarkdownRenderer.hpp"
#include "utils/HtmlText.hpp"
#include "utils/Utf8.hpp"
#include <crow/mustache.h>
#include <algorithm>
#include <cstring>
#include <regex>
#include <set>

namespace {

constexpr size_t kAboutMaxChars = 20000;

struct Form {
    crow::query_string q;
    explicit Form(const crow::request& req) : q("?" + req.body) {}
    std::string get(const char* k, size_t max = 200) const {
        const char* v = q.get(k);
        return v ? std::string(v).substr(0, max) : "";
    }
    int64_t num(const char* k, int64_t def = 0) const {
        try { return std::stoll(get(k, 20)); } catch (...) { return def; }
    }
};

int64_t scalar(SqliteDatabase& db, const std::string& sql, const std::vector<std::string>& args) {
    auto st = db.prepare(sql);
    for (size_t i = 0; i < args.size(); ++i) st.bind(static_cast<int>(i + 1), args[i]);
    return st.step() ? st.col_int(0) : 0;
}

int this_year() { return local_tm(std::time(nullptr)).tm_year + 1900; }

int year_param(const char* v) {
    int y = this_year();
    if (v) { try { y = std::stoi(v); } catch (...) {} }
    return std::clamp(y, 1990, 2200);
}

bool is_ymd(const std::string& s) {
    static const std::regex re(R"(^\d{4}-\d{2}-\d{2}$)");
    return std::regex_match(s, re);
}

// ── The year's activity summary ──

struct PublicEvent {
    int64_t id = 0;
    std::string date, end_date, title, location;
    int64_t days = 1, kids = 0, teens = 0, adults = 0, members = 0, mocs = 0, interest = 0;
    double sqft = 0;
    int64_t visitors() const { return kids + teens + adults; }
};

struct Summary {
    int year = 0;
    int64_t members_total = 0, members_new = 0, active = 0;
    int64_t meetings = 0, checkins = 0, lug_events = 0;
    int64_t show_days = 0, kids = 0, teens = 0, adults = 0, member_attendances = 0, mocs = 0, interest = 0;
    double sqft = 0;
    std::vector<PublicEvent> shows;
    std::vector<AmbassadorTerm> ambassadors;
};

Summary summarize(SqliteDatabase& db, int year) {
    Summary s;
    s.year = year;
    std::string lo = std::to_string(year) + "-01-01", hi = std::to_string(year + 1) + "-01-01";
    const std::vector<std::string> range{lo, hi};
    s.members_total = scalar(db, "SELECT COUNT(*) FROM members WHERE created_at < ?", {hi});
    s.members_new = scalar(db, "SELECT COUNT(*) FROM members WHERE created_at >= ? AND created_at < ?", range);
    s.active = scalar(db,
        "SELECT COUNT(*) FROM (SELECT a.member_id FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
        " WHERE a.entity_type='meeting' AND mt.start_time >= ?1 AND mt.start_time < ?2 "
        " UNION SELECT eda.member_id FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
        " WHERE ed.day_date >= ?1 AND ed.day_date < ?2)", range);
    s.meetings = scalar(db,
        "SELECT COUNT(*) FROM meetings WHERE status <> 'cancelled' AND start_time >= ? AND start_time < ?", range);
    s.checkins = scalar(db,
        "SELECT COUNT(*) FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
        "WHERE a.entity_type='meeting' AND mt.status <> 'cancelled' AND mt.start_time >= ? AND mt.start_time < ?", range);
    s.lug_events = scalar(db,
        "SELECT COUNT(*) FROM lug_events WHERE is_private=1 AND status <> 'cancelled' AND start_time >= ? AND start_time < ?", range);

    auto st = db.prepare(
        "SELECT e.id, substr(e.start_time,1,10), substr(COALESCE(NULLIF(e.end_time,''), e.start_time),1,10), e.title, e.location, "
        " MAX(1, (SELECT COUNT(*) FROM event_days d WHERE d.event_id = e.id)), "
        " e.public_kids, e.public_teens, e.public_adults, "
        " (SELECT COUNT(DISTINCT eda.member_id) FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id WHERE ed.event_id = e.id), "
        " (SELECT COUNT(*) FROM event_display_requests r WHERE r.event_id = e.id AND r.status='approved'), "
        " (SELECT COALESCE(SUM(r.width_in * r.depth_in),0) FROM event_display_requests r WHERE r.event_id = e.id AND r.status='approved'), "
        " e.public_interest "
        "FROM lug_events e WHERE e.is_private=0 AND e.status <> 'cancelled' AND e.start_time >= ? AND e.start_time < ? "
        "ORDER BY e.start_time, e.id");
    st.bind(1, lo); st.bind(2, hi);
    while (st.step()) {
        PublicEvent e;
        e.id = st.col_int(0); e.date = st.col_text(1); e.end_date = st.col_text(2);
        e.title = st.col_text(3); e.location = st.col_text(4); e.days = st.col_int(5);
        e.kids = st.col_int(6); e.teens = st.col_int(7); e.adults = st.col_int(8);
        e.members = st.col_int(9); e.mocs = st.col_int(10); e.sqft = st.col_int(11) / 144.0;
        e.interest = st.col_int(12);
        s.show_days += e.days; s.kids += e.kids; s.teens += e.teens; s.adults += e.adults;
        s.member_attendances += e.members; s.mocs += e.mocs; s.sqft += e.sqft; s.interest += e.interest;
        s.shows.push_back(std::move(e));
    }
    s.ambassadors = ambassador_terms(db, year);
    return s;
}

std::string one_decimal(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

std::string avg(int64_t total, int64_t n) { return one_decimal(n ? static_cast<double>(total) / n : 0.0); }

// Rows shared by the printed summary and its CSV.
std::vector<std::pair<std::string, std::string>> summary_rows(const Summary& s) {
    auto n = [](int64_t v) { return std::to_string(v); };
    return {
        {"Members at year end", n(s.members_total)},
        {"New members", n(s.members_new)},
        {"Members who attended something", n(s.active)},
        {"Meetings held", n(s.meetings)},
        {"Meeting check-ins", n(s.checkins)},
        {"Average attendance per meeting", avg(s.checkins, s.meetings)},
        {"Public shows and events", n(static_cast<int64_t>(s.shows.size()))},
        {"Public show days", n(s.show_days)},
        {"Public visitors", n(s.kids + s.teens + s.adults)},
        {"Public visitors - kids", n(s.kids)},
        {"Public visitors - teens", n(s.teens)},
        {"Public visitors - adults", n(s.adults)},
        {"Member attendances at public events", n(s.member_attendances)},
        {"Displays (MOCs) shown", n(s.mocs)},
        {"Display area (sq ft)", one_decimal(s.sqft)},
        {"Members-only events", n(s.lug_events)},
    };
}

std::string ambassador_line(const Summary& s) {
    std::string out;
    for (auto it = s.ambassadors.rbegin(); it != s.ambassadors.rend(); ++it)
        out += (out.empty() ? "" : "; ") + it->name + " (from " + it->started_on +
               (it->ended_on.empty() ? "" : " to " + it->ended_on) + ")";
    return out;
}

// ── /fancolab page ──

std::string render_fancolab(SqliteDatabase& db, SettingsRepository& settings, int year,
                            const std::string& flash = "", bool error = false) {
    crow::mustache::context ctx;
    auto fc = fan_colab_info(db);
    ctx["year"] = year;
    ctx["prev_year"] = year - 1;
    ctx["next_year"] = year + 1;
    ctx["recognized"] = fc.recognized;
    ctx["has_ambassador"] = !fc.ambassador.empty();
    ctx["ambassador"] = fc.ambassador;
    ctx["lug_name"] = fc.lug_name.empty() ? std::string("This group") : fc.lug_name;
    {
        crow::json::wvalue people = crow::json::wvalue::list();
        auto st = db.prepare("SELECT id, display_name FROM members ORDER BY display_name COLLATE NOCASE");
        int i = 0;
        while (st.step()) {
            people[i]["id"] = st.col_int(0); people[i]["name"] = st.col_text(1);
            people[i]["selected"] = st.col_int(0) == fc.ambassador_id;
            ++i;
        }
        ctx["people"] = std::move(people);
    }
    {
        crow::json::wvalue arr = crow::json::wvalue::list();
        int i = 0;
        for (const auto& t : ambassador_terms(db)) {
            arr[i]["id"] = t.id; arr[i]["name"] = t.name;
            arr[i]["started_on"] = t.started_on; arr[i]["ended_on"] = t.ended_on;
            arr[i]["serving"] = t.ended_on.empty();
            ++i;
        }
        ctx["terms"] = std::move(arr);
        ctx["has_terms"] = i > 0;
    }
    {
        auto st = db.prepare(
            "SELECT t.id, t.title, d.year IS NOT NULL, COALESCE(m.display_name,''), COALESCE(substr(d.done_at,1,10),'') "
            "FROM fan_colab_tasks t LEFT JOIN fan_colab_task_done d ON d.task_id = t.id AND d.year = ? "
            "LEFT JOIN members m ON m.id = d.done_by ORDER BY t.sort_order, t.id");
        st.bind(1, static_cast<int64_t>(year));
        crow::json::wvalue arr = crow::json::wvalue::list();
        int i = 0, done = 0;
        while (st.step()) {
            arr[i]["id"] = st.col_int(0); arr[i]["title"] = st.col_text(1);
            bool d = st.col_int(2) != 0;
            arr[i]["done"] = d; arr[i]["done_by"] = st.col_text(3); arr[i]["done_on"] = st.col_text(4);
            arr[i]["year"] = year;
            done += d; ++i;
        }
        ctx["tasks"] = std::move(arr);
        ctx["has_tasks"] = i > 0;
        ctx["tasks_done"] = done;
        ctx["tasks_total"] = i;
        ctx["all_done"] = i > 0 && done == i;
    }
    ctx["about_on"] = Features::on("about_page");
    ctx["has_about_text"] = !settings.get("about_markdown", "").empty();
    ctx["today"] = AttendanceService::today_ymd();
    if (!flash.empty()) { ctx["flash"] = flash; ctx["flash_error"] = error; }
    return crow::mustache::load("fancolab/_page.html").render(ctx).dump();
}

// Full page or htmx fragment.
crow::response page(const crow::request& req, LugApp& app, const std::string& html,
                    const std::string& title, const std::string& active, int code = 200) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.write(req.get_header_value("HX-Request") == "true" ? html : render_in_layout(req, app, html, title, active));
    return res;
}

// ── About page ──

// Upload names referenced by the About text ("/about/photos/<name>").
std::set<std::string> referenced_photos(const std::string& markdown) {
    std::set<std::string> out;
    static const std::regex re(R"(/about/photos/([A-Za-z0-9._-]+))");
    for (std::sregex_iterator it(markdown.begin(), markdown.end(), re), end; it != end; ++it) out.insert((*it)[1].str());
    return out;
}

std::string render_about_editor(SettingsRepository& settings, const std::string& flash = "", bool error = false) {
    crow::mustache::context ctx;
    std::string md = settings.get("about_markdown", "");
    ctx["enabled"] = Features::on("about_page");
    ctx["about_title"] = settings.get("about_title", "");
    ctx["lug_name"] = settings.get("lug_name", "");
    ctx["markdown"] = md;
    ctx["html"] = render_markdown(md);
    ctx["show_numbers"] = settings.get("about_show_numbers", "1") == "1";
    ctx["max_chars"] = static_cast<int>(kAboutMaxChars);
    if (!flash.empty()) { ctx["flash"] = flash; ctx["flash_error"] = error; }
    return crow::mustache::load("settings/_about.html").render(ctx).dump();
}

} // namespace

void register_fan_colab_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings,
                               std::shared_ptr<PhotoStore> photos, AuditService& audit) {

    // GET /fancolab?year=
    CROW_ROUTE(app, "/fancolab")([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return page(req, app, render_fancolab(db, settings, year_param(req.url_params.get("year"))),
                    "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/recognition - Recognized LEGO Fan Community + Community Ambassador
    CROW_ROUTE(app, "/fancolab/recognition").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        int year = year_param(f.get("year", 6).c_str());
        bool recognized = f.get("fan_colab_recognized", 2) == "1";
        int64_t amb_id = f.num("community_ambassador_id");
        if (amb_id > 0) {
            auto st = db.prepare("SELECT 1 FROM members WHERE id=?");
            st.bind(1, amb_id);
            if (!st.step())
                return page(req, app, render_fancolab(db, settings, year, "That member doesn't exist.", true),
                            "LEGO Fan CoLab", "active_fancolab", 400);
        }
        record_ambassador_change(db, std::max<int64_t>(amb_id, 0), AttendanceService::today_ymd());
        settings.set("fan_colab_recognized", recognized ? "1" : "0");
        settings.set("community_ambassador_id", amb_id > 0 ? std::to_string(amb_id) : "");
        audit.log(req, app, "settings.update", "settings", 0, "LEGO Fan CoLab",
                  std::string(recognized ? "Recognized" : "Not recognized") + (amb_id > 0 ? ", ambassador #" + std::to_string(amb_id) : ""));
        return page(req, app, render_fancolab(db, settings, year, "Saved."), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/terms - record a past Community Ambassador
    CROW_ROUTE(app, "/fancolab/terms").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        int year = year_param(f.get("year", 6).c_str());
        int64_t member = f.num("member_id");
        std::string from = f.get("started_on", 10), to = f.get("ended_on", 10), name;
        {
            auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
            st.bind(1, member);
            if (st.step()) name = st.col_text(0);
        }
        auto fail = [&](const std::string& msg) {
            return page(req, app, render_fancolab(db, settings, year, msg, true), "LEGO Fan CoLab", "active_fancolab", 400);
        };
        if (name.empty()) return fail("Choose a member.");
        if (!is_ymd(from) || !is_ymd(to)) return fail("Give both the start and end dates.");
        if (to < from) return fail("The end date is before the start date.");
        if (to > AttendanceService::today_ymd()) return fail("Past ambassadors only: the end date is in the future. Set the current ambassador above.");
        {
            auto st = db.prepare("INSERT INTO community_ambassador_terms (member_id, member_name, started_on, ended_on) VALUES (?,?,?,?)");
            st.bind(1, member); st.bind(2, name); st.bind(3, from); st.bind(4, to);
            st.step();
        }
        audit.log(req, app, "fancolab.ambassador_term", "member", member, name, "Ambassador " + from + " to " + to);
        return page(req, app, render_fancolab(db, settings, year, "Added " + name + "."), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/terms/<id> - correct a term's dates
    CROW_ROUTE(app, "/fancolab/terms/<int>").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        int year = year_param(f.get("year", 6).c_str());
        std::string from = f.get("started_on", 10), to = f.get("ended_on", 10), name, cur_end;
        bool serving = false;
        {
            auto st = db.prepare("SELECT member_name, ended_on IS NULL FROM community_ambassador_terms WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) { res.code = 404; return res; }
            name = st.col_text(0); serving = st.col_int(1) != 0;
        }
        auto fail = [&](const std::string& msg) {
            return page(req, app, render_fancolab(db, settings, year, msg, true), "LEGO Fan CoLab", "active_fancolab", 400);
        };
        if (!is_ymd(from)) return fail("Give the start date.");
        if (serving && !to.empty()) return fail("To end the current ambassador's term, choose someone else (or nobody) above.");
        if (!serving && !is_ymd(to)) return fail("Give the end date.");
        if (!serving && to < from) return fail("The end date is before the start date.");
        if (from > AttendanceService::today_ymd()) return fail("The start date is in the future.");
        {
            auto st = db.prepare("UPDATE community_ambassador_terms SET started_on=?, ended_on=? WHERE id=?");
            st.bind(1, from);
            if (serving) st.bind_null(2); else st.bind(2, to);
            st.bind(3, static_cast<int64_t>(id));
            st.step();
        }
        audit.log(req, app, "fancolab.ambassador_term", "settings", id, name, "Dates " + from + " to " + (serving ? "now" : to));
        return page(req, app, render_fancolab(db, settings, year, "Saved."), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/terms/<id>/delete - past terms only
    CROW_ROUTE(app, "/fancolab/terms/<int>/delete").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int year = year_param(Form(req).get("year", 6).c_str());
        std::string name;
        bool serving = false;
        {
            auto st = db.prepare("SELECT member_name, ended_on IS NULL FROM community_ambassador_terms WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) { res.code = 404; return res; }
            name = st.col_text(0); serving = st.col_int(1) != 0;
        }
        if (serving)
            return page(req, app, render_fancolab(db, settings, year, "That's the current ambassador: choose someone else (or nobody) above instead.", true),
                        "LEGO Fan CoLab", "active_fancolab", 400);
        {
            auto st = db.prepare("DELETE FROM community_ambassador_terms WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            st.step();
        }
        audit.log(req, app, "fancolab.ambassador_term", "settings", id, name, "Removed from ambassador history");
        return page(req, app, render_fancolab(db, settings, year, "Removed."), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/tasks - add a yearly to-do
    CROW_ROUTE(app, "/fancolab/tasks").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        int year = year_param(f.get("year", 6).c_str());
        std::string title = f.get("title", 200);
        title.erase(0, title.find_first_not_of(" \t\r\n"));
        title.erase(title.find_last_not_of(" \t\r\n") + 1);
        if (title.empty())
            return page(req, app, render_fancolab(db, settings, year, "Write what needs doing.", true), "LEGO Fan CoLab", "active_fancolab", 400);
        {
            auto st = db.prepare("INSERT INTO fan_colab_tasks (title, sort_order) "
                                 "VALUES (?, (SELECT COALESCE(MAX(sort_order),0)+1 FROM fan_colab_tasks))");
            st.bind(1, title);
            st.step();
        }
        audit.log(req, app, "fancolab.task", "settings", 0, title, "Added to the yearly to-do list");
        return page(req, app, render_fancolab(db, settings, year), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/tasks/<id>/toggle - tick / untick for a year
    CROW_ROUTE(app, "/fancolab/tasks/<int>/toggle").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int year = year_param(Form(req).get("year", 6).c_str());
        std::string title;
        {
            auto st = db.prepare("SELECT title FROM fan_colab_tasks WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) { res.code = 404; return res; }
            title = st.col_text(0);
        }
        bool was_done;
        {
            auto st = db.prepare("SELECT 1 FROM fan_colab_task_done WHERE task_id=? AND year=?");
            st.bind(1, static_cast<int64_t>(id)); st.bind(2, static_cast<int64_t>(year));
            was_done = st.step();
        }
        if (was_done) {
            auto st = db.prepare("DELETE FROM fan_colab_task_done WHERE task_id=? AND year=?");
            st.bind(1, static_cast<int64_t>(id)); st.bind(2, static_cast<int64_t>(year));
            st.step();
        } else {
            auto st = db.prepare("INSERT INTO fan_colab_task_done (task_id, year, done_by) VALUES (?,?,?)");
            st.bind(1, static_cast<int64_t>(id)); st.bind(2, static_cast<int64_t>(year));
            st.bind(3, app.get_context<AuthMiddleware>(req).auth.member_id);
            st.step();
        }
        audit.log(req, app, "fancolab.task", "settings", id, title,
                  std::string(was_done ? "Not done" : "Done") + " for " + std::to_string(year));
        return page(req, app, render_fancolab(db, settings, year), "LEGO Fan CoLab", "active_fancolab");
    });

    // POST /fancolab/tasks/<id>/delete
    CROW_ROUTE(app, "/fancolab/tasks/<int>/delete").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int year = year_param(Form(req).get("year", 6).c_str());
        std::string title;
        {
            auto st = db.prepare("SELECT title FROM fan_colab_tasks WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) { res.code = 404; return res; }
            title = st.col_text(0);
        }
        {
            auto st = db.prepare("DELETE FROM fan_colab_tasks WHERE id=?");
            st.bind(1, static_cast<int64_t>(id));
            st.step();
        }
        audit.log(req, app, "fancolab.task", "settings", id, title, "Removed from the yearly to-do list");
        return page(req, app, render_fancolab(db, settings, year), "LEGO Fan CoLab", "active_fancolab");
    });

    // GET /fancolab/summary?year= - printable one-page summary
    CROW_ROUTE(app, "/fancolab/summary")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto s = summarize(db, year_param(req.url_params.get("year")));
        auto fc = fan_colab_info(db);
        crow::mustache::context ctx;
        ctx["year"] = s.year;
        ctx["lug_name"] = fc.lug_name.empty() ? std::string("Our group") : fc.lug_name;
        ctx["recognized"] = fc.recognized;
        std::string amb = ambassador_line(s);
        if (!amb.empty()) ctx["ambassadors"] = amb;
        ctx["generated"] = AttendanceService::today_ymd();
        crow::json::wvalue rows = crow::json::wvalue::list();
        int i = 0;
        for (const auto& [k, v] : summary_rows(s)) { rows[i]["label"] = k; rows[i]["value"] = v; ++i; }
        ctx["rows"] = std::move(rows);
        crow::json::wvalue shows = crow::json::wvalue::list();
        i = 0;
        for (const auto& e : s.shows) {
            auto& o = shows[i++];
            o["date"] = e.date; o["end_date"] = e.end_date; o["multi_day"] = e.end_date != e.date;
            o["title"] = e.title; o["location"] = e.location; o["days"] = e.days;
            o["visitors"] = e.visitors(); o["members"] = e.members; o["mocs"] = e.mocs;
        }
        ctx["shows"] = std::move(shows);
        ctx["has_shows"] = i > 0;
        ctx["asset_v"] = asset_version();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(crow::mustache::load("fancolab/summary.html").render(ctx).dump());
        return res;
    });

    // GET /fancolab/summary.csv?year= - Measure,Value
    CROW_ROUTE(app, "/fancolab/summary.csv")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto s = summarize(db, year_param(req.url_params.get("year")));
        auto fc = fan_colab_info(db);
        std::string out = "Measure,Value\n";
        out += csv_field("Year") + "," + csv_field(std::to_string(s.year)) + "\n";
        out += csv_field("Group") + "," + csv_field(fc.lug_name) + "\n";
        out += csv_field("Recognized LEGO Fan Community") + "," + csv_field(fc.recognized ? "Yes" : "No") + "\n";
        out += csv_field("Community Ambassador") + "," + csv_field(ambassador_line(s)) + "\n";
        for (const auto& [k, v] : summary_rows(s)) out += csv_field(k) + "," + csv_field(v) + "\n";
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"fan-colab-summary-" + std::to_string(s.year) + ".csv\"");
        res.write(out);
        return res;
    });

    // GET /fancolab/events.csv?year= - one row per public show
    CROW_ROUTE(app, "/fancolab/events.csv")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto s = summarize(db, year_param(req.url_params.get("year")));
        std::string out = "Start,End,Event,Location,Days,Visitors,Kids,Teens,Adults,Members,Displays,Display sq ft,Said they'd come\n";
        for (const auto& e : s.shows) {
            std::vector<std::string> cols{e.date, e.end_date, e.title, e.location, std::to_string(e.days),
                                          std::to_string(e.visitors()), std::to_string(e.kids), std::to_string(e.teens),
                                          std::to_string(e.adults), std::to_string(e.members), std::to_string(e.mocs),
                                          one_decimal(e.sqft), std::to_string(e.interest)};
            for (size_t c = 0; c < cols.size(); ++c) out += (c ? "," : "") + csv_field(cols[c]);
            out += "\n";
        }
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"fan-colab-events-" + std::to_string(s.year) + ".csv\"");
        res.write(out);
        return res;
    });

    // ── About page ──

    // GET /about - public, no login (feature "about_page")
    CROW_ROUTE(app, "/about")([&db, &settings](const crow::request&) {
        crow::response res;
        auto fc = fan_colab_info(db);
        crow::mustache::context ctx;
        std::string name = fc.lug_name.empty() ? std::string("Our LEGO fan community") : fc.lug_name;
        std::string title = settings.get("about_title", "");
        if (title.empty()) title = "About " + name;
        std::string html = render_markdown(settings.get("about_markdown", ""));
        std::string desc = utf8_truncate(html_to_text(html), 160);
        ctx["title"] = title;
        ctx["lug_name"] = name;
        ctx["body_html"] = html;
        ctx["has_body"] = !desc.empty();
        if (desc.empty()) desc = name + (fc.recognized ? " - a Recognized LEGO Fan Community." : " - a LEGO fan community.");
        ctx["description"] = desc;
        ctx["recognized"] = fc.recognized;
        ctx["has_logo"] = !settings.get("branding_logo_extension", "").empty();
        ctx["logo_v"] = settings.get("branding_logo_updated_at", "0");
        ctx["shows_link"] = Features::on("public_shows");
        if (settings.get("about_show_numbers", "1") == "1") {
            // The past twelve months, so the page never shows an empty January.
            int64_t shows = scalar(db,
                "SELECT COUNT(*) FROM lug_events WHERE is_private=0 AND status <> 'cancelled' "
                "AND start_time >= date('now','-12 months') AND start_time < date('now','+1 day')", {});
            int64_t visitors = scalar(db,
                "SELECT COALESCE(SUM(public_kids+public_teens+public_adults),0) FROM lug_events WHERE is_private=0 "
                "AND status <> 'cancelled' AND start_time >= date('now','-12 months') AND start_time < date('now','+1 day')", {});
            int64_t meetings = scalar(db,
                "SELECT COUNT(*) FROM meetings WHERE status <> 'cancelled' "
                "AND start_time >= date('now','-12 months') AND start_time < date('now','+1 day')", {});
            int64_t active = scalar(db,
                "SELECT COUNT(*) FROM (SELECT a.member_id FROM attendance a JOIN meetings mt ON mt.id = a.entity_id "
                " WHERE a.entity_type='meeting' AND mt.start_time >= date('now','-12 months') "
                " UNION SELECT eda.member_id FROM event_day_attendance eda JOIN event_days ed ON ed.id = eda.event_day_id "
                " WHERE ed.day_date >= date('now','-12 months'))", {});
            crow::json::wvalue stats = crow::json::wvalue::list();
            int i = 0;
            auto add = [&](int64_t v, const char* label) {
                if (v <= 0) return;
                stats[i]["value"] = v; stats[i]["label"] = label; ++i;
            };
            add(active, "active members");
            add(meetings, "meetings");
            add(shows, "public shows");
            add(visitors, "visitors welcomed");
            ctx["stats"] = std::move(stats);
            ctx["has_stats"] = i > 0;
        }
        {
            crow::json::wvalue ld;
            ld["@context"] = "https://schema.org";
            ld["@type"] = "Organization";
            ld["name"] = name;
            ld["description"] = desc;
            // Inside <script>: no raw < > & at all, so the text can't close the tag.
            std::string json;
            for (char c : ld.dump()) {
                if (c == '<') json += "\\u003c";
                else if (c == '>') json += "\\u003e";
                else if (c == '&') json += "\\u0026";
                else json += c;
            }
            ctx["json_ld"] = json;
        }
        ctx["asset_v"] = asset_version();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("Cache-Control", "public, no-cache");   // edits show up straight away
        res.write(crow::mustache::load("about/public.html").render(ctx).dump());
        return res;
    });

    // GET /about/photos/<file> - public, but only photos placed on the About page
    CROW_ROUTE(app, "/about/photos/<string>")([&db, photos](const crow::request&, const std::string& name) {
        crow::response res;
        {
            auto st = db.prepare("SELECT 1 FROM about_photos WHERE file=?");
            st.bind(1, name);
            if (!st.step()) { res.code = 404; return res; }
        }
        std::string bytes;
        if (!photos->read(name, bytes)) { res.code = 404; return res; }
        res.add_header("Content-Type", PhotoStore::content_type(name));
        res.add_header("Cache-Control", "public, max-age=86400");
        res.add_header("Content-Security-Policy", "default-src 'none'; sandbox");
        res.add_header("X-Content-Type-Options", "nosniff");
        res.write(bytes);
        return res;
    });

    // GET /settings/about - editor (admin)
    CROW_ROUTE(app, "/settings/about")([&app, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return page(req, app, render_about_editor(settings), "Public pages", "active_about");
    });

    // POST /settings/about - title, text (Markdown from the editor), options
    CROW_ROUTE(app, "/settings/about").methods("POST"_method)([&app, &db, &settings, photos, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        std::string md = f.get("markdown", kAboutMaxChars + 1);
        if (md.size() > kAboutMaxChars)
            return page(req, app, render_about_editor(settings, "That's too long: keep it under " +
                        std::to_string(kAboutMaxChars) + " characters.", true), "Public pages", "active_about", 400);
        std::string title = f.get("about_title", 120);
        bool enabled = f.get("enabled", 2) == "1";
        settings.set("about_title", title);
        settings.set("about_markdown", md);
        settings.set("about_show_numbers", f.get("show_numbers", 2) == "1" ? "1" : "0");
        Features::set("about_page", enabled);
        // Photos taken out of the text are deleted.
        auto keep = referenced_photos(md);
        std::vector<std::string> gone;
        {
            auto st = db.prepare("SELECT file FROM about_photos");
            while (st.step()) if (!keep.count(st.col_text(0))) gone.push_back(st.col_text(0));
        }
        for (const auto& g : gone) {
            auto st = db.prepare("DELETE FROM about_photos WHERE file=?");
            st.bind(1, g);
            st.step();
            photos->remove(g);
        }
        audit.log(req, app, "settings.update", "settings", 0, "About page",
                  std::string(enabled ? "Public" : "Hidden") + ", " + std::to_string(md.size()) + " characters");
        return page(req, app, render_about_editor(settings, enabled ? "Saved. Your About page is public at /about."
                                                                    : "Saved. The page is hidden until you tick \"Show the page\"."),
                    "Public pages", "active_about");
    });

    // POST /settings/about/photo - multipart "photo"; JSON {url} for the editor
    CROW_ROUTE(app, "/settings/about/photo").methods("POST"_method)([&app, &db, photos, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::multipart::message msg(req);
        std::string bytes;
        auto it = msg.part_map.find("photo");
        if (it != msg.part_map.end()) bytes = it->second.body;
        std::string err, file = photos->save(bytes, err);
        crow::json::wvalue out;
        res.add_header("Content-Type", "application/json");
        if (file.empty()) {
            res.code = 400;
            out["error"] = err;
            res.write(out.dump());
            return res;
        }
        {
            auto st = db.prepare("INSERT OR IGNORE INTO about_photos (file) VALUES (?)");
            st.bind(1, file);
            st.step();
        }
        audit.log(req, app, "settings.update", "settings", 0, "About page", "Photo uploaded");
        out["url"] = "/about/photos/" + file;
        res.write(out.dump());
        return res;
    });
}
