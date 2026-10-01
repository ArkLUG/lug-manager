#include "routes/MemberMergeRoutes.hpp"
#include "services/MemberMerge.hpp"
#include "utils/HtmlEscape.hpp"
#include <crow/mustache.h>

namespace {

struct Brief { int64_t id = 0; std::string name, discord, email, role, paid_until, created; };

std::optional<Brief> brief(SqliteDatabase& db, int64_t id) {
    auto st = db.prepare("SELECT id, display_name, COALESCE(discord_username,''), COALESCE(email,''), role, "
                         "COALESCE(paid_until,''), substr(created_at,1,10) FROM members WHERE id=?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return Brief{st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_text(5), st.col_text(6)};
}

int64_t num(const crow::query_string& q, const char* k) {
    const char* v = q.get(k);
    try { return v ? std::stoll(v) : 0; } catch (...) { return 0; }
}

crow::response html(int code, const std::string& body) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.write(body);
    return res;
}

crow::response error(int code, const std::string& msg) {
    return html(code, "<p class=\"text-sm text-red-600\">" + html_escape(msg) + "</p>");
}

} // namespace

void register_member_merge_routes(LugApp& app, SqliteDatabase& db, AuditService& audit) {

    // GET /members/merge - modal with the two pickers
    CROW_ROUTE(app, "/members/merge")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::mustache::context ctx;
        crow::json::wvalue members = crow::json::wvalue::list();
        auto st = db.prepare("SELECT id, display_name, COALESCE(discord_username,''), COALESCE(email,'') FROM members "
                             "ORDER BY display_name COLLATE NOCASE");
        int i = 0;
        while (st.step()) {
            std::string extra = !st.col_text(2).empty() ? st.col_text(2) : st.col_text(3);
            members[i]["id"] = st.col_int(0);
            members[i]["label"] = st.col_text(1) + (extra.empty() ? "" : " (" + extra + ")") + " #" + std::to_string(st.col_int(0));
            ++i;
        }
        ctx["members"] = std::move(members);
        return html(200, crow::mustache::load("members/_merge.html").render(ctx).dump());
    });

    // POST /members/merge/preview - what will happen
    CROW_ROUTE(app, "/members/merge/preview").methods("POST"_method)([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto q = crow::query_string("?" + req.body);
        int64_t keep = num(q, "keep_id"), drop = num(q, "drop_id");
        if (keep <= 0 || drop <= 0) return error(400, "Pick both members.");
        if (keep == drop) return error(400, "Pick two different members.");
        auto k = brief(db, keep), d = brief(db, drop);
        if (!k || !d) return error(404, "One of those members no longer exists.");
        crow::mustache::context ctx;
        auto put = [](crow::json::wvalue& o, const Brief& b) {
            o["id"] = b.id; o["name"] = b.name; o["discord"] = b.discord; o["email"] = b.email;
            o["role"] = b.role; o["paid_until"] = b.paid_until; o["created"] = b.created;
        };
        crow::json::wvalue kv, dv;
        put(kv, *k); put(dv, *d);
        ctx["keep"] = std::move(kv);
        ctx["drop"] = std::move(dv);
        crow::json::wvalue counts = crow::json::wvalue::list();
        int i = 0;
        for (const auto& [label, n] : MemberMerge(db).counts(drop)) { counts[i]["label"] = label; counts[i]["n"] = n; ++i; }
        ctx["counts"] = std::move(counts);
        ctx["has_counts"] = i > 0;
        return html(200, crow::mustache::load("members/_merge_preview.html").render(ctx).dump());
    });

    // POST /members/merge - do it
    CROW_ROUTE(app, "/members/merge").methods("POST"_method)([&app, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto q = crow::query_string("?" + req.body);
        int64_t keep = num(q, "keep_id"), drop = num(q, "drop_id");
        auto k = brief(db, keep), d = brief(db, drop);
        if (!k || !d) return error(404, "One of those members no longer exists.");
        // Merging your own record away would end your session mid-request.
        if (drop == app.get_context<AuthMiddleware>(req).auth.member_id)
            return error(409, "You can't merge away your own record. Keep yours, or ask another admin.");
        try {
            MemberMerge(db).merge(keep, drop);
        } catch (const std::exception& e) {
            return error(409, e.what());
        }
        audit.log(req, app, "member.merge", "member", keep, k->name,
                  "Merged " + d->name + " (#" + std::to_string(drop) + ") into this record");
        return html(200, "<p class=\"text-sm text-green-700\">Merged " + html_escape(d->name) + " into " +
                         html_escape(k->name) + ".</p>");
    });
}
