#include "routes/accounts/AuthRoutes.hpp"
#include "utils/web/ClientIp.hpp"
#include "utils/web/AssetVersion.hpp"
#include "utils/Crypto.hpp"
#include "services/notifications/Notifier.hpp"
#include "services/AuditService.hpp"
#include "repositories/members/NotificationPrefs.hpp"
#include "auth/AccountSecurity.hpp"
#include <crow.h>
#include <algorithm>
#include <cctype>
#include <iostream>

// Canonical base URL from LUG_PUBLIC_URL; empty = derive from headers.
static std::string g_public_url;
// Set by register_email_auth_routes when email sign-in is available.
static bool g_email_login = false;
// Set by register_email_auth_routes: the DB (accounts, 2FA, settings) and audit log.
static SqliteDatabase* g_db = nullptr;
static AuditService* g_audit = nullptr;
static AttemptLimiter g_per_account(8, 900), g_per_ip(30, 900);

static std::string auth_setting(const std::string& key, const std::string& def) {
    if (!g_db) return def;
    auto st = g_db->prepare("SELECT value FROM lug_settings WHERE key=?");
    st.bind(1, key);
    return st.step() ? st.col_text(0) : def;
}
static bool password_login_on() { return auth_setting("auth_password_enabled", "1") != "0"; }
static bool email_links_on()    { return g_email_login && auth_setting("auth_email_links", "1") != "0"; }

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

// Links that go out by email use LUG_PUBLIC_URL only. Built from the request's
// Host header, anyone could ask for a reset link to someone else's account
// with their own host in it ("reset poisoning") and collect the token when
// the owner clicks. Without LUG_PUBLIC_URL these emails aren't sent.
static std::string emailed_url(const std::string& path) {
    if (g_public_url.empty()) {
        std::cerr << "[auth] LUG_PUBLIC_URL isn't set: sign-in and password-reset emails are off\n";
        return "";
    }
    return g_public_url + path;
}

// "; Secure" when the client reached us over HTTPS (directly or via the
// reverse proxy), so session/state cookies never travel over plain HTTP.
static std::string secure_attr(const crow::request& req) {
    bool https = g_public_url.rfind("https://", 0) == 0 ||
                 req.get_header_value("X-Forwarded-Proto") == "https";
    return https ? "; Secure" : "";
}

// Creates the session after every check has passed. `next` is "" (dashboard),
// "checkin:<token>" (back to a check-in page) or "recovery" (recovery code used).
static crow::response start_session(const crow::request& req, AuthService& auth, int64_t id, const std::string& role,
                                    const std::string& name, const std::string& next) {
    crow::response res;
    std::string session = auth.sessions().create(id, role, name, 24, req.get_header_value("User-Agent"));
    res.add_header("Set-Cookie", "session=" + session + "; HttpOnly; Path=/; Max-Age=86400; SameSite=Lax" + secure_attr(req));
    res.add_header("Set-Cookie", "mfa=; HttpOnly; Path=/auth/2fa; Max-Age=0; SameSite=Lax");
    if (next.rfind("checkin:", 0) == 0) {
        std::string tok = next.substr(8);
        // The one-shot checkin_ok cookie proves this visit is the tail of the
        // member's own sign-in (see /checkin).
        res.add_header("Set-Cookie", "checkin_ok=" + tok + "; HttpOnly; Path=/checkin; Max-Age=300; SameSite=Lax" + secure_attr(req));
        res.redirect(build_url(req, "/checkin/" + tok + "?discord=1"));
    } else if (next == "recovery") {
        res.redirect(build_url(req, "/account/security?recovery_used=1"));
    } else {
        res.redirect(build_url(req, "/dashboard"));
    }
    return res;
}

// First factor passed (Discord, password or email link). Members with
// two-factor on get the code page; everyone else is signed in.
static crow::response finish_sign_in(const crow::request& req, AuthService& auth, int64_t id, const std::string& role,
                                     const std::string& name, const std::string& method, const std::string& next) {
    if (g_db) {
        AccountSecurity sec(*g_db);
        auto acct = sec.by_id(id);
        if (acct && acct->two_factor) {
            crow::response res;
            std::string tok = sec.create_challenge(id, method, next);
            res.add_header("Set-Cookie", "mfa=" + tok + "; HttpOnly; Path=/auth/2fa; Max-Age=300; SameSite=Lax" + secure_attr(req));
            res.redirect(build_url(req, "/auth/2fa"));
            return res;
        }
    }
    if (g_audit && method != "discord") g_audit->log_system("auth.login", "member", id, name, "Signed in (" + method + ")");
    return start_session(req, auth, id, role, name, next);
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
        if (error == "link")            mctx["error_link"]           = true;
        if (params.get("email_sent"))   mctx["email_sent"]           = true;
        mctx["email_login"] = email_links_on();
        mctx["password_login"] = password_login_on();
        mctx["forgot"] = password_login_on() && g_email_login;
        mctx["discord_login"] = auth_setting("auth_discord_enabled", "1") != "0" && Features::on("discord");
        if (error == "password")        mctx["error_password"]       = true;
        if (error == "locked")          mctx["error_locked"]         = true;
        if (error == "expired")         mctx["error_expired"]        = true;
        if (params.get("reset"))        mctx["reset_done"]           = true;
        if (params.get("forgot_sent"))  mctx["forgot_sent"]          = true;

        crow::response res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        mctx["asset_v"] = asset_version();
        res.write(tmpl.render(mctx).dump());
        return res;
    });

    // GET /auth/login[?checkin=<token>] - redirect to Discord OAuth2 with a
    // fresh state nonce bound to this browser via the oauth_state cookie.
    CROW_ROUTE(app, "/auth/login")([&](const crow::request& req) {
        if (auth_setting("auth_discord_enabled", "1") == "0" || !Features::on("discord")) {
            crow::response off;
            off.redirect(build_url(req, "/login"));
            return off;
        }
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
        if (auth_setting("auth_discord_enabled", "1") == "0" || !Features::on("discord")) {
            crow::response off;
            off.redirect(build_url(req, "/login"));
            return off;
        }
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
            Member m = auth.discord_member(code, redirect_uri);
            crow::response res = finish_sign_in(req, auth, m.id, m.role, m.display_name, "discord",
                                                is_checkin ? "checkin:" + st.checkin_token : "");
            res.add_header("Set-Cookie", "oauth_state=; HttpOnly; Path=/auth; Max-Age=0; SameSite=Lax");
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

// ── Email sign-in + unsubscribe ─────────────────────────────────────────────

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string utc_in(int seconds) {
    std::time_t t = std::time(nullptr) + seconds;
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tm);
    return b;
}

crow::response page(const std::string& tmpl, crow::mustache::context& ctx, int code = 200) {
    crow::response res;
    res.code = code;
    ctx["asset_v"] = asset_version();
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.add_header("Cache-Control", "no-store");
    res.add_header("Referrer-Policy", "no-referrer");   // the URL carries a token
    res.write(crow::mustache::load(tmpl).render(ctx).dump());
    return res;
}

// Valid, unused, unexpired sign-in token -> member id.
std::optional<int64_t> pending_login(SqliteDatabase& db, const std::string& token) {
    auto st = db.prepare("SELECT member_id FROM email_login_tokens WHERE token_hash=? AND used_at IS NULL AND expires_at > ?");
    st.bind(1, sha256_hex(token)); st.bind(2, utc_in(0));
    if (!st.step()) return std::nullopt;
    return st.col_int(0);
}

bool token_shape_ok(const std::string& t) {
    return !t.empty() && t.size() <= 128 &&
           std::all_of(t.begin(), t.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

} // namespace

void register_email_auth_routes(LugApp& app, AuthService& auth, SqliteDatabase& db,
                                std::shared_ptr<Notifier> notifier, AuditService& audit) {
    g_email_login = notifier && notifier->mailer() && notifier->mailer()->enabled();
    g_db = &db;
    g_audit = &audit;

    // POST /auth/email - email a one-time sign-in link to a member without Discord.
    // Always answers the same way so it can't be used to discover addresses.
    CROW_ROUTE(app, "/auth/email").methods("POST"_method)([&db, notifier](const crow::request& req) {
        crow::response res;
        res.redirect(build_url(req, "/login?email_sent=1"));
        if (!email_links_on()) return res;
        auto p = crow::query_string("?" + req.body);
        const char* raw = p.get("email");
        std::string email = raw ? lower(std::string(raw).substr(0, 254)) : "";
        auto acct = AccountSecurity(db).by_email(email);     // exactly one member with this email
        if (!acct) return res;
        int64_t id = acct->id;
        std::string name = acct->display_name, to = acct->email;
        {
            auto rl = db.prepare("SELECT COUNT(*) FROM email_login_tokens WHERE member_id=? AND created_at > ?");
            rl.bind(1, id); rl.bind(2, utc_in(-3600));
            if (rl.step() && rl.col_int(0) >= 3) return res;   // max 3 links an hour
        }
        if (emailed_url("").empty()) return res;
        std::string token = SessionStore::generate_token();
        {
            auto ins = db.prepare("INSERT INTO email_login_tokens (token_hash, member_id, expires_at) VALUES (?,?,?)");
            ins.bind(1, sha256_hex(token)); ins.bind(2, id); ins.bind(3, utc_in(15 * 60));
            ins.step();
        }
        notifier->send_email_template(id, to, name, "email.sign_in_link", {{"link", emailed_url("/auth/email/" + token)}});
        return res;
    });

    // GET /auth/email/<token> - confirm page (a POST does the sign-in, so mail
    // scanners that prefetch links can't use up the token).
    CROW_ROUTE(app, "/auth/email/<string>")([&db](const crow::request& req, const std::string& token) {
        crow::response res;
        if (!token_shape_ok(token) || !pending_login(db, token)) {
            res.redirect(build_url(req, "/login?error=link"));
            return res;
        }
        crow::mustache::context ctx;
        ctx["token"] = token;
        return page("auth_email.html", ctx);
    });

    CROW_ROUTE(app, "/auth/email/<string>").methods("POST"_method)(
        [&app, &auth, &db, &audit](const crow::request& req, const std::string& token) {
        crow::response res;
        if (!token_shape_ok(token)) { res.redirect(build_url(req, "/login?error=link")); return res; }
        int64_t member_id = 0;
        {
            auto use = db.prepare("UPDATE email_login_tokens SET used_at=? WHERE token_hash=? AND used_at IS NULL "
                                  "AND expires_at > ? RETURNING member_id");
            use.bind(1, utc_in(0)); use.bind(2, sha256_hex(token)); use.bind(3, utc_in(0));
            if (use.step()) member_id = use.col_int(0);
        }
        std::string role, name;
        if (member_id > 0) {
            auto m = db.prepare("SELECT role, display_name FROM members WHERE id=?");
            m.bind(1, member_id);
            if (m.step()) { role = m.col_text(0); name = m.col_text(1); }
        }
        if (role.empty()) { res.redirect(build_url(req, "/login?error=link")); return res; }
        (void)app; (void)audit;
        return finish_sign_in(req, auth, member_id, role, name, "email link", "");
    });

    // ── Email + password ──
    // Limits: 8 tries per email and 30 per IP address, per 15 minutes.
    g_per_account.reset();
    g_per_ip.reset();

    // POST /auth/password - email + password; then the 2FA code if it's on
    CROW_ROUTE(app, "/auth/password").methods("POST"_method)([&auth, &db, &audit](const crow::request& req) {
        crow::response res;
        if (!password_login_on()) { res.redirect(build_url(req, "/login")); return res; }
        auto p = crow::query_string("?" + req.body);
        const char* e = p.get("email");
        const char* pw = p.get("password");
        std::string email = e ? lower(std::string(e).substr(0, 254)) : "";
        std::string password = pw ? std::string(pw).substr(0, password::kMaxLength + 1) : "";
        std::string ip = client_ip(req), kacct = "acct:" + email, kip = "ip:" + ip;
        if (!g_per_account.allowed(kacct) || !g_per_ip.allowed(kip)) {
            res.redirect(build_url(req, "/login?error=locked"));
            return res;
        }
        AccountSecurity sec(db);
        auto acct = sec.by_email(email);
        bool ok = acct && sec.check_password(*acct, password);
        if (!acct) password::verify_dummy(password);            // same time either way
        if (!ok) {
            g_per_account.hit(kacct);
            g_per_ip.hit(kip);
            if (acct) audit.log_system("auth.login_failed", "member", acct->id, acct->display_name, "Wrong password", ip);
            res.redirect(build_url(req, "/login?error=password"));
            return res;
        }
        g_per_account.clear(kacct);
        return finish_sign_in(req, auth, acct->id, acct->role, acct->display_name, "password", "");
    });

    // GET /auth/2fa - the code page between the first factor and the session
    CROW_ROUTE(app, "/auth/2fa")([&db](const crow::request& req) {
        crow::response res;
        if (!AccountSecurity(db).challenge(get_cookie(req, "mfa"))) {
            res.redirect(build_url(req, "/login?error=expired"));
            return res;
        }
        crow::mustache::context ctx;
        return page("auth_2fa.html", ctx);
    });

    CROW_ROUTE(app, "/auth/2fa").methods("POST"_method)([&auth, &db, &audit](const crow::request& req) {
        crow::response res;
        std::string token = get_cookie(req, "mfa");
        AccountSecurity sec(db);
        auto ch = sec.challenge(token);
        if (!ch) { res.redirect(build_url(req, "/login?error=expired")); return res; }
        auto p = crow::query_string("?" + req.body);
        const char* c = p.get("code");
        std::string code = c ? std::string(c).substr(0, 40) : "";
        bool recovery = false;
        auto acct = sec.by_id(ch->member_id);
        if (!acct || !sec.check_second_factor(ch->member_id, code, &recovery)) {
            sec.challenge_failed(token);
            if (acct) audit.log_system("auth.2fa_failed", "member", acct->id, acct->display_name, "Wrong code (" + ch->method + ")",
                                       client_ip(req));
            int left = 4 - ch->attempts;
            if (left <= 0) { res.redirect(build_url(req, "/login?error=expired")); return res; }
            crow::mustache::context ctx;
            ctx["error"] = true;
            ctx["left"] = left;
            return page("auth_2fa.html", ctx, 400);
        }
        sec.challenge_done(token);
        audit.log_system("auth.login", "member", acct->id, acct->display_name,
                         "Signed in (" + ch->method + (recovery ? ", recovery code" : ", 2FA code") + ")");
        return start_session(req, auth, acct->id, acct->role, acct->display_name, recovery ? "recovery" : ch->next);
    });

    // GET/POST /auth/forgot - email a "set a new password" link (needs SMTP)
    CROW_ROUTE(app, "/auth/forgot")([](const crow::request& req) {
        crow::response res;
        if (!password_login_on() || !g_email_login) { res.redirect(build_url(req, "/login")); return res; }
        crow::mustache::context ctx;
        return page("auth_forgot.html", ctx);
    });
    CROW_ROUTE(app, "/auth/forgot").methods("POST"_method)([&db, notifier, &audit](const crow::request& req) {
        crow::response res;
        res.redirect(build_url(req, "/login?forgot_sent=1"));      // same answer whatever the email
        if (!password_login_on() || !g_email_login) return res;
        auto p = crow::query_string("?" + req.body);
        const char* e = p.get("email");
        std::string email = e ? lower(std::string(e).substr(0, 254)) : "";
        if (!g_per_ip.allowed("forgot:" + client_ip(req))) return res;
        g_per_ip.hit("forgot:" + client_ip(req));
        AccountSecurity sec(db);
        auto acct = sec.by_email(email);
        if (!acct || sec.recent_reset_tokens(acct->id) >= 3 || emailed_url("").empty()) return res;
        std::string token = sec.create_reset_token(acct->id, 0, 1);
        notifier->send_email_template(acct->id, acct->email, acct->display_name, "email.password_reset",
                                      {{"link", emailed_url("/auth/reset/" + token)}});
        audit.log_system("auth.password_reset_sent", "member", acct->id, acct->display_name, "Reset link emailed", client_ip(req));
        return res;
    });

    // GET/POST /auth/reset/<token> - choose a new password (emailed or admin-made link)
    CROW_ROUTE(app, "/auth/reset/<string>")([&db](const crow::request& req, const std::string& token) {
        crow::response res;
        auto id = AccountSecurity(db).reset_token_member(token);
        if (!id || !password_login_on()) { res.redirect(build_url(req, "/login?error=link")); return res; }
        auto acct = AccountSecurity(db).by_id(*id);
        crow::mustache::context ctx;
        ctx["token"] = token;
        ctx["email"] = acct ? acct->email : "";
        ctx["name"] = acct ? acct->display_name : "";
        return page("auth_reset.html", ctx);
    });
    CROW_ROUTE(app, "/auth/reset/<string>").methods("POST"_method)(
        [&auth, &db, &audit](const crow::request& req, const std::string& token) {
        crow::response res;
        AccountSecurity sec(db);
        auto id = sec.reset_token_member(token);
        if (!id || !password_login_on()) { res.redirect(build_url(req, "/login?error=link")); return res; }
        auto acct = sec.by_id(*id);
        auto p = crow::query_string("?" + req.body);
        const char* a = p.get("password");
        const char* b = p.get("confirm");
        std::string pw = a ? std::string(a).substr(0, password::kMaxLength + 1) : "", again = b ? b : "";
        std::string err = password::policy_error(pw, acct ? acct->email : "");
        if (err.empty() && pw != again) err = "The two passwords don't match.";
        if (err.empty() && acct && (acct->email.empty() || !sec.by_email(acct->email)))
            err = "Your account needs an email address that no other member uses. Ask an admin to fix your email first.";
        if (!err.empty()) {
            crow::mustache::context ctx;
            ctx["token"] = token;
            ctx["email"] = acct ? acct->email : "";
            ctx["name"] = acct ? acct->display_name : "";
            ctx["error"] = err;
            return page("auth_reset.html", ctx, 400);
        }
        if (!sec.use_reset_token(token)) { res.redirect(build_url(req, "/login?error=link")); return res; }
        sec.set_password(*id, pw);
        auth.sessions().remove_all_for_member(*id);               // sign out everywhere
        audit.log_system("auth.password_set", "member", *id, acct ? acct->display_name : "", "Password set with a link",
                         client_ip(req));
        res.redirect(build_url(req, "/login?reset=1"));
        return res;
    });

    // GET /unsubscribe/<token>[?kind=..] - no login; shows the choices
    CROW_ROUTE(app, "/unsubscribe/<string>")([&db](const crow::request& req, const std::string& token) {
        crow::mustache::context ctx;
        int64_t id = 0;
        if (token_shape_ok(token)) {
            auto st = db.prepare("SELECT id FROM members WHERE email_token=?");
            st.bind(1, token);
            if (st.step()) id = st.col_int(0);
        }
        if (!id) { ctx["invalid"] = true; return page("unsubscribe.html", ctx, 404); }
        auto off = NotificationPrefs(db).optouts(id);
        const char* kind = req.url_params.get("kind");
        crow::json::wvalue kinds = crow::json::wvalue::list();
        int i = 0;
        for (const auto& k : NotificationPrefs::kinds()) {
            if (std::string(k.key) == "email" || (*k.feature && !Features::on(k.feature))) continue;
            kinds[i]["key"] = k.key; kinds[i]["label"] = k.label; kinds[i]["off"] = off.count(k.key) > 0;
            kinds[i]["this_one"] = kind && std::string(kind) == k.key;
            ++i;
        }
        ctx["kinds"] = std::move(kinds);
        ctx["token"] = token;
        ctx["all_off"] = off.count("email") > 0;
        return page("unsubscribe.html", ctx);
    });

    // POST /unsubscribe/<token>?kind=<k|email> - also the RFC 8058 one-click target
    CROW_ROUTE(app, "/unsubscribe/<string>").methods("POST"_method)(
        [&db, &audit](const crow::request& req, const std::string& token) {
        crow::mustache::context ctx;
        int64_t id = 0;
        std::string name;
        if (token_shape_ok(token)) {
            auto st = db.prepare("SELECT id, display_name FROM members WHERE email_token=?");
            st.bind(1, token);
            if (st.step()) { id = st.col_int(0); name = st.col_text(1); }
        }
        if (!id) { ctx["invalid"] = true; return page("unsubscribe.html", ctx, 404); }
        auto body = crow::query_string("?" + req.body);
        const char* k = req.url_params.get("kind");
        if (!k) k = body.get("kind");
        std::string kind = k ? k : "email";
        bool resub = body.get("resubscribe") != nullptr;
        if (!NotificationPrefs::is_kind(kind)) kind = "email";
        NotificationPrefs(db).set(id, kind, resub);
        audit.log_system("member.email_unsubscribe", "member", id, name, (resub ? "Re-enabled " : "Turned off ") + kind);
        ctx["done"] = true;
        ctx["resubscribed"] = resub;
        ctx["token"] = token;
        for (const auto& kk : NotificationPrefs::kinds())
            if (kind == kk.key) ctx["what"] = std::string(kind == "email" ? "all email" : kk.label);
        return page("unsubscribe.html", ctx);
    });
}
