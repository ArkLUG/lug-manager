#include "routes/MemberRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>
#include <sstream>
#include <unordered_set>

// Allowed sort column names (validated against this set)
static const std::unordered_set<std::string> kAllowedSortCols = {
    "display_name", "discord_username", "email", "is_paid", "role", "chapter_name", "fol_status"
};

static crow::mustache::context member_to_ctx(const Member& m) {
    crow::mustache::context ctx;
    ctx["id"]               = m.id;
    ctx["discord_user_id"]  = m.discord_user_id;
    ctx["discord_username"] = m.discord_username;
    ctx["first_name"]       = m.first_name;
    ctx["last_name"]        = m.last_name;
    ctx["display_name"]     = m.display_name;
    ctx["email"]            = m.email;
    ctx["is_paid"]          = m.is_paid;
    ctx["paid_until"]       = m.paid_until;
    ctx["role"]             = m.role;
    ctx["role_admin"]        = m.role == "admin";
    ctx["role_chapter_lead"] = m.role == "chapter_lead";
    ctx["role_member"]       = m.role == "member" || m.role.empty();
    ctx["birthday"]          = m.birthday;
    ctx["fol_status"]        = m.fol_status;
    ctx["fol_kfol"]          = m.fol_status == "kfol";
    ctx["fol_tfol"]          = m.fol_status == "tfol";
    ctx["fol_afol"]          = m.fol_status == "afol" || m.fol_status.empty();
    ctx["pii_public"]        = m.pii_public;
    ctx["phone"]             = m.phone;
    ctx["address_line1"]     = m.address_line1;
    ctx["address_line2"]     = m.address_line2;
    ctx["city"]              = m.city;
    ctx["state"]             = m.state;
    ctx["zip"]               = m.zip;
    ctx["chapter_id"]        = m.chapter_id;
    ctx["chapter_id_str"]    = m.chapter_id > 0 ? std::to_string(m.chapter_id) : "";
    ctx["created_at"]        = m.created_at;
    return ctx;
}

static std::string render_members_page(const crow::request& req,
                                        LugApp& app) {
    auto& auth = app.get_context<AuthMiddleware>(req).auth;
    bool is_admin = auth.is_admin();
    bool can_see_pii = auth.is_chapter_lead(); // admin or chapter_lead
    bool can_see_dues = auth.is_chapter_lead();
    crow::mustache::context ctx;
    ctx["title"]        = "Members";
    ctx["is_admin"]     = is_admin;
    ctx["can_see_pii"]  = can_see_pii;
    ctx["can_see_dues"] = can_see_dues;

    bool is_htmx = req.get_header_value("HX-Request") == "true";
    if (is_htmx) {
        auto tmpl = crow::mustache::load("members/_content.html");
        return tmpl.render(ctx).dump();
    }

    auto content_tmpl = crow::mustache::load("members/_content.html");
    std::string content = content_tmpl.render(ctx).dump();
    crow::mustache::context layout_ctx;
    layout_ctx["content"]        = content;
    layout_ctx["page_title"]     = "Members";
    layout_ctx["active_members"] = true;
    layout_ctx["is_admin"]       = is_admin;
        set_layout_auth(req, app, layout_ctx);
    auto layout = crow::mustache::load("layout.html");
    return layout.render(layout_ctx).dump();
}

void register_member_routes(LugApp& app, MemberService& members) {

    // GET /members - members page (table shell; data loaded via AJAX)
    CROW_ROUTE(app, "/members")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_members_page(req, app));
        return res;
    });

    // POST /api/members/datatable - DataTables server-side AJAX endpoint
    CROW_ROUTE(app, "/api/members/datatable").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto params = crow::query_string("?" + req.body);
        auto get_p  = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        DatatableParams p;
        try { p.draw   = std::stoi(get_p("draw")); }   catch (...) {}
        try { p.start  = std::stoi(get_p("start")); }  catch (...) {}
        try { p.length = std::stoi(get_p("length")); } catch (...) {}
        if (p.length < 1)  p.length = 25;
        if (p.length > 200) p.length = 200;

        p.search = get_p("search");

        std::string col_name = get_p("order_col_name");
        if (!col_name.empty() && kAllowedSortCols.count(col_name))
            p.sort_col = col_name;
        p.sort_dir = get_p("order_dir");

        auto result = members.datatable(p);

        // Access checks: admin/chapter_lead see PII + dues; regular members see
        // limited info — PII only for opted-in members, no dues, no discord username
        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        bool role_can_see_pii = auth.is_chapter_lead();
        bool can_see_dues = auth.is_chapter_lead(); // admin or chapter_lead

        // Build JSON manually so "data" is always a proper [] array.
        // Crow's default wvalue serialises as null when no indices are set.
        auto esc_json = [](const std::string& s) -> std::string {
            std::string r; r.reserve(s.size());
            for (unsigned char c : s) {
                if      (c == '"')  r += "\\\"";
                else if (c == '\\') r += "\\\\";
                else if (c == '\n') r += "\\n";
                else if (c == '\r') r += "\\r";
                else if (c == '\t') r += "\\t";
                else if (c < 0x20) { char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); r += buf; }
                else r += static_cast<char>(c);
            }
            return r;
        };

        std::ostringstream json;
        json << "{\"draw\":"            << result.draw
             << ",\"recordsTotal\":"    << result.records_total
             << ",\"recordsFiltered\":" << result.records_filtered
             << ",\"data\":[";
        for (size_t i = 0; i < result.data.size(); ++i) {
            if (i > 0) json << ",";
            const auto& m = result.data[i];
            bool show_pii = role_can_see_pii || m.pii_public;
            json << "{\"DT_RowId\":\"member-row-" << m.id << "\""
                 << ",\"id\":"               << m.id
                 << ",\"display_name\":\""   << esc_json(m.display_name) << "\""
                 << ",\"first_name\":\""     << esc_json(m.first_name) << "\""
                 << ",\"last_name\":\""      << esc_json(m.last_name) << "\""
                 << ",\"discord_username\":\"" << (show_pii ? esc_json(m.discord_username) : "") << "\""
                 << ",\"email\":\""          << (show_pii ? esc_json(m.email) : "") << "\""
                 << ",\"is_paid\":"          << (can_see_dues ? (m.is_paid ? "true" : "false") : "null")
                 << ",\"paid_until\":\""     << (can_see_dues ? esc_json(m.paid_until) : "") << "\""
                 << ",\"role\":\""           << esc_json(m.role) << "\""
                 << ",\"chapter_name\":\""   << esc_json(m.chapter_name) << "\""
                 << ",\"fol_status\":\""    << esc_json(m.fol_status) << "\""
                 << "}";
        }
        json << "]}";

        res.add_header("Content-Type", "application/json");
        res.write(json.str());
        return res;
    });

    // GET /members/me - self-edit profile form (any authenticated member)
    CROW_ROUTE(app, "/members/me")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        auto m = members.get(auth.member_id);
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_self_edit.html");
        auto ctx  = member_to_ctx(*m);
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /members/me - update own PII (any authenticated member)
    CROW_ROUTE(app, "/members/me").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        auto existing = members.get(auth.member_id);
        if (!existing) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        // Only allow PII fields — preserve all admin-managed fields
        Member updates = *existing;
        updates.first_name     = get_param("first_name");
        updates.last_name      = get_param("last_name");
        updates.email          = get_param("email");
        updates.phone          = get_param("phone");
        updates.address_line1  = get_param("address_line1");
        updates.address_line2  = get_param("address_line2");
        updates.city           = get_param("city");
        updates.state          = get_param("state");
        updates.zip            = get_param("zip");
        updates.birthday       = get_param("birthday");
        updates.pii_public     = (get_param("pii_public") == "on" || get_param("pii_public") == "1");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        try {
            members.update(auth.member_id, updates);
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + e.what() + "</div>");
        }
        return res;
    });

    // GET /members/<id>/view - read-only member detail modal (any authenticated user)
    CROW_ROUTE(app, "/members/<int>/view")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        bool show_pii = auth.is_chapter_lead() || m->pii_public;
        bool show_dues = auth.is_chapter_lead();
        bool is_self = (auth.member_id == m->id);

        auto ctx = member_to_ctx(*m);
        ctx["show_pii"]  = show_pii || is_self;
        ctx["show_dues"] = show_dues || is_self;
        ctx["is_self"]   = is_self;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_view.html");
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // GET /members/new - new member form fragment (chapter_lead+)
    CROW_ROUTE(app, "/members/new")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_form.html");
        crow::mustache::context ctx;
        ctx["action"] = "/members";
        ctx["title"]  = "Add Member";
        ctx["is_new"] = true;
        ctx["is_admin"] = app.get_context<AuthMiddleware>(req).auth.is_admin();
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // GET /members/<id> - member edit form fragment (chapter_lead+ - contains PII)
    CROW_ROUTE(app, "/members/<int>")([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        auto m = members.get(static_cast<int64_t>(id));
        if (!m) {
            res.code = 404;
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write("<div class=\"text-red-500 p-4\">Member not found.</div>");
            return res;
        }

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto tmpl = crow::mustache::load("members/_form.html");
        auto ctx  = member_to_ctx(*m);
        ctx["action"]   = "/members/" + std::to_string(id);
        ctx["title"]    = "Edit Member";
        ctx["is_edit"]  = true;
        ctx["is_admin"] = app.get_context<AuthMiddleware>(req).auth.is_admin();
        res.write(tmpl.render(ctx).dump());
        return res;
    });

    // POST /members - create member (form POST, chapter_lead+)
    CROW_ROUTE(app, "/members").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        bool caller_is_admin = app.get_context<AuthMiddleware>(req).auth.is_admin();

        Member m;
        m.discord_user_id  = get_param("discord_user_id");
        m.discord_username = get_param("discord_username");
        m.first_name       = get_param("first_name");
        m.last_name        = get_param("last_name");
        m.email            = get_param("email");
        std::string req_role = get_param("role");
        // Non-admins cannot assign admin role
        if (!caller_is_admin && req_role == "admin") req_role = "member";
        m.role             = req_role.empty() ? "member" : req_role;
        m.birthday         = get_param("birthday");
        m.fol_status       = get_param("fol_status").empty() ? "afol" : get_param("fol_status");
        m.phone            = get_param("phone");
        m.address_line1    = get_param("address_line1");
        m.address_line2    = get_param("address_line2");
        m.city             = get_param("city");
        m.state            = get_param("state");
        m.zip              = get_param("zip");
        m.pii_public       = (get_param("pii_public") == "on" || get_param("pii_public") == "1");

        res.add_header("Content-Type", "text/html; charset=utf-8");
        try {
            members.create(m);
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + e.what() + "</div>");
        }
        return res;
    });

    // POST /members/<id> - update member (form body, from edit modal, chapter_lead+)
    CROW_ROUTE(app, "/members/<int>").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        bool caller_is_admin = app.get_context<AuthMiddleware>(req).auth.is_admin();

        auto params    = crow::query_string("?" + req.body);
        auto get_param = [&](const char* k) -> std::string {
            const char* v = params.get(k);
            return v ? std::string(v) : "";
        };

        Member updates;
        updates.first_name       = get_param("first_name");
        updates.last_name        = get_param("last_name");
        updates.discord_username = get_param("discord_username");
        updates.email            = get_param("email");
        std::string req_role     = get_param("role");
        // Non-admins cannot assign admin role
        if (!caller_is_admin && req_role == "admin") req_role = "";
        updates.role             = req_role;
        updates.birthday         = get_param("birthday");
        updates.fol_status       = get_param("fol_status").empty() ? "afol" : get_param("fol_status");
        updates.phone            = get_param("phone");
        updates.address_line1    = get_param("address_line1");
        updates.address_line2    = get_param("address_line2");
        updates.city             = get_param("city");
        updates.state            = get_param("state");
        updates.zip              = get_param("zip");
        updates.pii_public       = (get_param("pii_public") == "on" || get_param("pii_public") == "1");

        std::string paid_until = get_param("paid_until");
        updates.paid_until = paid_until;
        updates.is_paid    = !paid_until.empty();

        res.add_header("Content-Type", "text/html; charset=utf-8");
        try {
            members.update(static_cast<int64_t>(id), updates);
            std::string chapter_str = get_param("chapter_id");
            int64_t chapter_id = chapter_str.empty() ? 0 : std::stoll(chapter_str);
            members.set_chapter(static_cast<int64_t>(id), chapter_id);
            res.add_header("HX-Trigger", "{\"closeModal\":true,\"membersUpdated\":true}");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(
                R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">)"
                "Error: ") + e.what() + "</div>");
        }
        return res;
    });

    // PUT /members/<id> - update member (JSON API, chapter_lead+)
    CROW_ROUTE(app, "/members/<int>").methods("PUT"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        auto body = crow::json::load(req.body);
        if (!body) {
            res.code = 400;
            res.write(R"({"error":"Invalid JSON"})");
            res.add_header("Content-Type", "application/json");
            return res;
        }

        Member updates;
        if (body.has("display_name")) updates.display_name = body["display_name"].s();
        if (body.has("email"))        updates.email        = body["email"].s();
        if (body.has("role"))         updates.role         = body["role"].s();
        if (body.has("is_paid"))      updates.is_paid      = body["is_paid"].b();
        if (body.has("paid_until"))   updates.paid_until   = body["paid_until"].s();

        try {
            auto updated = members.update(static_cast<int64_t>(id), updates);
            crow::json::wvalue resp;
            resp["id"]           = updated.id;
            resp["display_name"] = updated.display_name;
            resp["success"]      = true;
            res.write(resp.dump());
            res.add_header("Content-Type", "application/json");
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // DELETE /members/<id> - delete member (admin only)
    CROW_ROUTE(app, "/members/<int>").methods("DELETE"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        try {
            members.delete_member(static_cast<int64_t>(id));
            res.add_header("HX-Trigger", "membersUpdated");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
        }
        return res;
    });

    // POST /members/<id>/paid - set paid status (chapter_lead+)
    CROW_ROUTE(app, "/members/<int>/paid").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;

        auto params        = crow::query_string("?" + req.body);
        const char* v      = params.get("paid_until");
        std::string paid_until = v ? std::string(v) : "";
        bool is_paid           = !paid_until.empty();

        try {
            members.set_paid(static_cast<int64_t>(id), is_paid, paid_until);
            res.add_header("HX-Trigger", "duesUpdated");
            res.code = 200;
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")") + e.what() + "\"}");
        }
        res.add_header("Content-Type", "application/json");
        return res;
    });
}
