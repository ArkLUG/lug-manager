#include "routes/accounts/AccountRoutes.hpp"
#include "utils/web/CalendarLinks.hpp"
#include "services/Features.hpp"
#include "auth/AccountSecurity.hpp"
#include "services/Palettes.hpp"
#include "repositories/members/NotificationPrefs.hpp"
#include "services/events/AttendanceService.hpp"
#include <crow/mustache.h>
#include <set>

namespace {

std::string render_notifications(SqliteDatabase& db, int64_t member_id, const std::string& flash = "") {
    auto off = NotificationPrefs(db).optouts(member_id);
    crow::mustache::context ctx;
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    for (const auto& k : NotificationPrefs::kinds()) {
        if (*k.feature && !Features::on(k.feature)) continue;   // switched off
        arr[i]["key"] = k.key; arr[i]["label"] = k.label; arr[i]["help"] = k.help;
        arr[i]["on"] = !off.count(k.key);
        ++i;
    }
    ctx["kinds"] = std::move(arr);
    ctx["flash"] = flash;
    return crow::mustache::load("account/_notifications.html").render(ctx).dump();
}

// Every row of `sql` (bound to member_id) as a JSON object list, minus `skip` columns.
crow::json::wvalue rows_json(SqliteDatabase& db, const std::string& sql, int64_t member_id,
                             const std::set<std::string>& skip = {}) {
    crow::json::wvalue arr = crow::json::wvalue::list();
    auto st = db.prepare(sql);
    st.bind(1, member_id);
    int i = 0;
    while (st.step()) {
        for (int c = 0; c < st.col_count(); ++c) {
            std::string name = st.col_name(c);
            if (skip.count(name)) continue;
            if (st.col_is_null(c)) arr[i][name] = nullptr;
            else if (st.col_is_int(c)) arr[i][name] = st.col_int(c);
            else arr[i][name] = st.col_text(c);
        }
        ++i;
    }
    return arr;
}

} // namespace

void register_account_routes(LugApp& app, SqliteDatabase& db, MemberService& members,
                             std::shared_ptr<PhotoStore> photos, AuditService& audit) {

    // GET /account - the signed-in member's account page
    CROW_ROUTE(app, "/account")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        crow::mustache::context ctx;
        ctx["notifications"] = render_notifications(db, a.member_id);
        ctx["display_name"] = a.display_name;
        {
            AccountSecurity sec(db);
            auto acct = sec.by_id(a.member_id);
            if (acct && !acct->email.empty() && !sec.email_confirmed(a.member_id)) ctx["unconfirmed_email"] = acct->email;
        }
        {
            std::string mine;
            auto st = db.prepare("SELECT palette FROM members WHERE id=?");
            st.bind(1, a.member_id);
            if (st.step()) mine = st.col_text(0);
            auto lug = db.prepare("SELECT value FROM lug_settings WHERE key='default_palette'");
            std::string lug_default = lug.step() ? lug.col_text(0) : "classic";
            const std::string current = palettes::resolve(mine, lug_default);
            crow::json::wvalue opts = crow::json::wvalue::list();
            int i = 0;
            for (const auto& p : palettes::all()) {
                opts[i]["key"] = p.key; opts[i]["name"] = p.name; opts[i]["description"] = p.description;
                opts[i]["selected"] = current == p.key;
                opts[i]["lug_default"] = palettes::resolve("", lug_default) == p.key;
                ++i;
            }
            ctx["palettes"] = std::move(opts);
        }
        // Things this member owns that the LUG keeps or uses (Inventory)
        if (Features::on("inventory")) {
            crow::json::wvalue owned = crow::json::wvalue::list();
            int k = 0;
            auto st = db.prepare(
                "SELECT i.name, i.quantity, "
                "COALESCE((SELECT group_concat(k.quantity || ' at ' || s.name, ', ') FROM inventory_stock k "
                "          JOIN storage_locations s ON s.id=k.location_id WHERE k.item_id=i.id),''), "
                "COALESCE((SELECT group_concat(l.quantity || ' with ' || COALESCE(m.display_name,'?'), ', ') FROM inventory_loans l "
                "          LEFT JOIN members m ON m.id=l.member_id WHERE l.item_id=i.id AND l.returned_at IS NULL),'') "
                "FROM inventory_items i WHERE i.owner_member_id=? AND i.archived=0 ORDER BY i.name COLLATE NOCASE");
            st.bind(1, a.member_id);
            while (st.step()) {
                owned[k]["name"] = st.col_text(0); owned[k]["quantity"] = st.col_int(1);
                owned[k]["where"] = st.col_text(2); owned[k]["out"] = st.col_text(3); ++k;
            }
            ctx["owned_items"] = std::move(owned);
            ctx["has_owned_items"] = k > 0;
        }
        ctx["subscribe_html"] = cal_links::subscribe_html("/calendar.ics", "LUG Manager", "account-cal",
                                                          cal_links::public_google_calendar(db));
        std::string body = crow::mustache::load("account/_content.html").render(ctx).dump();
        return html_page(req, app, body, "My Account", "active_account");
    });

    // POST /account/notifications - save the checkbox form (unchecked = off)
    CROW_ROUTE(app, "/account/notifications").methods("POST"_method)([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        int64_t me = app.get_context<AuthMiddleware>(req).auth.member_id;
        auto p = crow::query_string("?" + req.body);
        NotificationPrefs prefs(db);
        for (const auto& k : NotificationPrefs::kinds()) {
            if (*k.feature && !Features::on(k.feature)) continue;   // hidden: leave as is
            prefs.set(me, k.key, p.get(k.key) != nullptr);
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_notifications(db, me, "Saved."));
        return res;
    });

    // POST /account/palette - their colour theme (palette=<key>; anything
    // else = the LUG default). The page has already switched; this saves it.
    CROW_ROUTE(app, "/account/palette").methods("POST"_method)([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto p = crow::query_string("?" + req.body);
        const char* v = p.get("palette");
        std::string key = v && palettes::valid(v) ? v : "";
        auto st = db.prepare("UPDATE members SET palette=? WHERE id=?");
        st.bind(1, key);
        st.bind(2, app.get_context<AuthMiddleware>(req).auth.member_id);
        st.step();
        res.code = 204;
        return res;
    });

    // GET /account/export - everything the app stores about the member, as JSON
    CROW_ROUTE(app, "/account/export")([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        int64_t me = a.member_id;
        crow::json::wvalue out;
        out["exported_at"] = AttendanceService::today_ymd();
        auto profile = rows_json(db, "SELECT * FROM members WHERE id=?", me, {"calendar_token_hash"});
        out["profile"] = std::move(profile);
        out["chapters"] = rows_json(db,
            "SELECT c.name AS chapter, cm.chapter_role, cm.granted_at FROM chapter_members cm "
            "JOIN chapters c ON c.id = cm.chapter_id WHERE cm.member_id=?", me);
        out["attendance"] = rows_json(db,
            "SELECT a.entity_type, a.entity_id, COALESCE(mt.title, ev.title, '') AS title, a.* FROM attendance a "
            "LEFT JOIN meetings mt ON a.entity_type='meeting' AND mt.id=a.entity_id "
            "LEFT JOIN lug_events ev ON a.entity_type='event' AND ev.id=a.entity_id WHERE a.member_id=?", me);
        out["event_day_attendance"] = rows_json(db, "SELECT * FROM event_day_attendance WHERE member_id=?", me);
        out["rsvps"] = rows_json(db,
            "SELECT e.title, r.* FROM event_rsvps r LEFT JOIN lug_events e ON e.id=r.event_id WHERE r.member_id=?", me);
        out["display_requests"] = rows_json(db, "SELECT * FROM event_display_requests WHERE member_id=?", me);
        out["volunteer_shifts"] = rows_json(db,
            "SELECT s.title, s.starts_at, s.ends_at, u.* FROM event_shift_signups u "
            "JOIN event_shifts s ON s.id=u.shift_id WHERE u.member_id=?", me);
        out["inventory_loans"] = rows_json(db,
            "SELECT i.name AS item, l.* FROM inventory_loans l JOIN inventory_items i ON i.id=l.item_id WHERE l.member_id=?", me);
        out["storage_locations_kept"] = rows_json(db, "SELECT name, kind, address FROM storage_locations WHERE keeper_id=? AND archived=0", me);
        out["dues_payments"] = rows_json(db, "SELECT * FROM dues_payments WHERE member_id=?", me);
        out["event_photos"] = rows_json(db, "SELECT * FROM event_photos WHERE member_id=?", me);
        out["challenge_entries"] = rows_json(db, "SELECT * FROM challenge_entries WHERE member_id=?", me);
        out["challenge_votes"] = rows_json(db, "SELECT * FROM challenge_votes WHERE member_id=?", me);
        out["notification_optouts"] = rows_json(db, "SELECT kind, created_at FROM notification_optouts WHERE member_id=?", me);
        out["sessions"] = rows_json(db, "SELECT * FROM sessions WHERE member_id=?", me, {"token", "token_is_hash"});
        out["my_actions"] = rows_json(db, "SELECT * FROM audit_log WHERE actor_id=? ORDER BY id", me);
        audit.log(req, app, "member.data_export", "member", me, a.display_name, "Downloaded own data");
        res.add_header("Content-Type", "application/json");
        res.add_header("Content-Disposition", "attachment; filename=\"my-lug-data.json\"");
        res.add_header("Cache-Control", "no-store");
        res.write(out.dump());
        return res;
    });

    // POST /account/delete - permanently delete the member's own record
    CROW_ROUTE(app, "/account/delete").methods("POST"_method)(
        [&app, &db, &members, photos, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        int64_t me = a.member_id;
        std::string name = a.display_name;
        auto p = crow::query_string("?" + req.body);
        const char* confirm = p.get("confirm");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!confirm || std::string(confirm) != "DELETE") {
            res.code = 400;
            res.write("<p class=\"text-sm text-red-600\">Type DELETE (in capitals) to confirm.</p>");
            return res;
        }
        if (a.is_admin()) {
            auto st = db.prepare("SELECT COUNT(*) FROM members WHERE role='admin' AND id<>?");
            st.bind(1, me);
            if (st.step() && st.col_int(0) == 0) {
                res.code = 409;
                res.write("<p class=\"text-sm text-red-600\">You're the only admin. Make someone else an admin first.</p>");
                return res;
            }
        }
        {
            auto st = db.prepare("SELECT COUNT(*) FROM inventory_loans WHERE member_id=? AND returned_at IS NULL");
            st.bind(1, me);
            if (st.step() && st.col_int(0) > 0) {
                res.code = 409;
                res.write("<p class=\"text-sm text-red-600\">You still have LUG items checked out. Return them first.</p>");
                return res;
            }
        }
        // Uploaded files go with the record.
        std::vector<std::string> files;
        for (const char* sql : {"SELECT file FROM event_photos WHERE member_id=?",
                                "SELECT file FROM challenge_entries WHERE member_id=?"}) {
            auto st = db.prepare(sql);
            st.bind(1, me);
            while (st.step()) files.push_back(st.col_text(0));
        }
        audit.log(req, app, "member.self_delete", "member", me, name, "Deleted own account");
        members.delete_member(me, /*kick_from_discord=*/false);
        for (const auto& f : files) photos->remove(f);
        res.add_header("Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0; SameSite=Lax");
        res.add_header("HX-Redirect", "/login");
        return res;
    });
}
