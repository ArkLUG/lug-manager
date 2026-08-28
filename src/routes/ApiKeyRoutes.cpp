#include "routes/ApiKeyRoutes.hpp"
#include "utils/Crypto.hpp"
#include <crow/mustache.h>

void register_api_key_routes(LugApp& app, ApiKeyRepository& api_keys, AuditService& audit) {

    // GET /settings/api-keys - list page
    CROW_ROUTE(app, "/settings/api-keys")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto keys = api_keys.find_all();
        crow::mustache::context mctx;
        crow::json::wvalue arr;
        for (size_t i = 0; i < keys.size(); ++i) {
            arr[i]["id"]               = keys[i].id;
            arr[i]["label"]            = keys[i].label;
            arr[i]["scope"]            = keys[i].scope;
            arr[i]["created_by_name"]  = keys[i].created_by_name.empty() ? "—" : keys[i].created_by_name;
            arr[i]["created_at"]       = keys[i].created_at;
            arr[i]["last_used_at"]     = keys[i].last_used_at.empty() ? "never" : keys[i].last_used_at;
            arr[i]["is_active"]        = keys[i].revoked_at.empty();
        }
        mctx["keys"]      = std::move(arr);
        mctx["has_keys"]  = !keys.empty();

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            auto tmpl = crow::mustache::load("settings/_api_keys.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(tmpl.render(mctx).dump());
        } else {
            auto content_tmpl = crow::mustache::load("settings/_api_keys.html");
            std::string content = content_tmpl.render(mctx).dump();
            crow::mustache::context layout_ctx;
            layout_ctx["content"]          = content;
            layout_ctx["page_title"]       = "API Keys";
            layout_ctx["active_api_keys"]  = true;
            layout_ctx["is_admin"]         = true;
            set_layout_auth(req, app, layout_ctx);
            auto layout = crow::mustache::load("layout.html");
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(layout.render(layout_ctx).dump());
        }
        return res;
    });

    // POST /settings/api-keys - create a key
    CROW_ROUTE(app, "/settings/api-keys").methods("POST"_method)(
        [&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto params = crow::query_string("?" + req.body);
        const char* label_p = params.get("label");
        const char* scope_p = params.get("scope");
        std::string label = label_p ? label_p : "";
        std::string scope = scope_p ? scope_p : "";

        if (scope != "read" && scope != "write" && scope != "admin") {
            res.code = 400;
            res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">Invalid scope.</div>)");
            return res;
        }

        auto& ctx = app.template get_context<AuthMiddleware>(req);
        std::string raw_key  = generate_random_hex(32);
        std::string key_hash = sha256_hex(raw_key);
        auto created = api_keys.create(key_hash, label, scope, ctx.auth.member_id);

        audit.log(req, app, "api_key.create", "api_key", created.id, label,
                  "Created API key (scope=" + scope + ")");

        crow::mustache::context mctx;
        mctx["raw_key"] = raw_key;
        mctx["label"]   = label;
        mctx["scope"]   = scope;
        auto tmpl = crow::mustache::load("settings/_api_key_created.html");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "apiKeyCreated");
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // POST /settings/api-keys/<id>/revoke
    CROW_ROUTE(app, "/settings/api-keys/<int>/revoke").methods("POST"_method)(
        [&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;

        auto key = api_keys.find_by_id(static_cast<int64_t>(id));
        std::string label = key ? key->label : "";
        api_keys.revoke(static_cast<int64_t>(id));
        audit.log(req, app, "api_key.revoke", "api_key", id, label, "Revoked API key");

        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.add_header("HX-Redirect", "/settings/api-keys");
            res.code = 200;
        } else {
            res.redirect("/settings/api-keys");
        }
        return res;
    });
}
