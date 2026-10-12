#include "routes/community/TreasuryRoutes.hpp"
#include "services/events/AttendanceService.hpp"
#include "utils/text/Csv.hpp"
#include "utils/text/Money.hpp"
#include "repositories/members/DuesRepository.hpp"
#include "services/TreasuryAccess.hpp"
#include "services/Features.hpp"
#include "services/ChapterMoney.hpp"
#include <crow/mustache.h>
#include <map>
#include <regex>

namespace {

int year_param(const crow::request& req) {
    int y = std::stoi(AttendanceService::today_ymd().substr(0, 4));
    if (const char* v = req.url_params.get("year")) {
        try { int p = std::stoi(v); if (p >= 1990 && p <= 2200) y = p; } catch (...) {}
    }
    return y;
}

struct Line {
    std::string on, kind, category, description, event, who, receipt;
    std::string chapter;   // what it's for: a chapter's name, or "" for the whole LUG (dues: the chapter's share)
    int64_t cents = 0, id = 0, member_id = 0, event_id = 0, chapter_id = 0, chapter_cents = 0, chapter_pct = 0;
    bool dues = false;
};

// Form fields from either a urlencoded or a multipart (file upload) body.
struct Fields {
    explicit Fields(const crow::request& req) : q("?" + req.body) {
        if (req.get_header_value("Content-Type").find("multipart/form-data") != std::string::npos) {
            mp = std::make_unique<crow::multipart::message>(req);
        }
    }
    std::string get(const char* k, size_t max = 200) const {
        if (mp) {
            auto it = mp->part_map.find(k);
            return it == mp->part_map.end() ? "" : it->second.body.substr(0, max);
        }
        const char* v = q.get(k);
        return v ? std::string(v).substr(0, max) : "";
    }
    std::string file(const char* k) const {
        if (!mp) return "";
        auto it = mp->part_map.find(k);
        return it == mp->part_map.end() ? "" : it->second.body;
    }
    crow::query_string q;
    std::unique_ptr<crow::multipart::message> mp;
};

// The "For" field: a chapter that exists, or 0 for the whole LUG.
int64_t chapter_param(SqliteDatabase& db, const std::string& v) {
    int64_t id = 0;
    try { id = std::stoll(v); } catch (...) {}
    if (id <= 0) return 0;
    auto st = db.prepare("SELECT 1 FROM chapters WHERE id=?");
    st.bind(1, id);
    return st.step() ? id : 0;
}

// `names`: show who paid each dues line (Settings > Treasury).
std::vector<Line> ledger(SqliteDatabase& db, const std::string& from, const std::string& to, bool names = true) {
    std::vector<Line> out;
    auto st = db.prepare(
        "SELECT t.id, t.entry_on, t.kind, t.category, t.amount_cents, t.description, COALESCE(e.title,''), COALESCE(m.display_name,''), t.receipt_file, "
        "COALESCE(t.event_id,0), COALESCE(t.chapter_id,0), COALESCE(c.name,'') "
        "FROM treasury_entries t LEFT JOIN lug_events e ON e.id=t.event_id LEFT JOIN members m ON m.id=t.recorded_by "
        "LEFT JOIN chapters c ON c.id=t.chapter_id "
        "WHERE t.entry_on >= ? AND t.entry_on < ?");
    st.bind(1, from); st.bind(2, to);
    while (st.step()) {
        Line l;
        l.id = st.col_int(0); l.on = st.col_text(1); l.kind = st.col_text(2); l.category = st.col_text(3);
        l.cents = st.col_int(4); l.description = st.col_text(5); l.event = st.col_text(6); l.who = st.col_text(7);
        l.receipt = st.col_text(8); l.event_id = st.col_int(9);
        l.chapter_id = st.col_int(10); l.chapter = st.col_text(11);
        out.push_back(std::move(l));
    }
    auto ds = db.prepare(
        "SELECT d.paid_on, d.amount_cents, COALESCE(m.display_name,''), d.method, d.member_id, "
        "COALESCE(d.chapter_id,0), COALESCE(c.name,''), d.amount_cents * COALESCE(d.chapter_pct,0) / 100, COALESCE(d.chapter_pct,0) "
        "FROM dues_payments d LEFT JOIN members m ON m.id=d.member_id LEFT JOIN chapters c ON c.id=d.chapter_id "
        "WHERE d.amount_cents > 0 AND d.paid_on >= ? AND d.paid_on < ?");
    ds.bind(1, from); ds.bind(2, to);
    while (ds.step()) {
        Line l;
        l.on = ds.col_text(0); l.kind = "income"; l.category = "Dues"; l.cents = ds.col_int(1);
        l.description = names ? "Dues - " + ds.col_text(2) + (ds.col_text(3).empty() ? "" : " (" + ds.col_text(3) + ")") : "Dues payment";
        l.member_id = names ? ds.col_int(4) : 0;
        l.chapter_id = ds.col_int(5);
        if (l.chapter_id > 0) {
            l.chapter_cents = ds.col_int(7);
            l.chapter = ds.col_text(6);
            l.chapter_pct = ds.col_int(8);
        }
        l.dues = true;
        out.push_back(std::move(l));
    }
    std::sort(out.begin(), out.end(), [](const Line& a, const Line& b) { return a.on != b.on ? a.on > b.on : a.id > b.id; });
    return out;
}

// Which part of the page, and an entry being edited (Ledger tab).
struct Show { treasury::View view; std::string tab = "overview"; int64_t editing = 0; };

std::string render(SqliteDatabase& db, int year, const Show& show, const std::string& flash = "", bool error = false) {
    std::string from = std::to_string(year) + "-01-01", to = std::to_string(year + 1) + "-01-01";
    const auto& v = show.view;
    auto lines = ledger(db, from, to, v.dues_names);

    int64_t dues = 0, income = 0, expense = 0;
    std::map<std::string, std::pair<int64_t, int64_t>> cats;     // category -> (in, out)
    std::map<std::string, std::pair<int64_t, int64_t>> by_event;
    int64_t month_in[12] = {0}, month_out[12] = {0};
    for (const auto& l : lines) {
        bool in = l.kind == "income";
        (l.dues ? dues : in ? income : expense) += l.cents;
        auto& c = cats[l.category.empty() ? "Uncategorized" : l.category];
        (in ? c.first : c.second) += l.cents;
        if (!l.event.empty()) { auto& e = by_event[l.event]; (in ? e.first : e.second) += l.cents; }
        int mo = 0;
        try { mo = std::stoi(l.on.substr(5, 2)) - 1; } catch (...) {}
        if (mo >= 0 && mo < 12) (in ? month_in[mo] : month_out[mo]) += l.cents;
    }
    // Balance carried in from all earlier years.
    int64_t before = query_int(db, "SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments WHERE paid_on < ?", from)
                   + query_int(db, "SELECT COALESCE(SUM(CASE kind WHEN 'income' THEN amount_cents ELSE -amount_cents END),0) "
                                "FROM treasury_entries WHERE entry_on < ?", from);
    int64_t net = dues + income - expense;

    crow::mustache::context ctx;
    if (!flash.empty()) ctx["flash"] = flash;
    ctx["flash_error"] = error;
    // Tabs: Overview (totals + breakdowns) and Ledger (entries, forms)
    const bool ledger_tab = v.ledger && (show.tab == "ledger" || !v.summary);
    ctx["tab_overview"] = !ledger_tab;
    ctx["tab_ledger"] = ledger_tab;
    ctx["show_tabs"] = v.summary && v.ledger;
    ctx["can_edit"] = v.edit;
    ctx["can_csv"] = v.ledger;
    ctx["see_summary"] = v.summary;
    ctx["see_receipts"] = v.receipts;
    ctx["tab_q"] = ledger_tab ? "&tab=ledger" : "";
    ctx["year"] = year;
    ctx["prev_year"] = year - 1;
    ctx["next_year"] = year + 1;
    ctx["today"] = AttendanceService::today_ymd();
    ctx["dues"] = money(dues);
    ctx["income"] = money(income);
    ctx["expense"] = money(expense);
    ctx["net"] = money(net);
    ctx["net_negative"] = net < 0;
    ctx["opening"] = money(before);
    ctx["closing"] = money(before + net);
    ctx["closing_negative"] = before + net < 0;

    crow::json::wvalue c = crow::json::wvalue::list();
    int i = 0;
    for (const auto& [name, v] : cats) {
        c[i]["name"] = name; c[i]["in"] = v.first ? money(v.first) : ""; c[i]["out"] = v.second ? money(v.second) : "";
        ++i;
    }
    ctx["categories"] = std::move(c);
    ctx["has_categories"] = i > 0;

    crow::json::wvalue ev = crow::json::wvalue::list();
    i = 0;
    for (const auto& [name, v] : by_event) {
        ev[i]["name"] = name; ev[i]["in"] = money(v.first); ev[i]["out"] = money(v.second);
        ev[i]["net"] = money(v.first - v.second); ev[i]["negative"] = v.first < v.second;
        ++i;
    }
    ctx["events"] = std::move(ev);
    ctx["has_events"] = i > 0;

    static const char* names[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    crow::json::wvalue months = crow::json::wvalue::list();
    for (int m = 0; m < 12; ++m) {
        months[m]["name"] = names[m];
        months[m]["in"] = month_in[m] ? money(month_in[m]) : "";
        months[m]["out"] = month_out[m] ? money(month_out[m]) : "";
    }
    ctx["months"] = std::move(months);

    crow::json::wvalue rows = crow::json::wvalue::list();
    i = 0;
    for (const auto& l : lines) {
        auto& r = rows[i++];
        r["id"] = l.id; r["on"] = l.on; r["category"] = l.category; r["description"] = l.description;
        r["event"] = l.event; r["who"] = l.who;
        if (!l.chapter.empty())
            r["chapter"] = l.dues ? money(l.chapter_cents) + " (" + std::to_string(l.chapter_pct) + "%) to " + l.chapter : "For " + l.chapter;
        r["amount"] = (l.kind == "income" ? "+" : "-") + money(l.cents);
        r["income"] = l.kind == "income";
        r["can_delete"] = v.edit && !l.dues;
        r["dues_line"] = l.dues;
        r["dues_edit"] = v.edit && l.dues && l.member_id > 0;
        r["member_id"] = l.member_id;
        if (!l.receipt.empty() && v.receipts) r["receipt"] = l.receipt;
        r["no_receipt"] = l.receipt.empty() && v.edit && !l.dues;
    }
    ctx["lines"] = std::move(rows);
    ctx["has_lines"] = i > 0;

    // Events from this year and last for the "for event" picker.
    crow::json::wvalue evs = crow::json::wvalue::list();
    auto es = db.prepare("SELECT id, title, substr(start_time,1,10) FROM lug_events WHERE start_time >= ? AND start_time < ? "
                         "ORDER BY start_time DESC");
    es.bind(1, std::to_string(year - 1) + "-01-01"); es.bind(2, to);
    i = 0;
    while (es.step()) { evs[i]["id"] = es.col_int(0); evs[i]["label"] = es.col_text(2) + " " + es.col_text(1); ++i; }
    ctx["event_options"] = std::move(evs);

    crow::json::wvalue known = crow::json::wvalue::list();
    auto ks = db.prepare("SELECT DISTINCT category FROM treasury_entries WHERE category <> '' ORDER BY category COLLATE NOCASE LIMIT 50");
    i = 0;
    while (ks.step()) known[i++]["name"] = ks.col_text(0);
    ctx["known_categories"] = std::move(known);

    // By chapter: each chapter's share of dues and what was marked for it; the rest is the LUG's
    {
        auto bs = chapter_money::buckets(db, from, to);
        crow::json::wvalue rows_c = crow::json::wvalue::list();
        crow::json::wvalue ch_opts = crow::json::wvalue::list();
        int k = 0, o = 0;
        for (const auto& b : bs) {
            auto& r = rows_c[k++];
            r["name"] = b.name; r["general"] = b.chapter_id == 0;
            r["dues"] = money(b.dues); r["income"] = money(b.income); r["expense"] = money(b.expense);
            r["net"] = money(b.net()); r["net_negative"] = b.net() < 0;
            r["closing"] = money(b.closing()); r["closing_negative"] = b.closing() < 0;
            if (b.chapter_id > 0) { ch_opts[o]["id"] = b.chapter_id; ch_opts[o]["name"] = b.name; ++o; }
        }
        ctx["by_chapter"] = std::move(rows_c);
        ctx["has_chapters"] = o > 0 && Features::on("chapters");
        ctx["chapter_options"] = std::move(ch_opts);
    }

    crow::json::wvalue members = crow::json::wvalue::list();
    auto ms = db.prepare("SELECT id, display_name FROM members ORDER BY display_name COLLATE NOCASE");
    i = 0;
    while (ms.step()) { members[i]["id"] = ms.col_int(0); members[i]["name"] = ms.col_text(1); ++i; }
    ctx["members"] = std::move(members);
    ctx["year_end"] = std::to_string(year) + "-12-31";
    if (v.edit && show.editing > 0) {
        auto st = db.prepare("SELECT entry_on, kind, category, amount_cents, description, COALESCE(event_id,0), receipt_file, "
                             "COALESCE(chapter_id,0) FROM treasury_entries WHERE id=?");
        st.bind(1, show.editing);
        if (st.step()) {
            crow::json::wvalue e;
            e["id"] = show.editing;
            e["on"] = st.col_text(0);
            e["income"] = st.col_text(1) == "income";
            e["category"] = st.col_text(2);
            e["amount"] = money(st.col_int(3)).substr(1);
            e["description"] = st.col_text(4);
            e["has_receipt"] = !st.col_text(6).empty();
            {
                const int64_t ch = st.col_int(7);
                crow::json::wvalue co = crow::json::wvalue::list();
                auto cs = db.prepare("SELECT id, name FROM chapters ORDER BY name COLLATE NOCASE");
                int k = 0;
                while (cs.step()) { co[k]["id"] = cs.col_int(0); co[k]["name"] = cs.col_text(1); co[k]["selected"] = cs.col_int(0) == ch; ++k; }
                e["chapter_options"] = std::move(co);
                e["has_chapter_options"] = k > 0 && Features::on("chapters");
            }
            const int64_t ev_id = st.col_int(5);
            crow::json::wvalue opts = crow::json::wvalue::list();
            auto eo = db.prepare("SELECT id, title, substr(start_time,1,10) FROM lug_events WHERE start_time >= ? AND start_time < ? "
                                 "OR id = ? ORDER BY start_time DESC");
            eo.bind(1, std::to_string(year - 1) + "-01-01"); eo.bind(2, to); eo.bind(3, ev_id);
            int k = 0;
            while (eo.step()) {
                opts[k]["id"] = eo.col_int(0); opts[k]["label"] = eo.col_text(2) + " " + eo.col_text(1);
                opts[k]["selected"] = eo.col_int(0) == ev_id; ++k;
            }
            e["event_options"] = std::move(opts);
            ctx["editing"] = std::move(e);
        }
    }
    return crow::mustache::load("treasury/_content.html").render(ctx).dump();
}

// After a change (treasurers only): the Ledger tab.
crow::response fragment(SqliteDatabase& db, int year, int code, const std::string& flash) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    Show show;
    show.view.edit = show.view.summary = show.view.ledger = show.view.receipts = show.view.dues_names = true;
    show.tab = "ledger";
    res.write(render(db, year, show, flash, code >= 400));
    return res;
}

template <typename App>
treasury::View viewer(const crow::request& req, App& app, SettingsRepository& settings) {
    auto& a = app.template get_context<AuthMiddleware>(req).auth;
    return treasury::view_for(&settings, a.can_treasury(), a.is_chapter_lead(), a.authenticated);
}

} // namespace

void register_treasury_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuditService& audit,
                              std::shared_ptr<PhotoStore> receipts) {

    // GET /treasury/receipts/<name> - treasurer/admin only; PDFs download.
    CROW_ROUTE(app, "/treasury/receipts/<string>")([&app, &db, &settings, receipts](const crow::request& req, const std::string& name) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        if (!viewer(req, app, settings).receipts) { res.code = 403; return res; }
        auto known = db.prepare("SELECT 1 FROM treasury_entries WHERE receipt_file=?");
        known.bind(1, name);
        std::string bytes;
        if (!known.step() || !receipts->read(name, bytes)) { res.code = 404; return res; }
        bool pdf = name.size() > 4 && name.compare(name.size() - 4, 4, ".pdf") == 0;
        res.add_header("Content-Type", PhotoStore::content_type(name));
        res.add_header("Content-Security-Policy", "default-src 'none'; sandbox");
        res.add_header("Cache-Control", "private, no-store");
        if (pdf) res.add_header("Content-Disposition", "attachment; filename=\"receipt-" + name + "\"");
        res.write(bytes);
        return res;
    });

    // POST /treasury/<id>/receipt - attach or replace an entry's receipt
    CROW_ROUTE(app, "/treasury/<int>/receipt").methods("POST"_method)([&app, &db, &audit, receipts](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        std::string on, old;
        {
            auto st = db.prepare("SELECT entry_on, receipt_file FROM treasury_entries WHERE id=?");
            st.bind(1, (int64_t)id);
            if (!st.step()) return fragment(db, year_param(req), 404, "That entry doesn't exist.");
            on = st.col_text(0); old = st.col_text(1);
        }
        int year = std::stoi(on.substr(0, 4));
        Fields f(req);
        std::string err, name = receipts->save_receipt(f.file("receipt"), err);
        if (name.empty()) return fragment(db, year, 400, err.empty() ? "Choose a file." : err);
        {
            auto up = db.prepare("UPDATE treasury_entries SET receipt_file=? WHERE id=?");
            up.bind(1, name); up.bind(2, (int64_t)id);
            up.step();
        }
        if (!old.empty()) receipts->remove(old);
        audit.log(req, app, "treasury.receipt", "treasury", id, on, "Attached receipt");
        return fragment(db, year, 200, "Receipt attached.");
    });

    // POST /treasury/dues - record a member's dues payment (treasurer/admin)
    CROW_ROUTE(app, "/treasury/dues").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        Fields f(req);
        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
        std::string paid_on = f.get("paid_on", 10), covers = f.get("covers_until", 10);
        int64_t cents = parse_cents(f.get("amount", 20));
        int64_t member = 0;
        try { member = std::stoll(f.get("member_id", 20)); } catch (...) {}
        int year = std::regex_match(paid_on, ymd) ? std::stoi(paid_on.substr(0, 4)) : year_param(req);
        if (member <= 0 || !std::regex_match(paid_on, ymd) || !std::regex_match(covers, ymd) || cents < 0 || covers < paid_on)
            return fragment(db, year, 400, "Pick a member, the date paid, an amount and when it covers until.");
        std::string name;
        {
            auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
            st.bind(1, member);
            if (!st.step()) return fragment(db, year, 404, "That member doesn't exist.");
            name = st.col_text(0);
        }
        DuesRepository(db).record(member, paid_on, cents, f.get("method", 40), covers, f.get("note", 300),
                                  app.get_context<AuthMiddleware>(req).auth.member_id);
        audit.log(req, app, "member.dues_payment", "member", member, name,
                  "Recorded " + money(cents) + " dues, covers until " + covers + " (Treasury)");
        return fragment(db, year, 200, "Recorded " + money(cents) + " dues for " + name + ".");
    });

    CROW_ROUTE(app, "/treasury")([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        Show show;
        show.view = viewer(req, app, settings);
        if (!show.view.any()) { res.code = 403; res.write("Not allowed"); return res; }
        if (const char* t = req.url_params.get("tab")) show.tab = t;
        std::string body = render(db, year_param(req), show);
        return html_page(req, app, body, "Treasury", "active_treasury");
    });

    // GET /treasury/<id>/edit - the Ledger tab with that entry's edit form
    CROW_ROUTE(app, "/treasury/<int>/edit")([&app, &db](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        auto st = db.prepare("SELECT entry_on FROM treasury_entries WHERE id=?");
        st.bind(1, (int64_t)id);
        if (!st.step()) return fragment(db, year_param(req), 404, "That entry doesn't exist.");
        int year = std::stoi(st.col_text(0).substr(0, 4));
        st.reset();
        Show show;
        show.view.edit = show.view.summary = show.view.ledger = show.view.receipts = show.view.dues_names = true;
        show.tab = "ledger";
        show.editing = id;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(db, year, show));
        return res;
    });

    CROW_ROUTE(app, "/treasury.csv")([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto v = viewer(req, app, settings);
        if (!v.ledger) { res.code = 403; return res; }
        int year = year_param(req);
        auto lines = ledger(db, std::to_string(year) + "-01-01", std::to_string(year + 1) + "-01-01", v.dues_names);
        std::string out = "Date,Type,Category,Amount,Description,Event,Recorded by,Chapter\n";
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            char amt[32];
            std::snprintf(amt, sizeof(amt), "%s%lld.%02lld", it->kind == "income" ? "" : "-",
                          static_cast<long long>(it->cents / 100), static_cast<long long>(it->cents % 100));
            out += csv_field(it->on) + "," + it->kind + "," + csv_field(it->category) + "," + amt + "," +
                   csv_field(it->description) + "," + csv_field(it->event) + "," + csv_field(it->who) + "," +
                   csv_field(it->dues && it->chapter_id > 0
                             ? money(it->chapter_cents) + " (" + std::to_string(it->chapter_pct) + "%) to " + it->chapter : it->chapter) + "\n";
        }
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"treasury-" + std::to_string(year) + ".csv\"");
        res.write(out);
        return res;
    });

    // POST /treasury - record income or an expense
    CROW_ROUTE(app, "/treasury").methods("POST"_method)([&app, &db, &audit, receipts](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        Fields f(req);
        auto gp = [&](const char* k, size_t max = 200) { return f.get(k, max); };
        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
        std::string on = gp("entry_on", 10), kind = gp("kind", 10), category = gp("category", 60);
        int64_t cents = parse_cents(gp("amount", 20));
        int year = std::regex_match(on, ymd) ? std::stoi(on.substr(0, 4)) : year_param(req);
        if (!std::regex_match(on, ymd) || (kind != "income" && kind != "expense") || cents <= 0)
            return fragment(db, year, 400, "Enter a date, income or expense, and an amount like 25.00.");
        std::string receipt;
        if (std::string bytes = f.file("receipt"); !bytes.empty()) {
            std::string err;
            receipt = receipts->save_receipt(bytes, err);
            if (receipt.empty()) return fragment(db, year, 400, err);
        }
        int64_t event_id = 0;
        try { event_id = std::stoll(gp("event_id", 20)); } catch (...) {}
        const int64_t chapter = chapter_param(db, gp("chapter_id", 20));
        auto ins = db.prepare("INSERT INTO treasury_entries (entry_on, kind, category, amount_cents, description, event_id, recorded_by, receipt_file, chapter_id) "
                              "VALUES (?,?,?,?,?,?,?,?,?) RETURNING id");
        ins.bind(1, on); ins.bind(2, kind); ins.bind(3, category); ins.bind(4, cents);
        ins.bind(5, gp("description", 300));
        if (event_id > 0) {
            auto chk = db.prepare("SELECT 1 FROM lug_events WHERE id=?");
            chk.bind(1, event_id);
            if (chk.step()) ins.bind(6, event_id); else ins.bind_null(6);
        } else {
            ins.bind_null(6);
        }
        ins.bind(7, app.get_context<AuthMiddleware>(req).auth.member_id);
        ins.bind(8, receipt);
        if (chapter > 0) ins.bind(9, chapter); else ins.bind_null(9);
        ins.step();
        int64_t id = ins.col_int(0);
        ins.reset();
        audit.log(req, app, "treasury.add", "treasury", id, category.empty() ? kind : category,
                  (kind == "income" ? "+" : "-") + money(cents) + " on " + on + (chapter > 0 ? " for chapter " + std::to_string(chapter) : ""));
        return fragment(db, year, 200, std::string(kind == "income" ? "Income" : "Expense") + " of " + money(cents) + " recorded.");
    });

    // POST /treasury/<id> - change an entry (date, type, amount, category,
    // event, description; replace or remove its receipt)
    CROW_ROUTE(app, "/treasury/<int>").methods("POST"_method)([&app, &db, &audit, receipts](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        std::string old_on, old_kind, old_cat, old_desc, old_receipt;
        int64_t old_cents = 0, old_event = 0, old_chapter = 0;
        {
            auto st = db.prepare("SELECT entry_on, kind, category, amount_cents, description, COALESCE(event_id,0), receipt_file, "
                                 "COALESCE(chapter_id,0) FROM treasury_entries WHERE id=?");
            st.bind(1, (int64_t)id);
            if (!st.step()) return fragment(db, year_param(req), 404, "That entry doesn't exist.");
            old_on = st.col_text(0); old_kind = st.col_text(1); old_cat = st.col_text(2); old_cents = st.col_int(3);
            old_desc = st.col_text(4); old_event = st.col_int(5); old_receipt = st.col_text(6); old_chapter = st.col_int(7);
        }
        Fields f(req);
        static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
        std::string on = f.get("entry_on", 10), kind = f.get("kind", 10), category = f.get("category", 60);
        std::string description = f.get("description", 300);
        int64_t cents = parse_cents(f.get("amount", 20));
        if (!std::regex_match(on, ymd) || (kind != "income" && kind != "expense") || cents <= 0) {
            crow::response r;
            r.code = 400;
            r.add_header("Content-Type", "text/html; charset=utf-8");
            Show show;
            show.view.edit = show.view.summary = show.view.ledger = show.view.receipts = show.view.dues_names = true;
            show.tab = "ledger"; show.editing = id;
            r.write(render(db, std::stoi(old_on.substr(0, 4)), show, "Enter a date, income or expense, and an amount like 25.00.", true));
            return r;
        }
        int64_t event_id = 0;
        try { event_id = std::stoll(f.get("event_id", 20)); } catch (...) {}
        if (event_id > 0) {
            auto chk = db.prepare("SELECT 1 FROM lug_events WHERE id=?");
            chk.bind(1, event_id);
            if (!chk.step()) event_id = 0;
        }
        const int64_t chapter = chapter_param(db, f.get("chapter_id", 20));
        std::string receipt = old_receipt;
        bool receipt_changed = false;
        if (std::string bytes = f.file("receipt"); !bytes.empty()) {
            std::string err;
            receipt = receipts->save_receipt(bytes, err);
            if (receipt.empty()) return fragment(db, std::stoi(old_on.substr(0, 4)), 400, err);
            receipt_changed = true;
        } else if (f.get("remove_receipt") == "1") {
            receipt.clear();
            receipt_changed = !old_receipt.empty();
        }
        {
            auto up = db.prepare("UPDATE treasury_entries SET entry_on=?, kind=?, category=?, amount_cents=?, description=?, "
                                 "event_id=?, receipt_file=?, chapter_id=? WHERE id=?");
            up.bind(1, on); up.bind(2, kind); up.bind(3, category); up.bind(4, cents); up.bind(5, description);
            if (event_id > 0) up.bind(6, event_id); else up.bind_null(6);
            up.bind(7, receipt);
            if (chapter > 0) up.bind(8, chapter); else up.bind_null(8);
            up.bind(9, (int64_t)id);
            up.step();
        }
        if (receipt_changed && !old_receipt.empty() && old_receipt != receipt) receipts->remove(old_receipt);
        std::string changes;
        auto note = [&](const std::string& what, const std::string& a, const std::string& b) {
            if (a != b) changes += (changes.empty() ? "" : "; ") + what + ": " + (a.empty() ? "-" : a) + " -> " + (b.empty() ? "-" : b);
        };
        note("date", old_on, on); note("type", old_kind, kind); note("amount", money(old_cents), money(cents));
        note("category", old_cat, category); note("description", old_desc, description);
        note("event", old_event ? std::to_string(old_event) : "", event_id ? std::to_string(event_id) : "");
        note("chapter", old_chapter ? std::to_string(old_chapter) : "LUG", chapter ? std::to_string(chapter) : "LUG");
        if (receipt_changed) changes += std::string(changes.empty() ? "" : "; ") + (receipt.empty() ? "receipt removed" : "receipt replaced");
        audit.log(req, app, "treasury.edit", "treasury", id, category.empty() ? kind : category,
                  changes.empty() ? "No changes" : "Edited: " + changes);
        return fragment(db, std::stoi(on.substr(0, 4)), 200, "Entry updated.");
    });

    CROW_ROUTE(app, "/treasury/<int>/delete").methods("POST"_method)([&app, &db, &audit, receipts](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        auto del = db.prepare("DELETE FROM treasury_entries WHERE id=? RETURNING entry_on, kind, amount_cents, category, receipt_file");
        del.bind(1, (int64_t)id);
        if (!del.step()) return fragment(db, year_param(req), 404, "That entry doesn't exist.");
        std::string on = del.col_text(0), kind = del.col_text(1), category = del.col_text(3), receipt = del.col_text(4);
        int64_t cents = del.col_int(2);
        del.reset();
        if (!receipt.empty()) receipts->remove(receipt);
        audit.log(req, app, "treasury.delete", "treasury", id, category.empty() ? kind : category,
                  "Deleted " + (kind == "income" ? std::string("+") : std::string("-")) + money(cents) + " on " + on);
        return fragment(db, std::stoi(on.substr(0, 4)), 200, "Entry deleted.");
    });

    // Settings > Treasury: who can see each part (admins)
    auto settings_page = [&settings](const std::string& flash) {
        crow::mustache::context ctx;
        struct P { const char* key; const char* title; const char* help; treasury::Part part; };
        static const P parts[] = {
            {"summary", "Overview", "Totals for the year, start and end balance, and the by-category, by-event and by-month breakdowns.", treasury::Part::Summary},
            {"ledger", "Ledger", "Every entry: date, category, description, amount, and the CSV export.", treasury::Part::Ledger},
            {"receipts", "Receipts", "The receipt files attached to entries (needs the ledger).", treasury::Part::Receipts},
            {"dues_names", "Who paid dues", "Member names on dues lines in the ledger; without this they show as \"Dues payment\" (needs the ledger).", treasury::Part::DuesNames},
        };
        crow::json::wvalue rows = crow::json::wvalue::list();
        int i = 0;
        for (const auto& p : parts) {
            const std::string a = treasury::audience(&settings, p.part);
            rows[i]["key"] = p.key; rows[i]["title"] = p.title; rows[i]["help"] = p.help;
            rows[i]["treasurers"] = a == "treasurers"; rows[i]["leads"] = a == "leads"; rows[i]["members"] = a == "members";
            ++i;
        }
        ctx["parts"] = std::move(rows);
        ctx["treasury_on"] = Features::on("treasury");
        if (!flash.empty()) ctx["flash"] = flash;
        return crow::mustache::load("settings/_treasury.html").render(ctx).dump();
    };
    CROW_ROUTE(app, "/settings/treasury")([&app, settings_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, settings_page(""), "Treasury settings", "active_treasury_settings");
    });
    CROW_ROUTE(app, "/settings/treasury").methods("POST"_method)([&app, &settings, &audit, settings_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Fields f(req);
        std::string changes;
        for (auto p : {treasury::Part::Summary, treasury::Part::Ledger, treasury::Part::Receipts, treasury::Part::DuesNames}) {
            std::string key = treasury::setting_key(p);
            std::string field = key.substr(std::string("treasury_view_").size());
            std::string v = f.get(field.c_str(), 20);
            if (!treasury::valid_audience(v)) v = "treasurers";
            if (v != treasury::audience(&settings, p)) changes += (changes.empty() ? "" : ", ") + field + " -> " + v;
            settings.set(key, v);
        }
        audit.log(req, app, "settings.update", "settings", 0, "Treasury", changes.empty() ? "Treasury visibility saved (no changes)"
                                                                                        : "Treasury visibility: " + changes);
        return html_page(req, app, settings_page("Saved."), "Treasury settings", "active_treasury_settings");
    });
}
