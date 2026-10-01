#include "routes/ChatSettingsRoutes.hpp"
#include "utils/FormBody.hpp"
#include "chat/Templates.hpp"
#include "services/Notifier.hpp"
#include "utils/HtmlEscape.hpp"
#include <crow/mustache.h>

namespace {

struct Form : FormBody {
    explicit Form(const crow::request& req) : FormBody(req, 8000) {}
};

crow::response page(const crow::request& req, LugApp& app, const std::string& html, const std::string& title, int code = 200) {
    return html_page(req, app, html, title, "active_messages", code);
}

std::string render_list(SqliteDatabase& db) {
    chat::TemplateStore store(db);
    crow::mustache::context ctx;
    crow::json::wvalue groups = crow::json::wvalue::list();
    int gi = -1;
    std::string current;
    int ii = 0;
    for (const auto& d : chat::all_templates()) {
        if (current != d.group) {
            current = d.group;
            ++gi;
            ii = 0;
            groups[gi]["name"] = d.group;
            groups[gi]["items"] = crow::json::wvalue::list();
        }
        auto& it = groups[gi]["items"][ii++];
        it["key"] = d.key;
        it["label"] = d.label;
        it["help"] = d.help;
        it["customized"] = store.customized(d.key);
    }
    ctx["groups"] = std::move(groups);
    return crow::mustache::load("settings/_messages.html").render(ctx).dump();
}

std::string render_editor(SqliteDatabase& db, const chat::TemplateDef& d, const std::string& subject, const std::string& body,
                          const std::string& flash = "", const std::string& error = "") {
    crow::mustache::context ctx;
    ctx["key"] = d.key;
    ctx["label"] = d.label;
    ctx["help"] = d.help;
    ctx["group"] = d.group;
    ctx["has_subject"] = std::string(d.default_subject) != "";
    ctx["subject"] = subject;
    ctx["body"] = body;
    ctx["customized"] = chat::TemplateStore(db).customized(d.key);
    ctx["is_email_only"] = std::string(d.group) == chat::kGroupSignIn;
    ctx["is_dm"] = std::string(d.group) == chat::kGroupDirect;
    crow::json::wvalue ph = crow::json::wvalue::list();
    for (size_t i = 0; i < d.placeholders.size(); ++i) {
        ph[i]["name"] = d.placeholders[i].name;
        ph[i]["token"] = std::string("{") + d.placeholders[i].name + "}";
        ph[i]["help"] = d.placeholders[i].help;
        ph[i]["sample"] = d.placeholders[i].sample;
        bool req = false;
        for (const char* r : d.required) if (std::string(r) == d.placeholders[i].name) req = true;
        ph[i]["required"] = req;
    }
    ctx["placeholders"] = std::move(ph);
    ctx["preview"] = chat::render(body, chat::sample_values(d));
    if (std::string(d.default_subject) != "") ctx["preview_subject"] = chat::render(subject, chat::sample_values(d));
    if (!flash.empty()) ctx["flash"] = flash;
    if (!error.empty()) ctx["error"] = error;
    return crow::mustache::load("settings/_message_edit.html").render(ctx).dump();
}

// "" when the template can be saved, else what's wrong.
std::string check(const chat::TemplateDef& d, const std::string& subject, const std::string& body) {
    if (body.find_first_not_of(" \t\r\n") == std::string::npos) return "The message can't be empty.";
    for (const char* r : d.required)
        if (body.find(std::string("{") + r + "}") == std::string::npos)
            return std::string("Keep the {") + r + "} placeholder - the message doesn't work without it.";
    auto bad = chat::unknown_placeholders(d, body + "\n" + subject);
    if (!bad.empty()) {
        std::string list;
        for (const auto& b : bad) list += (list.empty() ? "" : ", ") + std::string("{") + b + "}";
        return "Unknown placeholder" + std::string(bad.size() > 1 ? "s" : "") + ": " + list + ". Use the ones listed below.";
    }
    if (std::string(d.default_subject) != "" && subject.find_first_not_of(" \t") == std::string::npos)
        return "The subject line can't be empty.";
    return "";
}

std::string activity_html(SqliteDatabase& db, bool failures_only) {
    crow::mustache::context ctx;
    auto st = db.prepare(std::string("SELECT id, provider, action, what, entity_type, entity_id, ok, error, channel, payload <> '', created_at "
                                     "FROM chat_activity ") + (failures_only ? "WHERE ok=0 " : "") + "ORDER BY id DESC LIMIT 200");
    crow::json::wvalue rows = crow::json::wvalue::list();
    int i = 0;
    while (st.step()) {
        auto& r = rows[i++];
        r["id"] = st.col_int(0);
        r["provider"] = st.col_text(1);
        r["action"] = st.col_text(2);
        r["what"] = st.col_text(3);
        std::string et = st.col_text(4);
        int64_t eid = st.col_int(5);
        if ((et == "event" || et == "meeting") && eid > 0) {
            r["link"] = "/" + et + "s/" + std::to_string(eid);
            r["entity"] = et + " #" + std::to_string(eid);
        } else if (!et.empty() && eid > 0) {
            r["entity"] = et + " #" + std::to_string(eid);
        }
        r["ok"] = st.col_int(6) != 0;
        r["skipped"] = st.col_text(2) == "skip";
        r["error"] = st.col_text(7);
        r["can_retry"] = st.col_int(9) != 0;
        r["when"] = st.col_text(10);
    }
    ctx["rows"] = std::move(rows);
    ctx["has_rows"] = i > 0;
    ctx["failures_only"] = failures_only;
    return crow::mustache::load("settings/_chat_activity.html").render(ctx).dump();
}

} // namespace

void register_chat_settings_routes(LugApp& app, SqliteDatabase& db, std::shared_ptr<chat::ChatHub> hub,
                                   std::shared_ptr<Notifier> notifier, AuditService& audit) {

    // GET /settings/messages - every message LUG Manager sends, grouped
    CROW_ROUTE(app, "/settings/messages")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return page(req, app, render_list(db), "Message wording");
    });

    // GET /settings/messages/<key> - edit one
    CROW_ROUTE(app, "/settings/messages/<string>")([&app, &db](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const chat::TemplateDef* d = chat::find_template(key);
        if (!d) { res.code = 404; return res; }
        chat::TemplateStore st(db);
        return page(req, app, render_editor(db, *d, st.subject(key), st.body(key)), d->label);
    });

    // POST /settings/messages/<key>/preview - live preview while typing
    CROW_ROUTE(app, "/settings/messages/<string>/preview").methods("POST"_method)([&app](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const chat::TemplateDef* d = chat::find_template(key);
        if (!d) { res.code = 404; return res; }
        Form f(req);
        std::string body = f.get("body"), subject = f.get("subject", 300);
        crow::mustache::context ctx;
        ctx["preview"] = chat::render(body, chat::sample_values(*d));
        if (std::string(d->default_subject) != "") ctx["preview_subject"] = chat::render(subject, chat::sample_values(*d));
        std::string err = check(*d, subject, body);
        if (!err.empty()) ctx["warning"] = err;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(crow::mustache::load("settings/_message_preview.html").render(ctx).dump());
        return res;
    });

    // POST /settings/messages/<key> - save
    CROW_ROUTE(app, "/settings/messages/<string>").methods("POST"_method)([&app, &db, &audit](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const chat::TemplateDef* d = chat::find_template(key);
        if (!d) { res.code = 404; return res; }
        Form f(req);
        std::string body = f.get("body"), subject = f.get("subject", 300);
        for (std::string* s : {&body, &subject}) {     // browsers send CRLF
            for (size_t p; (p = s->find('\r')) != std::string::npos;) s->erase(p, 1);
        }
        std::string err = check(*d, subject, body);
        if (!err.empty()) return page(req, app, render_editor(db, *d, subject, body, "", err), d->label, 400);
        chat::TemplateStore(db).save(key, subject, body);
        audit.log(req, app, "settings.message_template", "settings", 0, d->label, "Saved message wording");
        return page(req, app, render_editor(db, *d, subject, body, "Saved."), d->label);
    });

    // POST /settings/messages/<key>/reset - back to the built-in wording
    CROW_ROUTE(app, "/settings/messages/<string>/reset").methods("POST"_method)([&app, &db, &audit](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const chat::TemplateDef* d = chat::find_template(key);
        if (!d) { res.code = 404; return res; }
        chat::TemplateStore(db).reset(key);
        audit.log(req, app, "settings.message_template", "settings", 0, d->label, "Reset message wording");
        return page(req, app, render_editor(db, *d, d->default_subject, d->default_body, "Back to the built-in wording."), d->label);
    });

    // POST /settings/messages/<key>/test - send yourself an example (chat DM or email)
    CROW_ROUTE(app, "/settings/messages/<string>/test").methods("POST"_method)(
        [&app, &db, hub, notifier](const crow::request& req, const std::string& key) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        const chat::TemplateDef* d = chat::find_template(key);
        if (!d) { res.code = 404; return res; }
        auto& me = app.get_context<AuthMiddleware>(req).auth;
        chat::Values v = chat::sample_values(*d);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        // Example values, sent to the admin only: a DM if they have a chat account, else email.
        bool sent = false;
        std::string how;
        if (std::string(d->group) != chat::kGroupSignIn && hub && hub->can_dm(me.member_id)) {
            sent = hub->direct_message(me.member_id, key, v);
            how = "a direct message";
        }
        if (!sent && notifier && notifier->email_enabled()) {
            auto st = db.prepare("SELECT COALESCE(email,''), display_name FROM members WHERE id=?");
            st.bind(1, me.member_id);
            if (st.step() && !st.col_text(0).empty()) {
                notifier->send_email_template(me.member_id, st.col_text(0), st.col_text(1), key, v);
                sent = true;
                how = "an email to " + st.col_text(0);
            }
        }
        res.write(sent ? "<span class=\"text-green-700 text-sm\">Sent you " + html_escape(how) + " with example values.</span>"
                       : "<span class=\"text-red-600 text-sm\">Couldn't send a test: you need Discord DMs or email set up for your account.</span>");
        return res;
    });

    // GET /settings/chat-activity[?failed=1] - what was posted, and what failed
    CROW_ROUTE(app, "/settings/chat-activity")([&app, &db](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::response out = page(req, app, activity_html(db, req.url_params.get("failed") != nullptr), "Chat activity");
        return out;
    });

    // POST /settings/chat-activity/<id>/retry - send a failed standalone post again
    CROW_ROUTE(app, "/settings/chat-activity/<int>/retry").methods("POST"_method)(
        [&app, &db, hub, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        bool ok = hub && hub->retry(id);
        audit.log(req, app, "chat.retry", "settings", id, "", ok ? "Sent" : "Failed again");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(ok ? "<span class=\"text-green-700 text-xs\">Sent.</span>" : "<span class=\"text-red-600 text-xs\">Failed again - see the log.</span>");
        (void)db;
        return res;
    });
}
