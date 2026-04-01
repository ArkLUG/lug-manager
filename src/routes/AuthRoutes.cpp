#include "routes/AuthRoutes.hpp"
#include <crow.h>
#include <iostream>

// Build absolute URL respecting X-Forwarded-Proto/Host from reverse proxy
static std::string build_url(const crow::request& req, const std::string& path) {
    std::string proto = req.get_header_value("X-Forwarded-Proto");
    if (proto.empty()) proto = "http";
    std::string host = req.get_header_value("X-Forwarded-Host");
    if (host.empty()) host = req.get_header_value("Host");
    if (host.empty()) host = "localhost";
    return proto + "://" + host + path;
}

void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth) {

    // GET /login - show login page
    CROW_ROUTE(app, "/login")([&](const crow::request& req) {
        // If already authenticated, redirect to dashboard
        auto& ctx = app.get_context<AuthMiddleware>(req);
        if (ctx.auth.authenticated) {
            crow::response res;
            res.redirect(build_url(req, "/dashboard"));
            return res;
        }

        // Check for error query param
        auto params = crow::query_string(req.url_params);
        const char* error_raw = params.get("error");
        std::string error = error_raw ? std::string(error_raw) : "";

        auto tmpl = crow::mustache::load("login.html");
        crow::mustache::context mctx;
        if (error == "not_member")      mctx["error_not_member"]     = true;
        if (error == "discord_denied")  mctx["error_discord_denied"] = true;
        if (error == "failed")          mctx["error_failed"]         = true;
        if (error == "no_code")         mctx["error_failed"]         = true;

        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /auth/login - redirect to Discord OAuth2
    CROW_ROUTE(app, "/auth/login")([&](const crow::request& req) {
        std::string redirect_uri = build_url(req, "/auth/callback");
        std::string url = oauth.get_auth_url("lug-state", redirect_uri);
        crow::response res;
        res.redirect(url);
        return res;
    });

    // GET /auth/callback - handle OAuth2 callback
    CROW_ROUTE(app, "/auth/callback")([&](const crow::request& req) {
        auto params = crow::query_string(req.url_params);
        const char* code_raw  = params.get("code");
        const char* error_raw = params.get("error");

        if (error_raw) {
            crow::response res;
            res.redirect(build_url(req, "/login?error=discord_denied"));
            return res;
        }

        if (!code_raw) {
            crow::response res;
            res.redirect(build_url(req, "/login?error=no_code"));
            return res;
        }

        std::string code(code_raw);
        std::string redirect_uri = build_url(req, "/auth/callback");
        try {
            std::string token = auth.login_with_discord(code, redirect_uri);
            crow::response res;
            res.add_header("Set-Cookie",
                "session=" + token + "; HttpOnly; Path=/; Max-Age=86400; SameSite=Lax");
            res.redirect(build_url(req, "/dashboard"));
            return res;
        } catch (const std::exception& e) {
            std::string err = e.what();
            crow::response res;
            if (err == "not_authorized") {
                res.redirect(build_url(req, "/login?error=not_member"));
            } else {
                std::cerr << "[auth] Login error: " << err << "\n";
                res.redirect(build_url(req, "/login?error=failed"));
            }
            return res;
        }
    });

    // POST or GET /auth/logout
    CROW_ROUTE(app, "/auth/logout").methods("POST"_method, "GET"_method)(
        [&](const crow::request& req) {
        // Extract session cookie and destroy it
        std::string cookie_header = req.get_header_value("Cookie");
        std::string token;
        size_t pos = cookie_header.find("session=");
        if (pos != std::string::npos) {
            pos += 8;
            size_t end = cookie_header.find(';', pos);
            token = cookie_header.substr(
                pos, end == std::string::npos ? std::string::npos : end - pos);
        }
        if (!token.empty()) auth.logout(token);

        crow::response res;
        res.add_header("Set-Cookie", "session=; HttpOnly; Path=/; Max-Age=0; SameSite=Lax");
        res.redirect(build_url(req, "/login"));
        return res;
    });

    // GET / - redirect based on auth status
    CROW_ROUTE(app, "/")([&](const crow::request& req) {
        auto& ctx = app.get_context<AuthMiddleware>(req);
        crow::response res;
        if (ctx.auth.authenticated) {
            res.redirect(build_url(req, "/dashboard"));
        } else {
            res.redirect(build_url(req, "/login"));
        }
        return res;
    });
}
