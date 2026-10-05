#include "routes/community/InventoryRoutes.hpp"
#include "utils/text/Plural.hpp"
#include "live/LiveHub.hpp"
#include "utils/web/FormBody.hpp"
#include "services/events/AttendanceService.hpp"
#include "routes/events/EventAccess.hpp"
#include "utils/text/Csv.hpp"
#include <crow/mustache.h>
#include <map>
#include <regex>

// Inventory model (migrations 059, 065):
//   inventory_items.quantity  - how many the LUG owns in total
//   inventory_stock           - how many sit at each storage location
//   inventory_loans (open)    - how many are out with members
//   not placed                = total - at locations - out on loan

namespace {

using Form = FormBody;

const char* KINDS[][2] = {{"storage", "Storage unit"}, {"trailer", "Trailer"}, {"home", "At a member's home"},
                          {"venue", "At a venue"}, {"other", "Other"}};

std::string kind_label(const std::string& k) {
    for (const auto& p : KINDS) if (k == p[0]) return p[1];
    return "Other";
}

const char* CONDITIONS[][2] = {{"good", "Good"}, {"worn", "Worn"}, {"needs_repair", "Needs repair"}, {"broken", "Broken"}};

std::string condition_label(const std::string& c) {
    for (const auto& p : CONDITIONS) if (c == p[0]) return p[1];
    return "Good";
}

bool valid_kind(const std::string& k) {
    for (const auto& p : KINDS) if (k == p[0]) return true;
    return false;
}

bool valid_ymd(const std::string& s) {
    static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
    return s.empty() || std::regex_match(s, ymd);
}

int64_t out_count(SqliteDatabase& db, int64_t item) {
    return query_int(db, "SELECT COALESCE(SUM(quantity),0) FROM inventory_loans WHERE item_id=? AND returned_at IS NULL", item);
}
int64_t placed_count(SqliteDatabase& db, int64_t item) {
    return query_int(db, "SELECT COALESCE(SUM(quantity),0) FROM inventory_stock WHERE item_id=?", item);
}
int64_t stock_at(SqliteDatabase& db, int64_t item, int64_t loc) {
    return query_int(db, "SELECT COALESCE(SUM(quantity),0) FROM inventory_stock WHERE item_id=? AND location_id=?", item, loc);
}
// How many are neither at a location nor out on loan.
int64_t unplaced(SqliteDatabase& db, int64_t item) {
    int64_t total = query_int(db, "SELECT quantity FROM inventory_items WHERE id=?", item);
    return total - placed_count(db, item) - out_count(db, item);
}

// Adds (delta > 0) or takes (delta < 0) stock at a location; removes rows
// that reach zero. Callers check there's enough first (inside a Transaction).
void adjust_stock(SqliteDatabase& db, int64_t item, int64_t loc, int64_t delta) {
    if (loc <= 0 || delta == 0) return;
    int64_t now = stock_at(db, item, loc), next = now + delta;
    if (next <= 0) {
        auto del = db.prepare("DELETE FROM inventory_stock WHERE item_id=? AND location_id=?");
        del.bind(1, item); del.bind(2, loc);
        del.step();
    } else if (now == 0) {
        auto ins = db.prepare("INSERT INTO inventory_stock (item_id, location_id, quantity) VALUES (?,?,?)");
        ins.bind(1, item); ins.bind(2, loc); ins.bind(3, next);
        ins.step();
    } else {
        auto up = db.prepare("UPDATE inventory_stock SET quantity=? WHERE item_id=? AND location_id=?");
        up.bind(1, next); up.bind(2, item); up.bind(3, loc);
        up.step();
    }
}

// Owner from a form ("0"/empty = the LUG). Returns false if the member doesn't exist.
bool read_owner(SqliteDatabase& db, int64_t owner, std::string& name) {
    name.clear();
    if (owner <= 0) return true;
    auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
    st.bind(1, owner);
    if (!st.step()) return false;
    name = st.col_text(0);
    return true;
}
// "the LUG", the member's name, or "<name> (left - reassign)"
std::string owner_label(int64_t owner_id, const std::string& current, const std::string& saved) {
    if (owner_id > 0) return current;
    if (!saved.empty()) return saved + " (left - reassign)";
    return "the LUG";
}

bool location_exists(SqliteDatabase& db, int64_t loc) {
    return loc > 0 && query_int(db, "SELECT COUNT(*) FROM storage_locations WHERE id=? AND archived=0", loc) > 0;
}

std::string location_name(SqliteDatabase& db, int64_t loc) {
    if (loc <= 0) return "not placed";
    auto st = db.prepare("SELECT name FROM storage_locations WHERE id=?");
    st.bind(1, loc);
    return st.step() ? st.col_text(0) : "?";
}

std::string render(const crow::request& req, LugApp& app, SqliteDatabase& db, const std::string& flash = "",
                   bool error = false) {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    bool manage = a.can("inventory.manage");
    std::string today = AttendanceService::today_ymd();
    int64_t filter = 0;
    if (const char* l = req.url_params.get("location")) { try { filter = std::stoll(l); } catch (...) {} }
    std::string owner_filter;   // "mine" | "lug" | "" (everything)
    if (const char* o = req.url_params.get("owner")) owner_filter = std::string(o) == "mine" || std::string(o) == "lug" ? o : "";

    crow::mustache::context ctx;
    if (!flash.empty()) ctx["flash"] = flash;
    ctx["flash_error"] = error;
    ctx["can_manage"] = manage;
    ctx["today"] = today;
    ctx["owner_mine"] = owner_filter == "mine";
    ctx["owner_lug"] = owner_filter == "lug";
    ctx["owner_all"] = owner_filter.empty();

    std::vector<std::pair<int64_t, std::string>> people;
    if (manage) {
        auto ms = db.prepare("SELECT id, display_name FROM members ORDER BY display_name COLLATE NOCASE");
        while (ms.step()) people.emplace_back(ms.col_int(0), ms.col_text(1));
    }

    // ── Locations ──
    std::map<int64_t, std::string> loc_names;
    crow::json::wvalue locs = crow::json::wvalue::list();
    crow::json::wvalue loc_opts = crow::json::wvalue::list();
    crow::json::wvalue mine = crow::json::wvalue::list();
    int nl = 0, nmine = 0;
    {
        auto st = db.prepare(
            "SELECT s.id, s.name, s.kind, s.address, s.notes, s.keeper_id, COALESCE(m.display_name,''), "
            "COALESCE(s.owner_member_id,0), s.owner_name, COALESCE(om.display_name,''), "
            "COALESCE((SELECT SUM(k.quantity) FROM inventory_stock k JOIN inventory_items i ON i.id=k.item_id "
            "          WHERE k.location_id=s.id AND i.archived=0),0), "
            "(SELECT COUNT(*) FROM inventory_stock k JOIN inventory_items i ON i.id=k.item_id WHERE k.location_id=s.id AND i.archived=0) "
            "FROM storage_locations s LEFT JOIN members m ON m.id=s.keeper_id LEFT JOIN members om ON om.id=s.owner_member_id "
            "WHERE s.archived=0 "
            "ORDER BY s.name COLLATE NOCASE");
        while (st.step()) {
            int64_t id = st.col_int(0);
            loc_names[id] = st.col_text(1);
            auto& l = locs[nl];
            l["id"] = id; l["name"] = st.col_text(1); l["kind"] = st.col_text(2); l["kind_label"] = kind_label(st.col_text(2));
            l["address"] = st.col_text(3); l["notes"] = st.col_text(4);
            l["keeper_id"] = st.col_int(5); l["keeper"] = st.col_text(6);
            l["units"] = st.col_int(10); l["units_text"] = count_of(st.col_int(10), "item", "items"); l["item_kinds"] = st.col_int(11);
            const int64_t lowner = st.col_int(7);
            l["owner"] = owner_label(lowner, st.col_text(9), st.col_text(8));
            l["owner_member"] = lowner > 0 || !st.col_text(8).empty();
            l["selected"] = id == filter;
            l["can_manage"] = manage;
            if (manage) {
                l["keeper_options"] = crow::json::wvalue::list();
                int k = 0;
                for (const auto& [pid, pname] : people) {
                    l["keeper_options"][k]["id"] = pid; l["keeper_options"][k]["name"] = pname;
                    l["keeper_options"][k]["selected"] = pid == st.col_int(5);
                    l["owner_options"][k]["id"] = pid; l["owner_options"][k]["name"] = pname;
                    l["owner_options"][k]["selected"] = pid == lowner; ++k;
                }
            }
            for (int k = 0; k < 5; ++k) {
                l["kinds"][k]["value"] = KINDS[k][0]; l["kinds"][k]["label"] = KINDS[k][1];
                l["kinds"][k]["selected"] = st.col_text(2) == KINDS[k][0];
            }
            loc_opts[nl]["id"] = id; loc_opts[nl]["name"] = st.col_text(1);
            if (st.col_int(5) == a.member_id) {
                mine[nmine]["id"] = id; mine[nmine]["name"] = st.col_text(1); mine[nmine]["units"] = st.col_int(10); mine[nmine]["units_text"] = count_of(st.col_int(10), "item", "items"); ++nmine;
            }
            ++nl;
        }
    }
    ctx["locations"] = std::move(locs);
    ctx["has_locations"] = nl > 0;
    ctx["location_options"] = std::move(loc_opts);
    ctx["my_locations"] = std::move(mine);
    ctx["has_my_locations"] = nmine > 0;
    if (filter > 0 && loc_names.count(filter)) ctx["filter_name"] = loc_names[filter];
    crow::json::wvalue kinds = crow::json::wvalue::list();
    for (int k = 0; k < 5; ++k) { kinds[k]["value"] = KINDS[k][0]; kinds[k]["label"] = KINDS[k][1]; }
    ctx["kinds"] = std::move(kinds);

    // ── Stock per item ──
    std::map<int64_t, std::vector<std::pair<int64_t, int64_t>>> stock;   // item -> (location, qty)
    {
        auto st = db.prepare("SELECT item_id, location_id, quantity FROM inventory_stock ORDER BY quantity DESC");
        while (st.step()) stock[st.col_int(0)].emplace_back(st.col_int(1), st.col_int(2));
    }

    // ── Items ──
    crow::json::wvalue items = crow::json::wvalue::list();
    crow::json::wvalue options = crow::json::wvalue::list();
    int i = 0, n_opt = 0;
    auto st = db.prepare(
        "SELECT i.id, i.name, i.category, i.quantity, i.notes, "
        "COALESCE((SELECT SUM(l.quantity) FROM inventory_loans l WHERE l.item_id=i.id AND l.returned_at IS NULL),0), "
        "i.photo_file, i.condition, i.condition_note, COALESCE(i.owner_member_id,0), i.owner_name, COALESCE(om.display_name,'') "
        "FROM inventory_items i LEFT JOIN members om ON om.id=i.owner_member_id WHERE i.archived=0 "
        "ORDER BY i.category COLLATE NOCASE, i.name COLLATE NOCASE");
    while (st.step()) {
        int64_t id = st.col_int(0), qty = st.col_int(3), out = st.col_int(5);
        int64_t placed = 0;
        bool here = filter == 0;
        crow::json::wvalue where = crow::json::wvalue::list();
        int w = 0;
        for (const auto& [loc, n] : stock[id]) {
            placed += n;
            if (loc == filter) here = true;
            where[w]["location_id"] = loc; where[w]["name"] = loc_names.count(loc) ? loc_names[loc] : "?";
            where[w]["quantity"] = n; where[w]["item_id"] = id; ++w;
        }
        int64_t free_ = qty - placed - out;
        if (!here) continue;
        const int64_t owner = st.col_int(9);
        if (owner_filter == "mine" && owner != a.member_id) continue;
        if (owner_filter == "lug" && (owner > 0 || !st.col_text(10).empty())) continue;
        auto& it = items[i++];
        it["id"] = id;
        it["name"] = st.col_text(1);
        it["category"] = st.col_text(2);
        it["quantity"] = qty;
        it["notes"] = st.col_text(4);
        if (!st.col_text(6).empty()) it["photo"] = st.col_text(6);
        std::string cond = st.col_text(7);
        it["condition_label"] = condition_label(cond);
        it["condition_issue"] = cond != "good";
        it["condition_broken"] = cond == "broken" || cond == "needs_repair";
        if (!st.col_text(8).empty()) it["condition_note"] = st.col_text(8);
        for (int c = 0; c < 4; ++c) {
            it["conditions"][c]["value"] = CONDITIONS[c][0]; it["conditions"][c]["label"] = CONDITIONS[c][1];
            it["conditions"][c]["selected"] = cond == CONDITIONS[c][0];
        }
        it["available"] = qty - out;
        it["out"] = out;
        it["has_out"] = out > 0;
        it["all_out"] = qty - out <= 0;
        it["where"] = std::move(where);
        it["unplaced"] = free_;
        it["has_unplaced"] = free_ > 0;
        it["can_manage"] = manage;
        it["owner"] = owner_label(owner, st.col_text(11), st.col_text(10));
        it["owner_member"] = owner > 0 || !st.col_text(10).empty();
        it["owner_left"] = owner == 0 && !st.col_text(10).empty();
        it["owned_by_me"] = owner > 0 && owner == a.member_id;
        if (manage) {
            int k = 0;
            it["owner_options"] = crow::json::wvalue::list();
            for (const auto& [pid, pname] : people) {
                it["owner_options"][k]["id"] = pid; it["owner_options"][k]["name"] = pname;
                it["owner_options"][k]["selected"] = pid == owner; ++k;
            }
        }
        it["locations"] = crow::json::wvalue::list();
        int k = 0;
        for (const auto& [lid, lname] : loc_names) { it["locations"][k]["id"] = lid; it["locations"][k]["name"] = lname; ++k; }
        if (qty - out > 0) {
            options[n_opt]["id"] = id;
            options[n_opt]["label"] = st.col_text(1) + " (" + std::to_string(qty - out) + " available)";
            ++n_opt;
        }
    }
    ctx["items"] = std::move(items);
    ctx["has_items"] = i > 0;
    ctx["item_options"] = std::move(options);
    ctx["has_item_options"] = n_opt > 0;

    // ── Open loans: managers see all, members see their own ──
    crow::json::wvalue loans = crow::json::wvalue::list();
    int nloan = 0;
    auto ls = db.prepare(std::string(
        "SELECT l.id, i.name, COALESCE(m.display_name,''), l.quantity, l.due_on, l.notes, substr(l.checked_out_at,1,10), "
        "l.member_id, COALESCE(l.from_location_id, 0) "
        "FROM inventory_loans l JOIN inventory_items i ON i.id=l.item_id LEFT JOIN members m ON m.id=l.member_id "
        "WHERE l.returned_at IS NULL") + (manage ? "" : " AND l.member_id=?") +
        " ORDER BY CASE WHEN l.due_on='' THEN 1 ELSE 0 END, l.due_on, l.id");
    if (!manage) ls.bind(1, a.member_id);
    while (ls.step()) {
        auto& l = loans[nloan++];
        std::string due = ls.col_text(4);
        int64_t from = ls.col_int(8);
        l["id"] = ls.col_int(0);
        l["item"] = ls.col_text(1);
        l["who"] = ls.col_text(2);
        l["quantity"] = ls.col_int(3);
        l["due_on"] = due;
        l["notes"] = ls.col_text(5);
        l["since"] = ls.col_text(6);
        l["overdue"] = !due.empty() && due < today;
        l["mine"] = ls.col_int(7) == a.member_id;
        l["can_manage"] = manage;
        if (from > 0 && loc_names.count(from)) l["from"] = loc_names[from];
        l["return_options"] = crow::json::wvalue::list();
        int k = 0;
        for (const auto& [lid, lname] : loc_names) {
            l["return_options"][k]["id"] = lid; l["return_options"][k]["name"] = lname;
            l["return_options"][k]["selected"] = lid == from; ++k;
        }
    }
    ctx["loans"] = std::move(loans);
    ctx["has_loans"] = nloan > 0;

    if (manage) {
        crow::json::wvalue members = crow::json::wvalue::list();
        int nm = 0;
        for (const auto& [pid, pname] : people) { members[nm]["id"] = pid; members[nm]["name"] = pname; ++nm; }
        ctx["members"] = std::move(members);
    }
    return crow::mustache::load("inventory/_content.html").render(ctx).dump();
}

crow::response fragment(const crow::request& req, LugApp& app, SqliteDatabase& db, int code,
                        const std::string& flash) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.write(render(req, app, db, flash, code >= 400));
    return res;
}

// ── Pack lists: what to bring to an event ──
std::string where_kept(SqliteDatabase& db, int64_t item) {
    auto st = db.prepare("SELECT k.quantity, s.name FROM inventory_stock k JOIN storage_locations s ON s.id=k.location_id "
                         "WHERE k.item_id=? ORDER BY k.quantity DESC");
    st.bind(1, item);
    std::string out;
    while (st.step()) out += (out.empty() ? "" : ", ") + std::to_string(st.col_int(0)) + " at " + st.col_text(1);
    return out;
}

crow::mustache::context pack_ctx(SqliteDatabase& db, const LugEvent& ev, bool manage, const std::string& flash) {
    crow::mustache::context ctx;
    ctx["event_id"] = ev.id;
    ctx["event_title"] = ev.title;
    ctx["event_date"] = ev.start_time.substr(0, 10);
    ctx["event_location"] = ev.location;
    ctx["can_manage"] = manage;
    if (!flash.empty()) ctx["flash"] = flash;
    crow::json::wvalue lines = crow::json::wvalue::list();
    int n = 0, packed = 0;
    auto st = db.prepare("SELECT p.item_id, i.name, p.quantity, p.packed, p.note, i.quantity, i.condition, i.condition_note "
                         "FROM inventory_pack p JOIN inventory_items i ON i.id=p.item_id WHERE p.event_id=? "
                         "ORDER BY p.packed, i.category COLLATE NOCASE, i.name COLLATE NOCASE");
    st.bind(1, ev.id);
    while (st.step()) {
        auto& l = lines[n++];
        int64_t item = st.col_int(0);
        l["item_id"] = item; l["event_id"] = ev.id;
        l["name"] = st.col_text(1); l["quantity"] = st.col_int(2);
        bool done = st.col_int(3) != 0;
        l["packed"] = done; packed += done;
        if (!st.col_text(4).empty()) l["note"] = st.col_text(4);
        l["short"] = st.col_int(2) > st.col_int(5);
        l["owned"] = st.col_int(5);
        std::string where = where_kept(db, item);
        if (!where.empty()) l["where"] = where;
        if (st.col_text(6) != "good") {
            l["condition"] = condition_label(st.col_text(6)) +
                             (st.col_text(7).empty() ? "" : " - " + st.col_text(7));
        }
        l["can_manage"] = manage;
    }
    ctx["lines"] = std::move(lines);
    ctx["has_lines"] = n > 0;
    ctx["packed_count"] = packed;
    ctx["line_count"] = n;
    ctx["all_packed"] = n > 0 && packed == n;
    if (manage) {
        crow::json::wvalue opts = crow::json::wvalue::list();
        auto os = db.prepare("SELECT id, name, quantity FROM inventory_items WHERE archived=0 ORDER BY name COLLATE NOCASE");
        int k = 0;
        while (os.step()) {
            opts[k]["id"] = os.col_int(0);
            opts[k]["label"] = os.col_text(1) + " (" + std::to_string(os.col_int(2)) + " owned)";
            ++k;
        }
        ctx["item_options"] = std::move(opts);
        ctx["has_item_options"] = k > 0;
    }
    return ctx;
}

crow::response pack_panel(const crow::request& req, LugApp& app, SqliteDatabase& db, const LugEvent& ev,
                          ChapterMemberRepository& cm, int code = 200, const std::string& flash = "") {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.write(crow::mustache::load("inventory/_pack.html").render(
        pack_ctx(db, ev, can_manage_event(req, app, ev, cm), flash)).dump());
    return res;
}

void register_pack_routes(LugApp& app, SqliteDatabase& db, AuditService& audit, EventService& events,
                          ChapterMemberRepository& chapter_members) {
    // GET /events/<id>/pack - the event's pack list panel (members can see it)
    CROW_ROUTE(app, "/events/<int>/pack")([&app, &db, &events, &chapter_members](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        return pack_panel(req, app, db, *ev, chapter_members);
    });

    // GET /events/<id>/pack/print - printable checklist
    CROW_ROUTE(app, "/events/<int>/pack/print")([&app, &db, &events](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        auto ctx = pack_ctx(db, *ev, false, "");
        ctx["asset_v"] = asset_version();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(crow::mustache::load("inventory/pack_print.html").render(ctx).dump());
        return res;
    });

    // POST /events/<id>/pack - add an item (or change its quantity)
    CROW_ROUTE(app, "/events/<int>/pack").methods("POST"_method)(
        [&app, &db, &audit, &events, &chapter_members](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        Form f(req);
        int64_t item = f.num("item_id"), qty = f.num("quantity", 1);
        std::string name;
        {
            auto st = db.prepare("SELECT name FROM inventory_items WHERE id=? AND archived=0");
            st.bind(1, item);
            if (!st.step()) return pack_panel(req, app, db, *ev, chapter_members, 404, "That item doesn't exist.");
            name = st.col_text(0);
        }
        if (qty < 1 || qty > 100000) return pack_panel(req, app, db, *ev, chapter_members, 400, "How many should we bring?");
        {
            auto up = db.prepare("INSERT INTO inventory_pack (event_id, item_id, quantity, note) VALUES (?,?,?,?) "
                                 "ON CONFLICT(event_id, item_id) DO UPDATE SET quantity=excluded.quantity, note=excluded.note");
            up.bind(1, ev->id); up.bind(2, item); up.bind(3, qty); up.bind(4, f.get("note", 200));
            up.step();
        }
        audit.log(req, app, "event.pack_add", "event", ev->id, ev->title, std::to_string(qty) + " x " + name);
        return pack_panel(req, app, db, *ev, chapter_members, 200, "Added " + name + ".");
    });

    // POST /events/<id>/pack/<item>/toggle - packed / not packed
    CROW_ROUTE(app, "/events/<int>/pack/<int>/toggle").methods("POST"_method)(
        [&app, &db, &events, &chapter_members](const crow::request& req, int id, int item) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto up = db.prepare("UPDATE inventory_pack SET packed = 1 - packed WHERE event_id=? AND item_id=?");
        up.bind(1, ev->id); up.bind(2, static_cast<int64_t>(item));
        up.step();
        live::changed_by(req, "event", ev->id);
        return pack_panel(req, app, db, *ev, chapter_members);
    });

    // POST /events/<id>/pack/<item>/remove
    CROW_ROUTE(app, "/events/<int>/pack/<int>/remove").methods("POST"_method)(
        [&app, &db, &audit, &events, &chapter_members](const crow::request& req, int id, int item) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto ev = events.get(id);
        if (!ev) { res.code = 404; return res; }
        if (!can_manage_event(req, app, *ev, chapter_members)) { res.code = 403; return res; }
        auto del = db.prepare("DELETE FROM inventory_pack WHERE event_id=? AND item_id=?");
        del.bind(1, ev->id); del.bind(2, static_cast<int64_t>(item));
        del.step();
        audit.log(req, app, "event.pack_remove", "event", ev->id, ev->title, "Item #" + std::to_string(item));
        return pack_panel(req, app, db, *ev, chapter_members);
    });
}

} // namespace

void register_inventory_routes(LugApp& app, SqliteDatabase& db, AuditService& audit,
                               std::shared_ptr<PhotoStore> photos, EventService& events,
                               ChapterMemberRepository& chapter_members) {

    // POST /inventory/<id>/photo - add or replace an item's photo (multipart "photo")
    CROW_ROUTE(app, "/inventory/<int>/photo").methods("POST"_method)([&app, &db, &audit, photos](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        std::string old, name;
        {
            auto st = db.prepare("SELECT photo_file, name FROM inventory_items WHERE id=? AND archived=0");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
            old = st.col_text(0); name = st.col_text(1);
        }
        crow::multipart::message msg(req);
        std::string bytes;
        auto it = msg.part_map.find("photo");
        if (it != msg.part_map.end()) bytes = it->second.body;
        std::string err, file = photos->save(bytes, err);
        if (file.empty()) return fragment(req, app, db, 400, err);
        {
            auto up = db.prepare("UPDATE inventory_items SET photo_file=? WHERE id=?");
            up.bind(1, file); up.bind(2, static_cast<int64_t>(id));
            up.step();
        }
        if (!old.empty()) photos->remove(old);
        audit.log(req, app, "inventory.photo", "inventory", id, name, "Photo added");
        return fragment(req, app, db, 200, "Photo added to " + name + ".");
    });

    CROW_ROUTE(app, "/inventory/<int>/photo/delete").methods("POST"_method)([&app, &db, &audit, photos](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        std::string old, name;
        {
            auto st = db.prepare("SELECT photo_file, name FROM inventory_items WHERE id=? AND archived=0");
            st.bind(1, static_cast<int64_t>(id));
            if (!st.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
            old = st.col_text(0); name = st.col_text(1);
        }
        {
            auto up = db.prepare("UPDATE inventory_items SET photo_file='' WHERE id=?");
            up.bind(1, static_cast<int64_t>(id));
            up.step();
        }
        if (!old.empty()) photos->remove(old);
        audit.log(req, app, "inventory.photo", "inventory", id, name, "Photo removed");
        return fragment(req, app, db, 200, "Photo removed.");
    });

    register_pack_routes(app, db, audit, events, chapter_members);

    CROW_ROUTE(app, "/inventory")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        std::string body = render(req, app, db);
        return html_page(req, app, body, "Inventory", "active_inventory");
    });

    // CSV: one row per item and place (location, member on loan, not placed)
    CROW_ROUTE(app, "/inventory.csv")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        std::string out = "Item,Category,Owner,Total,Where,Quantity there,Looked after by / borrower,Due\n";
        auto st = db.prepare(
            "SELECT i.name, i.category, CASE WHEN i.owner_member_id IS NOT NULL THEN COALESCE((SELECT display_name FROM members WHERE id=i.owner_member_id),'') "
            "  WHEN i.owner_name <> '' THEN i.owner_name || ' (left)' ELSE 'LUG' END, i.quantity, s.name, k.quantity, COALESCE(m.display_name,''), '' "
            "FROM inventory_items i JOIN inventory_stock k ON k.item_id=i.id JOIN storage_locations s ON s.id=k.location_id "
            "LEFT JOIN members m ON m.id=s.keeper_id WHERE i.archived=0 "
            "UNION ALL SELECT i.name, i.category, CASE WHEN i.owner_member_id IS NOT NULL THEN COALESCE((SELECT display_name FROM members WHERE id=i.owner_member_id),'') "
            "  WHEN i.owner_name <> '' THEN i.owner_name || ' (left)' ELSE 'LUG' END, i.quantity, 'On loan', l.quantity, COALESCE(m.display_name,''), l.due_on "
            "FROM inventory_items i JOIN inventory_loans l ON l.item_id=i.id AND l.returned_at IS NULL "
            "LEFT JOIN members m ON m.id=l.member_id WHERE i.archived=0 "
            "ORDER BY 1 COLLATE NOCASE, 5");
        while (st.step()) {
            for (int c = 0; c < 8; ++c) out += (c ? "," : "") + csv_field(st.col_text(c));
            out += "\n";
        }
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"inventory.csv\"");
        res.write(out);
        return res;
    });

    // ── Locations ──
    CROW_ROUTE(app, "/inventory/locations").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        std::string name = f.get("name", 100), kind = f.get("kind", 20);
        int64_t keeper = f.num("keeper_id"), owner = f.num("owner_id");
        std::string owner_name;
        if (name.empty() || !valid_kind(kind)) return fragment(req, app, db, 400, "Give the location a name and a type.");
        if ((keeper > 0 && query_int(db, "SELECT COUNT(*) FROM members WHERE id=?", keeper) == 0) || !read_owner(db, owner, owner_name))
            return fragment(req, app, db, 404, "That member doesn't exist.");
        auto ins = db.prepare("INSERT INTO storage_locations (name, kind, address, notes, keeper_id, owner_member_id, owner_name) "
                              "VALUES (?,?,?,?,?,?,?) RETURNING id");
        ins.bind(1, name); ins.bind(2, kind); ins.bind(3, f.get("address", 200)); ins.bind(4, f.get("notes", 500));
        if (keeper > 0) ins.bind(5, keeper); else ins.bind_null(5);
        if (owner > 0) ins.bind(6, owner); else ins.bind_null(6);
        ins.bind(7, owner_name);
        ins.step();
        int64_t id = ins.col_int(0);
        ins.reset();
        audit.log(req, app, "inventory.location_create", "inventory_location", id, name, kind_label(kind));
        return fragment(req, app, db, 200, "Added " + name + ".");
    });

    CROW_ROUTE(app, "/inventory/locations/<int>").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        std::string name = f.get("name", 100), kind = f.get("kind", 20);
        int64_t keeper = f.num("keeper_id"), owner = f.num("owner_id");
        std::string owner_name;
        if (name.empty() || !valid_kind(kind)) return fragment(req, app, db, 400, "Give the location a name and a type.");
        if ((keeper > 0 && query_int(db, "SELECT COUNT(*) FROM members WHERE id=?", keeper) == 0) || !read_owner(db, owner, owner_name))
            return fragment(req, app, db, 404, "That member doesn't exist.");
        auto up = db.prepare("UPDATE storage_locations SET name=?, kind=?, address=?, notes=?, keeper_id=?, owner_member_id=?, owner_name=? "
                             "WHERE id=? AND archived=0 RETURNING id");
        up.bind(1, name); up.bind(2, kind); up.bind(3, f.get("address", 200)); up.bind(4, f.get("notes", 500));
        if (keeper > 0) up.bind(5, keeper); else up.bind_null(5);
        if (owner > 0) up.bind(6, owner); else up.bind_null(6);
        up.bind(7, owner_name);
        up.bind(8, static_cast<int64_t>(id));
        bool found = up.step();
        up.reset();
        if (!found) return fragment(req, app, db, 404, "That location doesn't exist.");
        audit.log(req, app, "inventory.location_update", "inventory_location", id, name,
                  (keeper > 0 ? "Looked after by member #" + std::to_string(keeper) : "No keeper") +
                  (owner > 0 ? ", owned by " + owner_name : ", owned by the LUG"));
        return fragment(req, app, db, 200, "Saved " + name + ".");
    });

    CROW_ROUTE(app, "/inventory/locations/<int>/archive").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        if (query_int(db, "SELECT COUNT(*) FROM inventory_stock k JOIN inventory_items i ON i.id=k.item_id "
                       "WHERE k.location_id=? AND i.archived=0", id) > 0)
            return fragment(req, app, db, 409, "Move everything out of that location first.");
        auto up = db.prepare("UPDATE storage_locations SET archived=1 WHERE id=? AND archived=0 RETURNING name");
        up.bind(1, static_cast<int64_t>(id));
        if (!up.step()) return fragment(req, app, db, 404, "That location doesn't exist.");
        std::string name = up.col_text(0);
        up.reset();
        audit.log(req, app, "inventory.location_archive", "inventory_location", id, name, "Removed");
        return fragment(req, app, db, 200, "Removed " + name + ".");
    });

    // ── Items ──
    // POST /inventory - add an item (optionally all at one location)
    CROW_ROUTE(app, "/inventory").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        std::string name = f.get("name", 150);
        int64_t qty = f.num("quantity", 1), loc = f.num("location_id"), owner = f.num("owner_id");
        std::string owner_name;
        if (name.empty() || qty < 1 || qty > 100000) return fragment(req, app, db, 400, "Give the item a name and a quantity of at least 1.");
        if (loc > 0 && !location_exists(db, loc)) return fragment(req, app, db, 404, "That location doesn't exist.");
        if (!read_owner(db, owner, owner_name)) return fragment(req, app, db, 404, "That member doesn't exist.");
        int64_t id = 0;
        {
            Transaction tx(db);
            {
                auto ins = db.prepare("INSERT INTO inventory_items (name, category, quantity, notes, owner_member_id, owner_name) "
                                      "VALUES (?,?,?,?,?,?) RETURNING id");
                ins.bind(1, name); ins.bind(2, f.get("category", 60)); ins.bind(3, qty); ins.bind(4, f.get("notes", 1000));
                if (owner > 0) ins.bind(5, owner); else ins.bind_null(5);
                ins.bind(6, owner_name);
                ins.step();
                id = ins.col_int(0);
            }
            adjust_stock(db, id, loc, qty);
            tx.commit();
        }
        audit.log(req, app, "inventory.create", "inventory", id, name,
                  "Quantity " + std::to_string(qty) + (loc > 0 ? " at " + location_name(db, loc) : "") +
                  (owner > 0 ? ", owned by " + owner_name : ""));
        return fragment(req, app, db, 200, "Added " + name + ".");
    });

    // POST /inventory/<id> - edit name/category/total/notes
    CROW_ROUTE(app, "/inventory/<int>").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        std::string name = f.get("name", 150);
        int64_t qty = f.num("quantity", 1);
        if (name.empty() || qty < 1 || qty > 100000) return fragment(req, app, db, 400, "Give the item a name and a quantity of at least 1.");
        if (qty < out_count(db, id) + placed_count(db, id))
            return fragment(req, app, db, 400, "More of " + name + " are at locations or on loan than that - move or return them first.");
        int64_t owner = f.num("owner_id");
        std::string owner_name;
        if (!read_owner(db, owner, owner_name)) return fragment(req, app, db, 404, "That member doesn't exist.");
        std::string cond = f.get("condition", 20);
        bool cond_ok = false;
        for (const auto& c : CONDITIONS) cond_ok |= cond == c[0];
        if (!cond_ok) cond = "good";
        auto up = db.prepare("UPDATE inventory_items SET name=?, category=?, quantity=?, notes=?, condition=?, condition_note=?, "
                             "owner_member_id=?, owner_name=? WHERE id=? AND archived=0 RETURNING id");
        up.bind(1, name); up.bind(2, f.get("category", 60)); up.bind(3, qty);
        up.bind(4, f.get("notes", 1000)); up.bind(5, cond); up.bind(6, f.get("condition_note", 300));
        if (owner > 0) up.bind(7, owner); else up.bind_null(7);
        up.bind(8, owner_name);
        up.bind(9, static_cast<int64_t>(id));
        bool found = up.step();
        up.reset();
        if (!found) return fragment(req, app, db, 404, "That item doesn't exist.");
        audit.log(req, app, "inventory.update", "inventory", id, name,
                  "Quantity " + std::to_string(qty) + ", " + condition_label(cond) +
                  (owner > 0 ? ", owned by " + owner_name : ", owned by the LUG"));
        return fragment(req, app, db, 200, "Saved " + name + ".");
    });

    // POST /inventory/<id>/move - move some from one place to another (0 = not placed)
    CROW_ROUTE(app, "/inventory/<int>/move").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        int64_t from = f.num("from_location_id"), to = f.num("to_location_id"), qty = f.num("quantity", 0);
        int64_t item = static_cast<int64_t>(id);
        if (query_int(db, "SELECT COUNT(*) FROM inventory_items WHERE id=? AND archived=0", item) == 0)
            return fragment(req, app, db, 404, "That item doesn't exist.");
        if (qty < 1 || from == to) return fragment(req, app, db, 400, "Pick how many to move and where to.");
        if ((from > 0 && !location_exists(db, from)) || (to > 0 && !location_exists(db, to)))
            return fragment(req, app, db, 404, "That location doesn't exist.");
        std::string name;
        {
            Transaction tx(db);
            int64_t have = from > 0 ? stock_at(db, item, from) : unplaced(db, item);
            if (qty > have)
                return fragment(req, app, db, 409, "Only " + std::to_string(have) + " there to move.");
            adjust_stock(db, item, from, -qty);
            adjust_stock(db, item, to, qty);
            tx.commit();
        }
        {
            auto st = db.prepare("SELECT name FROM inventory_items WHERE id=?");
            st.bind(1, item);
            if (st.step()) name = st.col_text(0);
        }
        std::string what = std::to_string(qty) + " from " + location_name(db, from) + " to " + location_name(db, to);
        audit.log(req, app, "inventory.move", "inventory", item, name, what);
        return fragment(req, app, db, 200, "Moved " + std::to_string(qty) + " × " + name + " to " + location_name(db, to) + ".");
    });

    // POST /inventory/<id>/archive - retire an item (history is kept)
    CROW_ROUTE(app, "/inventory/<int>/archive").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        if (out_count(db, id) > 0) return fragment(req, app, db, 409, "Some of that item are still checked out.");
        auto up = db.prepare("UPDATE inventory_items SET archived=1 WHERE id=? RETURNING name");
        up.bind(1, static_cast<int64_t>(id));
        if (!up.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
        std::string name = up.col_text(0);
        up.reset();
        audit.log(req, app, "inventory.archive", "inventory", id, name, "Removed from inventory");
        return fragment(req, app, db, 200, "Removed " + name + ".");
    });

    // ── Loans ──
    // POST /inventory/checkout - lend an item to a member, taken from a location (or from what isn't placed)
    CROW_ROUTE(app, "/inventory/checkout").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        int64_t item = f.num("item_id"), member = f.num("member_id"), qty = f.num("quantity", 1);
        int64_t from = f.num("from_location_id");
        std::string due = f.get("due_on", 10);
        if (item <= 0 || member <= 0 || qty < 1 || !valid_ymd(due))
            return fragment(req, app, db, 400, "Pick an item, a member and a quantity.");
        std::string name, who;
        {
            auto st = db.prepare("SELECT name FROM inventory_items WHERE id=? AND archived=0");
            st.bind(1, item);
            if (!st.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
            name = st.col_text(0);
        }
        {
            auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
            st.bind(1, member);
            if (!st.step()) return fragment(req, app, db, 404, "That member doesn't exist.");
            who = st.col_text(0);
        }
        if (from > 0 && !location_exists(db, from)) return fragment(req, app, db, 404, "That location doesn't exist.");
        {
            // Check and record under one lock so two check-outs can't both take the last one.
            Transaction tx(db);
            if (from == 0) {
                // No location picked: take what isn't placed, else from wherever has the most.
                if (unplaced(db, item) < qty) {
                    auto st = db.prepare("SELECT location_id FROM inventory_stock WHERE item_id=? AND quantity>=? "
                                         "ORDER BY quantity DESC LIMIT 1");
                    st.bind(1, item); st.bind(2, qty);
                    if (st.step()) from = st.col_int(0);
                }
            }
            int64_t have = from > 0 ? stock_at(db, item, from) : unplaced(db, item);
            if (qty > have)
                return fragment(req, app, db, 409, "Only " + std::to_string(have) + " of " + name + " available " +
                                (from > 0 ? "at " + location_name(db, from) : "there") + ".");
            adjust_stock(db, item, from, -qty);
            {
                auto ins = db.prepare("INSERT INTO inventory_loans (item_id, member_id, quantity, due_on, notes, checked_out_by, from_location_id) "
                                      "VALUES (?,?,?,?,?,?,?)");
                ins.bind(1, item); ins.bind(2, member); ins.bind(3, qty); ins.bind(4, due);
                ins.bind(5, f.get("notes", 500)); ins.bind(6, app.get_context<AuthMiddleware>(req).auth.member_id);
                if (from > 0) ins.bind(7, from); else ins.bind_null(7);
                ins.step();
            }
            tx.commit();
        }
        audit.log(req, app, "inventory.checkout", "inventory", item, name,
                  std::to_string(qty) + " to " + who + (from > 0 ? " from " + location_name(db, from) : "") +
                  (due.empty() ? "" : ", due " + due));
        return fragment(req, app, db, 200, "Checked out " + std::to_string(qty) + " × " + name + " to " + who + ".");
    });

    // POST /inventory/loans/<id>/return - back to where it came from, or to the chosen location
    CROW_ROUTE(app, "/inventory/loans/<int>/return").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "perm:inventory.manage")) return res;
        Form f(req);
        int64_t to = f.num("to_location_id", -1);
        int64_t item = 0, qty = 0, from = 0;
        std::string name, who;
        {
            Transaction tx(db);
            {
                auto up = db.prepare(
                    "UPDATE inventory_loans SET returned_at=datetime('now') WHERE id=? AND returned_at IS NULL "
                    "RETURNING item_id, quantity, COALESCE(from_location_id,0), (SELECT name FROM inventory_items WHERE id=item_id), "
                    "(SELECT display_name FROM members WHERE id=member_id)");
                up.bind(1, static_cast<int64_t>(id));
                if (!up.step()) return fragment(req, app, db, 404, "That loan isn't open.");
                item = up.col_int(0); qty = up.col_int(1); from = up.col_int(2);
                name = up.col_text(3); who = up.col_text(4);
            }
            if (to < 0) to = location_exists(db, from) ? from : 0;           // default: where it came from
            if (to > 0 && !location_exists(db, to)) return fragment(req, app, db, 404, "That location doesn't exist.");
            adjust_stock(db, item, to, qty);
            tx.commit();
        }
        audit.log(req, app, "inventory.return", "inventory", item, name,
                  std::to_string(qty) + " returned by " + who + " to " + location_name(db, to));
        return fragment(req, app, db, 200, "Marked " + name + " as returned to " + location_name(db, to) + ".");
    });
}
