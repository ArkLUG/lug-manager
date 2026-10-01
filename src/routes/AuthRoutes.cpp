#include "routes/AuthRoutes.hpp"
#include "utils/AssetVersion.hpp"
#include <crow.h>
#include <algorithm>
#include <cctype>
#include <iostream>

// Canonical base URL from LUG_PUBLIC_URL; empty = derive from headers.
static std::string g_public_url;

// Build an absolute URL. Prefers the configured public URL: the Host and
// X-Forwarded-* headers are client-controllable unless the reverse proxy
// overwrites them, so they're only a fallback for unconfigured installs.
static std::string build_url(const crow::request& req, const std::string& path) {
    if (!g_public_url.empty()) return g_public_url + path;
    std::string proto = req.get_header_value("X-Forwarded-Proto");
    if (proto.empty()) proto = "http";
    std::string host = req.get_header_value("X-Forwarded-Host");
    if (host.empty()) host = req.get_header_value("Host");
    if (host.empty()) host = "localhost";
    return proto + "://" + host + path;
}

// "; Secure" when the client reached us over HTTPS (directly or via the
// reverse proxy), so session/state cookies never travel over plain HTTP.
static std::string secure_attr(const crow::request& req) {
    bool https = g_public_url.rfind("https://", 0) == 0 ||
                 req.get_header_value("X-Forwarded-Proto") == "https";
    return https ? "; Secure" : "";
}

// OAuth state is "<nonce>" or "<nonce>.checkin:<token>". The nonce is also
// stored in the short-lived oauth_state cookie and must match on callback -
// that is what stops login CSRF (an attacker completing their own OAuth flow
// in a victim's browser). The check-in token is restricted to UUID characters
// because it is reflected into a redirect Location.
struct OAuthState {
    std::string nonce;
    std::string checkin_token; // empty unless this is a check-in login
};

static bool is_token_char(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) || c == '-';
}

static OAuthState parse_state(const std::string& state) {
    OAuthState st;
    size_t p = state.find("checkin:");
    if (p == std::string::npos) {
        st.nonce = state;
        return st;
    }
    st.nonce = p > 0 ? state.substr(0, p - 1) : "";
    std::string tok = state.substr(p + 8);
    if (!tok.empty() && tok.size() <= 64 && std::all_of(tok.begin(), tok.end(), is_token_char))
        st.checkin_token = tok;
    return st;
}

void register_auth_routes(LugApp& app, AuthService& auth, DiscordOAuth& oauth,
                          const std::string& public_url) {
    g_public_url = public_url;

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
        mctx["asset_v"] = asset_version();
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /auth/login[?checkin=<token>] - redirect to Discord OAuth2 with a
    // fresh state nonce bound to this browser via the oauth_state cookie.
    CROW_ROUTE(app, "/auth/login")([&](const crow::request& req) {
        std::string nonce = SessionStore::generate_token();
        std::string state = nonce;
        const char* checkin = req.url_params.get("checkin");
        if (checkin) {
            auto parsed = parse_state(std::string("checkin:") + checkin);
            if (!parsed.checkin_token.empty()) state += ".checkin:" + parsed.checkin_token;
        }
        std::string redirect_uri = build_url(req, "/auth/callback");
        crow::response res;
        res.add_header("Set-Cookie", "oauth_state=" + nonce +
            "; HttpOnly; Path=/auth; Max-Age=600; SameSite=Lax" + secure_attr(req));
        res.redirect(oauth.get_auth_url(state, redirect_uri));
        return res;
    });

    // GET /auth/callback - handle OAuth2 callback (normal login or checkin)
    CROW_ROUTE(app, "/auth/callback")([&](const crow::request& req) {
        auto params = crow::query_string(req.url_params);
        const char* code_raw  = params.get("code");
        const char* error_raw = params.get("error");
        const char* state_raw = params.get("state");
        std::string state = state_raw ? std::string(state_raw) : "";
        OAuthState st = parse_state(state);
        bool is_checkin = !st.checkin_token.empty();
        const std::string checkin_path = "/checkin/" + st.checkin_token;

        if (error_raw) {
            std::string error(error_raw);
            crow::response res;
            // interaction_required only happens because we asked for prompt=none
            // (skip the consent screen for an already-authorized user) and Discord
            // couldn't silently approve — the user has never authorized this app
            // (or revoked it). Retry once with a normal prompt instead of showing
            // an error, so first-time/revoked users still get a working login.
            if (error == "interaction_required") {
                std::string redirect_uri = build_url(req, "/auth/callback");
                std::string url = oauth.get_auth_url(state, redirect_uri, /*skip_prompt=*/false);
                res.redirect(url);
                return res;
            }
            if (is_checkin) {
                res.redirect(build_url(req, checkin_path + "?error=discord_denied"));
            } else {
                res.redirect(build_url(req, "/login?error=discord_denied"));
            }
            return res;
        }

        if (!code_raw) {
            crow::response res;
            res.redirect(build_url(req, "/login?error=no_code"));
            return res;
        }

        // Login CSRF guard: the state nonce must match the cookie set by
        // /auth/login in this same browser.
        std::string expected_nonce = get_cookie(req, "oauth_state");
        if (expected_nonce.empty() || st.nonce != expected_nonce) {
            crow::response res;
            res.redirect(build_url(req, is_checkin ? checkin_path + "?error=failed"
                                                   : "/login?error=failed"));
            return res;
        }

        std::string code(code_raw);
        std::string redirect_uri = build_url(req, "/auth/callback");
        try {
            std::string session_token = auth.login_with_discord(code, redirect_uri, req.get_header_value("User-Agent"));
            crow::response res;
            res.add_header("Set-Cookie",
                "session=" + session_token + "; HttpOnly; Path=/; Max-Age=86400; SameSite=Lax" + secure_attr(req));
            res.add_header("Set-Cookie", "oauth_state=; HttpOnly; Path=/auth; Max-Age=0; SameSite=Lax");
            if (is_checkin) {
                // Redirect back to checkin page — the session cookie will identify the user.
                // The one-shot checkin_ok cookie proves this visit is the tail of
                // the user's own login, so a bare link to ?discord=1 can't check
                // a logged-in member in without their action.
                res.add_header("Set-Cookie", "checkin_ok=" + st.checkin_token +
                    "; HttpOnly; Path=/checkin; Max-Age=300; SameSite=Lax" + secure_attr(req));
                res.redirect(build_url(req, checkin_path + "?discord=1"));
            } else {
                res.redirect(build_url(req, "/dashboard"));
            }
            return res;
        } catch (const std::exception& e) {
            std::string err = e.what();
            crow::response res;
            if (is_checkin) {
                res.redirect(build_url(req, checkin_path + "?error=failed"));
            } else if (err == "not_authorized") {
                res.redirect(build_url(req, "/login?error=not_member"));
            } else {
                std::cerr << "[auth] Login error: " << err << "\n";
                res.redirect(build_url(req, "/login?error=failed"));
            }
            return res;
        }
    });

    // POST /auth/logout - POST only, so a cross-site link/image can't log users out
    CROW_ROUTE(app, "/auth/logout").methods("POST"_method)(
        [&](const crow::request& req) {
        std::string token = get_cookie(req, "session");
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
