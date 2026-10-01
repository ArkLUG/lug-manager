#include "routes/AccountSecurityRoutes.hpp"
#include "utils/FormBody.hpp"
#include "auth/AccountSecurity.hpp"
#include <crow/mustache.h>

namespace {

using Form = FormBody;

std::string base_url(const std::string& public_url, const crow::request& req) {
    if (!public_url.empty()) return public_url;
    std::string proto = req.get_header_value("X-Forwarded-Proto");
    std::string host = req.get_header_value("X-Forwarded-Host");
    if (host.empty()) host = req.get_header_value("Host");
    return (proto.empty() ? "http" : proto) + "://" + (host.empty() ? "localhost" : host);
}

struct View {
    std::string flash, error, secret;
    std::vector<std::string> codes;
};

std::string render(SqliteDatabase& db, SettingsRepository& settings, int64_t id, const View& v) {
    AccountSecurity sec(db);
    auto a = sec.by_id(id);
    crow::mustache::context ctx;
    if (!a) return "";
    ctx["email"] = a->email;
    ctx["has_email"] = !a->email.empty();
    bool unique = !a->email.empty() && !sec.email_taken(a->email, id);
    ctx["email_unique"] = unique;
    ctx["email_shared"] = !a->email.empty() && !unique;
    ctx["has_password"] = !a->password_hash.empty();
    ctx["password_login"] = settings.get("auth_password_enabled", "1") != "0";
    ctx["two_factor"] = a->two_factor;
    ctx["codes_left"] = sec.recovery_codes_left(id);
    ctx["few_codes"] = a->two_factor && sec.recovery_codes_left(id) < 3;
    std::string req2 = settings.get("auth_require_2fa", "off");
    ctx["required"] = AccountSecurity::role_requires(req2, a->role);
    ctx["required_missing"] = AccountSecurity::role_requires(req2, a->role) && !a->two_factor;
    {
        auto st = db.prepare("SELECT COALESCE(substr(password_changed_at,1,10),''), COALESCE(substr(totp_enabled_at,1,10),'') FROM members WHERE id=?");
        st.bind(1, id);
        if (st.step()) { ctx["password_changed"] = st.col_text(0); ctx["two_factor_since"] = st.col_text(1); }
    }
    if (!v.secret.empty()) {
        std::string issuer = settings.get("lug_name", "");
        issuer = issuer.empty() ? "LUG Manager" : issuer;
        ctx["setup_secret"] = v.secret;
        std::string spaced;
        for (size_t i = 0; i < v.secret.size(); ++i) spaced += (i && i % 4 == 0 ? " " : "") + std::string(1, v.secret[i]);
        ctx["setup_secret_spaced"] = spaced;
        ctx["setup_uri"] = totp::provisioning_uri(v.secret, a->email.empty() ? a->display_name : a->email, issuer);
    }
    if (!v.codes.empty()) {
        crow::json::wvalue arr = crow::json::wvalue::list();
        for (size_t i = 0; i < v.codes.size(); ++i) arr[i] = v.codes[i];
        ctx["recovery_codes"] = std::move(arr);
        ctx["show_codes"] = true;
    }
    if (!v.flash.empty()) ctx["flash"] = v.flash;
    if (!v.error.empty()) ctx["error"] = v.error;
    return crow::mustache::load("account/_security.html").render(ctx).dump();
}

crow::response reply(const crow::request& req, LugApp& app, const std::string& html, int code = 200) {
    auto res = html_page(req, app, html, "Password & two-factor", "active_account", code);
    res.add_header("Cache-Control", "no-store");
    return res;
}

} // namespace

void register_account_security_routes(LugApp& app, SqliteDatabase& db, SettingsRepository& settings, AuthService& auth,
                                      std::shared_ptr<Notifier> notifier, AuditService& audit, const std::string& public_url) {

    // GET /account/security - password and two-factor (also where members are
    // sent when two-factor is required and they haven't set it up yet)
    CROW_ROUTE(app, "/account/security")([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        View v;
        if (req.url_params.get("recovery_used"))
            v.flash = "You signed in with a recovery code; that code can't be used again.";
        return reply(req, app, render(db, settings, a.member_id, v));
    });

    // POST /account/password - set or change your password
    CROW_ROUTE(app, "/account/password").methods("POST"_method)([&app, &db, &settings, &auth, notifier, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(db);
        auto a = sec.by_id(ctx.member_id);
        Form f(req);
        std::string current = f.get("current", 201), pw = f.get("password", 201), again = f.get("confirm", 201);
        View v;
        if (settings.get("auth_password_enabled", "1") == "0") v.error = "Password sign-in is turned off for this LUG.";
        else if (!a || a->email.empty()) v.error = "Add an email address to your profile first: it's what you'll sign in with.";
        else if (sec.email_taken(a->email, a->id)) v.error = "Another member has the same email address. Ask an admin to fix it before setting a password.";
        else if (!a->password_hash.empty() && !sec.check_password(*a, current)) v.error = "Your current password isn't right.";
        else if (std::string e = password::policy_error(pw, a->email); !e.empty()) v.error = e;
        else if (pw != again) v.error = "The two new passwords don't match.";
        if (!v.error.empty()) return reply(req, app, render(db, settings, ctx.member_id, v), 400);
        bool had = !a->password_hash.empty();
        sec.set_password(a->id, pw);
        int out = auth.sessions().remove_all_for_member(a->id, get_cookie(req, "session"));
        audit.log(req, app, "auth.password_set", "member", a->id, a->display_name, had ? "Changed password" : "Set a password");
        if (had && notifier && notifier->mailer() && notifier->mailer()->enabled())
            notifier->send_email_template(a->id, a->email, a->display_name, "email.password_changed", {});
        v.flash = std::string(had ? "Password changed." : "Password set: you can now sign in with your email and password.") +
                  (out > 0 ? " Other devices were signed out." : "");
        return reply(req, app, render(db, settings, ctx.member_id, v));
    });

    // POST /account/2fa/setup - new secret, shown as a QR code + text
    CROW_ROUTE(app, "/account/2fa/setup").methods("POST"_method)([&app, &db, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(db);
        auto a = sec.by_id(ctx.member_id);
        View v;
        if (a && a->two_factor) v.error = "Two-factor is already on.";
        else v.secret = sec.begin_totp_setup(ctx.member_id);
        return reply(req, app, render(db, settings, ctx.member_id, v));
    });

    // POST /account/2fa/enable - confirm with a code; shows recovery codes once
    CROW_ROUTE(app, "/account/2fa/enable").methods("POST"_method)([&app, &db, &settings, &auth, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(db);
        View v;
        v.codes = sec.enable_totp(ctx.member_id, Form(req).get("code", 20));
        if (v.codes.empty()) {
            v.secret = sec.pending_secret(ctx.member_id);
            v.error = v.secret.empty() ? "Start the setup again." : "That code didn't match. Check the time on your phone is right, and try the newest code.";
            return reply(req, app, render(db, settings, ctx.member_id, v), 400);
        }
        auth.sessions().remove_all_for_member(ctx.member_id, get_cookie(req, "session"));
        audit.log(req, app, "auth.2fa_on", "member", ctx.member_id, ctx.display_name, "Turned on two-factor");
        v.flash = "Two-factor is on. Save your recovery codes now: they won't be shown again.";
        return reply(req, app, render(db, settings, ctx.member_id, v));
    });

    // POST /account/2fa/disable - with a current code (not when it's required)
    CROW_ROUTE(app, "/account/2fa/disable").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(db);
        View v;
        if (AccountSecurity::role_requires(settings.get("auth_require_2fa", "off"), ctx.role))
            v.error = "Two-factor is required for your account, so it can't be turned off.";
        else if (!sec.check_second_factor(ctx.member_id, Form(req).get("code", 40)))
            v.error = "That code didn't work.";
        if (!v.error.empty()) return reply(req, app, render(db, settings, ctx.member_id, v), 400);
        sec.disable_totp(ctx.member_id);
        audit.log(req, app, "auth.2fa_off", "member", ctx.member_id, ctx.display_name, "Turned off two-factor");
        v.flash = "Two-factor is off.";
        return reply(req, app, render(db, settings, ctx.member_id, v));
    });

    // POST /account/2fa/recovery - new set of recovery codes (needs a current code)
    CROW_ROUTE(app, "/account/2fa/recovery").methods("POST"_method)([&app, &db, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& ctx = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(db);
        View v;
        if (!sec.check_second_factor(ctx.member_id, Form(req).get("code", 40))) {
            v.error = "That code didn't work.";
            return reply(req, app, render(db, settings, ctx.member_id, v), 400);
        }
        v.codes = sec.new_recovery_codes(ctx.member_id);
        audit.log(req, app, "auth.recovery_codes", "member", ctx.member_id, ctx.display_name, "New recovery codes");
        v.flash = "New recovery codes. The old ones no longer work.";
        return reply(req, app, render(db, settings, ctx.member_id, v));
    });

    // ── Admin ──

    // GET/POST /settings/sign-in - which sign-in methods, and who must use 2FA
    auto sign_in_page = [&db, &settings, public_url](const std::string& flash) {
        crow::mustache::context ctx;
        ctx["password_on"] = settings.get("auth_password_enabled", "1") != "0";
        ctx["links_on"] = settings.get("auth_email_links", "1") != "0";
        std::string r = settings.get("auth_require_2fa", "off");
        ctx["req_off"] = r != "staff" && r != "everyone";
        ctx["req_staff"] = r == "staff";
        ctx["req_everyone"] = r == "everyone";
        auto n = [&db](const char* sql) { auto st = db.prepare(sql); return st.step() ? st.col_int(0) : 0; };
        ctx["members"] = n("SELECT COUNT(*) FROM members");
        ctx["with_password"] = n("SELECT COUNT(*) FROM members WHERE password_hash <> ''");
        ctx["with_2fa"] = n("SELECT COUNT(*) FROM members WHERE totp_enabled_at IS NOT NULL");
        ctx["staff_without_2fa"] = n("SELECT COUNT(*) FROM members WHERE role <> 'member' AND totp_enabled_at IS NULL");
        ctx["with_email"] = n("SELECT COUNT(*) FROM members WHERE COALESCE(email,'') <> ''");
        int64_t shared = n("SELECT COUNT(*) FROM (SELECT lower(email) FROM members WHERE COALESCE(email,'') <> '' "
                           "GROUP BY lower(email) HAVING COUNT(*) > 1)");
        ctx["shared_emails"] = shared;
        ctx["has_shared"] = shared > 0;
        ctx["no_public_url"] = public_url.empty();
        if (!flash.empty()) ctx["flash"] = flash;
        return crow::mustache::load("settings/_sign_in.html").render(ctx).dump();
    };
    CROW_ROUTE(app, "/settings/sign-in")([&app, sign_in_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, sign_in_page(""), "Sign-in", "active_sign_in");
    });
    CROW_ROUTE(app, "/settings/sign-in").methods("POST"_method)([&app, &settings, &audit, sign_in_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        Form f(req);
        std::string r = f.get("require_2fa", 10);
        if (r != "staff" && r != "everyone") r = "off";
        settings.set("auth_password_enabled", f.get("password", 2) == "1" ? "1" : "0");
        settings.set("auth_email_links", f.get("links", 2) == "1" ? "1" : "0");
        settings.set("auth_require_2fa", r);
        audit.log(req, app, "settings.update", "settings", 0, "Sign-in",
                  std::string("Passwords ") + (f.get("password", 2) == "1" ? "on" : "off") + ", email links " +
                  (f.get("links", 2) == "1" ? "on" : "off") + ", 2FA required: " + r);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(sign_in_page("Saved."));
        return res;
    });

    // POST /members/<id>/password-link[?email=1] - a one-time "set your password" link (24 h)
    CROW_ROUTE(app, "/members/<int>/password-link").methods("POST"_method)(
        [&app, &db, notifier, &audit, public_url](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        AccountSecurity sec(db);
        auto a = sec.by_id(id);
        if (!a) { res.code = 404; return res; }
        if (a->email.empty() || sec.email_taken(a->email, id)) {
            res.code = 400;
            res.write("<span class=\"text-xs text-red-600\">They need an email address that no other member uses first.</span>");
            return res;
        }
        auto& me = app.get_context<AuthMiddleware>(req).auth;
        std::string link = base_url(public_url, req) + "/auth/reset/" + sec.create_reset_token(id, me.member_id, 24);
        bool send = req.url_params.get("email") && notifier && notifier->mailer() && notifier->mailer()->enabled();
        if (send)
            notifier->send_email_template(a->id, a->email, a->display_name, "email.password_link", {{"link", link}, {"email", a->email}});
        audit.log(req, app, "auth.password_link", "member", id, a->display_name, send ? "Emailed a set-password link" : "Made a set-password link");
        crow::mustache::context ctx;
        ctx["link"] = link;
        ctx["sent"] = send;
        ctx["email"] = a->email;
        res.write(crow::mustache::load("members/_password_link.html").render(ctx).dump());
        return res;
    });

    // POST /members/<id>/reset-2fa - for someone who lost their phone and codes
    CROW_ROUTE(app, "/members/<int>/reset-2fa").methods("POST"_method)([&app, &db, &auth, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        AccountSecurity sec(db);
        auto a = sec.by_id(id);
        if (!a) { res.code = 404; return res; }
        if (!a->two_factor) { res.write("<span class=\"text-xs text-gray-500\">Two-factor isn't on for them.</span>"); return res; }
        sec.disable_totp(id);
        auth.sessions().remove_all_for_member(id);
        audit.log(req, app, "auth.2fa_reset", "member", id, a->display_name, "Admin turned off their two-factor");
        res.write("<span class=\"text-xs text-green-700\">Two-factor turned off and they were signed out. They can set it up again after signing in.</span>");
        return res;
    });
}
