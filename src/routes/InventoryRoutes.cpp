#include "routes/InventoryRoutes.hpp"
#include "services/AttendanceService.hpp"
#include "utils/Csv.hpp"
#include <crow/mustache.h>
#include <regex>

namespace {

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

bool valid_ymd(const std::string& s) {
    static const std::regex ymd(R"(\d{4}-\d{2}-\d{2})");
    return s.empty() || std::regex_match(s, ymd);
}

int64_t out_count(SqliteDatabase& db, int64_t item_id) {
    auto st = db.prepare("SELECT COALESCE(SUM(quantity),0) FROM inventory_loans WHERE item_id=? AND returned_at IS NULL");
    st.bind(1, item_id);
    return st.step() ? st.col_int(0) : 0;
}

std::string render(const crow::request& req, LugApp& app, SqliteDatabase& db, const std::string& flash = "",
                   bool error = false) {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    bool manage = a.is_chapter_lead();
    std::string today = AttendanceService::today_ymd();
    crow::mustache::context ctx;
    ctx["flash"] = flash;
    ctx["flash_error"] = error;
    ctx["can_manage"] = manage;
    ctx["today"] = today;

    crow::json::wvalue items = crow::json::wvalue::list();
    crow::json::wvalue options = crow::json::wvalue::list();
    int i = 0, n_opt = 0;
    auto st = db.prepare(
        "SELECT i.id, i.name, i.category, i.quantity, i.location, i.notes, "
        "COALESCE((SELECT SUM(l.quantity) FROM inventory_loans l WHERE l.item_id=i.id AND l.returned_at IS NULL),0) "
        "FROM inventory_items i WHERE i.archived=0 ORDER BY i.category COLLATE NOCASE, i.name COLLATE NOCASE");
    while (st.step()) {
        int64_t qty = st.col_int(3), out = st.col_int(6);
        auto& it = items[i++];
        it["id"] = st.col_int(0);
        it["name"] = st.col_text(1);
        it["category"] = st.col_text(2);
        it["quantity"] = qty;
        it["location"] = st.col_text(4);
        it["notes"] = st.col_text(5);
        it["available"] = qty - out;
        it["out"] = out;
        it["all_out"] = qty - out <= 0;
        it["can_manage"] = manage;
        if (qty - out > 0) {
            options[n_opt]["id"] = st.col_int(0);
            options[n_opt]["label"] = st.col_text(1) + " (" + std::to_string(qty - out) + " available)";
            ++n_opt;
        }
    }
    ctx["items"] = std::move(items);
    ctx["has_items"] = i > 0;
    ctx["item_options"] = std::move(options);
    ctx["has_item_options"] = n_opt > 0;

    // Open loans: managers see all, members see their own.
    crow::json::wvalue loans = crow::json::wvalue::list();
    int nl = 0;
    auto ls = db.prepare(std::string(
        "SELECT l.id, i.name, COALESCE(m.display_name,''), l.quantity, l.due_on, l.notes, substr(l.checked_out_at,1,10), l.member_id "
        "FROM inventory_loans l JOIN inventory_items i ON i.id=l.item_id LEFT JOIN members m ON m.id=l.member_id "
        "WHERE l.returned_at IS NULL") + (manage ? "" : " AND l.member_id=?") +
        " ORDER BY CASE WHEN l.due_on='' THEN 1 ELSE 0 END, l.due_on, l.id");
    if (!manage) ls.bind(1, a.member_id);
    while (ls.step()) {
        auto& l = loans[nl++];
        std::string due = ls.col_text(4);
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
    }
    ctx["loans"] = std::move(loans);
    ctx["has_loans"] = nl > 0;

    if (manage) {
        crow::json::wvalue members = crow::json::wvalue::list();
        auto ms = db.prepare("SELECT id, display_name FROM members ORDER BY display_name COLLATE NOCASE");
        int nm = 0;
        while (ms.step()) {
            members[nm]["id"] = ms.col_int(0);
            members[nm]["name"] = ms.col_text(1);
            ++nm;
        }
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

} // namespace

void register_inventory_routes(LugApp& app, SqliteDatabase& db, AuditService& audit) {

    CROW_ROUTE(app, "/inventory")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        std::string body = render(req, app, db);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(req.get_header_value("HX-Request") == "true" ? body
                  : render_in_layout(req, app, body, "Inventory", "active_inventory"));
        return res;
    });

    // CSV of all items and who has them (managers)
    CROW_ROUTE(app, "/inventory.csv")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        std::string out = "Item,Category,Quantity,Location,Checked out to,Qty out,Due\n";
        auto st = db.prepare(
            "SELECT i.name, i.category, i.quantity, i.location, COALESCE(m.display_name,''), COALESCE(l.quantity,''), COALESCE(l.due_on,'') "
            "FROM inventory_items i LEFT JOIN inventory_loans l ON l.item_id=i.id AND l.returned_at IS NULL "
            "LEFT JOIN members m ON m.id=l.member_id WHERE i.archived=0 ORDER BY i.name COLLATE NOCASE");
        while (st.step()) {
            for (int c = 0; c < 7; ++c) out += (c ? "," : "") + csv_field(st.col_text(c));
            out += "\n";
        }
        res.add_header("Content-Type", "text/csv; charset=utf-8");
        res.add_header("Content-Disposition", "attachment; filename=\"inventory.csv\"");
        res.write(out);
        return res;
    });

    // POST /inventory - add an item
    CROW_ROUTE(app, "/inventory").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        Form f(req);
        std::string name = f.get("name", 150);
        int64_t qty = f.num("quantity", 1);
        if (name.empty() || qty < 1 || qty > 100000) return fragment(req, app, db, 400, "Give the item a name and a quantity of at least 1.");
        auto ins = db.prepare("INSERT INTO inventory_items (name, category, quantity, location, notes) VALUES (?,?,?,?,?) RETURNING id");
        ins.bind(1, name); ins.bind(2, f.get("category", 60)); ins.bind(3, qty);
        ins.bind(4, f.get("location", 150)); ins.bind(5, f.get("notes", 1000));
        ins.step();
        int64_t id = ins.col_int(0);
        ins.reset();
        audit.log(req, app, "inventory.create", "inventory", id, name, "Quantity " + std::to_string(qty));
        return fragment(req, app, db, 200, "Added " + name + ".");
    });

    // POST /inventory/<id> - edit an item
    CROW_ROUTE(app, "/inventory/<int>").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        Form f(req);
        std::string name = f.get("name", 150);
        int64_t qty = f.num("quantity", 1);
        if (name.empty() || qty < 1 || qty > 100000) return fragment(req, app, db, 400, "Give the item a name and a quantity of at least 1.");
        if (qty < out_count(db, id)) return fragment(req, app, db, 400, "More of " + name + " are checked out than that - return them first.");
        auto up = db.prepare("UPDATE inventory_items SET name=?, category=?, quantity=?, location=?, notes=? WHERE id=? AND archived=0 RETURNING id");
        up.bind(1, name); up.bind(2, f.get("category", 60)); up.bind(3, qty);
        up.bind(4, f.get("location", 150)); up.bind(5, f.get("notes", 1000)); up.bind(6, (int64_t)id);
        bool found = up.step();
        up.reset();
        if (!found) return fragment(req, app, db, 404, "That item doesn't exist.");
        audit.log(req, app, "inventory.update", "inventory", id, name, "Quantity " + std::to_string(qty));
        return fragment(req, app, db, 200, "Saved " + name + ".");
    });

    // POST /inventory/<id>/archive - retire an item (history is kept)
    CROW_ROUTE(app, "/inventory/<int>/archive").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        if (out_count(db, id) > 0) return fragment(req, app, db, 409, "Some of that item are still checked out.");
        auto up = db.prepare("UPDATE inventory_items SET archived=1 WHERE id=? RETURNING name");
        up.bind(1, (int64_t)id);
        if (!up.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
        std::string name = up.col_text(0);
        up.reset();
        audit.log(req, app, "inventory.archive", "inventory", id, name, "Removed from inventory");
        return fragment(req, app, db, 200, "Removed " + name + ".");
    });

    // POST /inventory/checkout - lend an item to a member
    CROW_ROUTE(app, "/inventory/checkout").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        Form f(req);
        int64_t item = f.num("item_id"), member = f.num("member_id"), qty = f.num("quantity", 1);
        std::string due = f.get("due_on", 10);
        if (item <= 0 || member <= 0 || qty < 1 || !valid_ymd(due))
            return fragment(req, app, db, 400, "Pick an item, a member and a quantity.");
        std::string name, who;
        int64_t total = 0;
        {
            auto st = db.prepare("SELECT name, quantity FROM inventory_items WHERE id=? AND archived=0");
            st.bind(1, item);
            if (!st.step()) return fragment(req, app, db, 404, "That item doesn't exist.");
            name = st.col_text(0); total = st.col_int(1);
        }
        {
            auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
            st.bind(1, member);
            if (!st.step()) return fragment(req, app, db, 404, "That member doesn't exist.");
            who = st.col_text(0);
        }
        {
            // Check and insert under one lock so two check-outs can't both take the last one.
            Transaction tx(db);
            if (qty > total - out_count(db, item))
                return fragment(req, app, db, 409, "Only " + std::to_string(total - out_count(db, item)) + " of " + name + " available.");
            {
                auto ins = db.prepare("INSERT INTO inventory_loans (item_id, member_id, quantity, due_on, notes, checked_out_by) VALUES (?,?,?,?,?,?)");
                ins.bind(1, item); ins.bind(2, member); ins.bind(3, qty); ins.bind(4, due);
                ins.bind(5, f.get("notes", 500)); ins.bind(6, app.get_context<AuthMiddleware>(req).auth.member_id);
                ins.step();
            }
            tx.commit();
        }
        audit.log(req, app, "inventory.checkout", "inventory", item, name,
                  std::to_string(qty) + " to " + who + (due.empty() ? "" : ", due " + due));
        return fragment(req, app, db, 200, "Checked out " + std::to_string(qty) + " × " + name + " to " + who + ".");
    });

    // POST /inventory/loans/<id>/return
    CROW_ROUTE(app, "/inventory/loans/<int>/return").methods("POST"_method)([&app, &db, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        auto up = db.prepare(
            "UPDATE inventory_loans SET returned_at=datetime('now') WHERE id=? AND returned_at IS NULL "
            "RETURNING item_id, quantity, (SELECT name FROM inventory_items WHERE id=item_id), "
            "(SELECT display_name FROM members WHERE id=member_id)");
        up.bind(1, (int64_t)id);
        if (!up.step()) return fragment(req, app, db, 404, "That loan isn't open.");
        int64_t item = up.col_int(0);
        std::string detail = std::to_string(up.col_int(1)) + " returned by " + up.col_text(3);
        std::string name = up.col_text(2);
        up.reset();
        audit.log(req, app, "inventory.return", "inventory", item, name, detail);
        return fragment(req, app, db, 200, "Marked " + name + " as returned.");
    });
}
