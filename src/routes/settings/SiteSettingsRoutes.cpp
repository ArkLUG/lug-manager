#include "routes/settings/SiteSettingsRoutes.hpp"
#include "auth/AccountSecurity.hpp"
#include "services/SiteSettings.hpp"
#include "utils/web/FormBody.hpp"
#include <crow/mustache.h>

namespace {

std::string render(SettingsRepository& settings, const Mailer* mailer, const std::string& flash = "",
                   const std::string& error = "") {
    crow::mustache::context ctx;
    const auto locks = site::smtp_locks();
    const auto smtp = site::smtp();
    ctx["public_url"]        = site::public_url_locked() ? site::public_url() : settings.get("public_url", "");
    ctx["public_url_locked"] = site::public_url_locked();
    ctx["smtp_url"]  = smtp.url;  ctx["smtp_url_locked"]  = locks.url;
    ctx["smtp_user"] = smtp.user; ctx["smtp_user_locked"] = locks.user;
    ctx["smtp_from"] = smtp.from; ctx["smtp_from_locked"] = locks.from;
    ctx["has_password"] = locks.password;
    ctx["email_ready"]  = mailer && mailer->enabled() && !site::public_url().empty();
    ctx["no_address"]   = site::public_url().empty();
    if (!flash.empty()) ctx["flash"] = flash;
    if (!error.empty()) ctx["error"] = error;
    return crow::mustache::load("settings/_site.html").render(ctx).dump();
}

} // namespace

void register_site_settings_routes(LugApp& app, SettingsRepository& settings, std::shared_ptr<Mailer> mailer,
                                   AuditService& audit) {
    CROW_ROUTE(app, "/settings/site")([&app, &settings, mailer](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, render(settings, mailer.get()), "Email & address", "active_site");
    });

    CROW_ROUTE(app, "/settings/site").methods("POST"_method)([&app, &settings, mailer, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        FormBody f(req, 300);
        std::vector<std::string> changed;
        if (!site::public_url_locked()) {
            std::string raw = f.get("public_url");
            std::string url = site::normalize_url(raw);
            if (!raw.empty() && url.empty())
                return html_page(req, app, render(settings, mailer.get(), "",
                    "The address must be like https://lug.example.org (the site only, no path)."), "Email & address", "active_site", 400);
            if (url != settings.get("public_url", "")) { settings.set("public_url", url); changed.push_back("public address"); }
        }
        const auto locks = site::smtp_locks();
        auto save = [&](bool locked, const char* key, const char* field, const char* label) {
            if (locked) return;
            std::string v = f.get(field);
            if (v.find_first_of("\r\n") != std::string::npos) return;   // one line only
            if (v != settings.get(key, "")) { settings.set(key, v); changed.push_back(label); }
        };
        std::string smtp_url = f.get("smtp_url");
        if (!locks.url && !smtp_url.empty() && smtp_url.rfind("smtp://", 0) != 0 && smtp_url.rfind("smtps://", 0) != 0)
            return html_page(req, app, render(settings, mailer.get(), "",
                "The SMTP server must start with smtps:// or smtp:// (e.g. smtps://smtp.example.com:465)."), "Email & address", "active_site", 400);
        save(locks.url, "smtp_url", "smtp_url", "SMTP server");
        save(locks.user, "smtp_user", "smtp_user", "SMTP user");
        save(locks.from, "smtp_from", "smtp_from", "From address");
        if (mailer) mailer->reconfigure(site::smtp());
        std::string what;
        for (const auto& c : changed) what += (what.empty() ? "" : ", ") + c;
        audit.log(req, app, "settings.update", "settings", 0, "Email & address", what.empty() ? "No changes" : "Changed " + what);
        return html_page(req, app, render(settings, mailer.get(), "Saved."), "Email & address", "active_site");
    });

    // POST /settings/site/test - a test email to the admin's own (confirmed) address
    CROW_ROUTE(app, "/settings/site/test").methods("POST"_method)([&app, &settings, mailer, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto& me = app.get_context<AuthMiddleware>(req).auth;
        AccountSecurity sec(settings.db());
        auto a = sec.by_id(me.member_id);
        std::string err;
        if (!mailer || !mailer->enabled()) err = "Set up the SMTP server first.";
        else if (!a || a->email.empty() || !sec.email_confirmed(me.member_id)) err = "Your own record needs a confirmed email to send the test to.";
        if (!err.empty()) return html_page(req, app, render(settings, mailer.get(), "", err), "Email & address", "active_site", 400);
        mailer->send({a->email, "LUG Manager test email",
                      "This is a test from LUG Manager's Settings > Email & address. If you can read it, email works.\n", ""});
        audit.log(req, app, "settings.email_test", "settings", 0, "Email & address", "Test email to " + a->email);
        return html_page(req, app, render(settings, mailer.get(), "Test email sent to " + a->email +
                         ". If it doesn't arrive in a few minutes, check the spam folder and the server log."),
                         "Email & address", "active_site");
    });
}
