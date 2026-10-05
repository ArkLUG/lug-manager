#include "routes/members/DuesRoutes.hpp"
#include "utils/LocalTime.hpp"
#include "utils/text/Money.hpp"
#include "services/members/DuesProration.hpp"
#include "services/Features.hpp"
#include "utils/web/FormBody.hpp"
#include <crow/mustache.h>
#include <algorithm>
#include <cstdio>
#include <regex>

namespace {

std::string today_ymd() {
    std::tm t = local_tm(std::time(nullptr));
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return buf;
}

bool is_ymd(const std::string& s) {
    static const std::regex re(R"(\d{4}-\d{2}-\d{2})");
    return std::regex_match(s, re);
}


dues::Config dues_config(SettingsRepository& settings) {
    dues::Config c;
    c.amount_cents = std::max<int64_t>(0, parse_cents(settings.get("dues_amount", "")));
    try { c.year_end_month = std::stoi(settings.get("dues_year_end_month", "12")); } catch (...) {}
    if (c.year_end_month < 1 || c.year_end_month > 12) c.year_end_month = 12;
    c.prorate = settings.get("dues_prorate", "1") == "1";
    try { c.grace_days = std::stoi(settings.get("dues_grace_days", "0")); } catch (...) {}
    if (c.grace_days < 0 || c.grace_days > 365) c.grace_days = 0;
    return c;
}

// The amount and "covers until" fields of the record-a-payment form, filled
// in from Settings > Dues for a payment made on `paid_on`. Re-fetched when
// the date changes (GET /members/<id>/dues/suggest).
std::string render_suggestion(const Member& m, SettingsRepository& settings, const std::string& paid_on) {
    auto sug = dues::suggest(dues_config(settings), paid_on, m.paid_until);
    crow::mustache::context ctx;
    ctx["member_id"] = m.id;
    ctx["covers_until"] = sug.covers_until;
    ctx["amount"] = sug.cents > 0 ? money(sug.cents).substr(1) : "";   // "11.00", no "$"
    ctx["explain"] = sug.explain;
    return crow::mustache::load("members/_dues_suggest.html").render(ctx).dump();
}

std::string render_panel(const crow::request& req, LugApp& app, const Member& m,
                         DuesRepository& dues, SettingsRepository& settings, const std::string& flash = "",
                         int64_t editing = 0) {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    crow::mustache::context ctx;
    ctx["member_id"]  = m.id;
    ctx["can_record"] = a.is_chapter_lead();
    ctx["can_delete"] = a.can_treasury();
    ctx["today"]      = today_ymd();
    ctx["suggestion"] = render_suggestion(m, settings, today_ymd());
    ctx["flash"]      = flash;
    ctx["is_paid"]    = m.is_paid;
    ctx["paid_until"] = m.paid_until;
    {
        const std::string g = m.is_paid ? dues::grace_until(m.paid_until, dues_config(settings).grace_days, today_ymd()) : "";
        ctx["in_grace"] = !g.empty();
        ctx["grace_until"] = g;
        ctx["lapsed"] = !m.is_paid && !m.paid_until.empty();
    }
    auto hist = dues.history(m.id);
    crow::json::wvalue arr = crow::json::wvalue::list();
    for (size_t i = 0; i < hist.size(); ++i) {
        arr[i]["id"]           = hist[i].id;
        arr[i]["member_id"]    = m.id;
        arr[i]["paid_on"]      = hist[i].paid_on;
        arr[i]["amount"]       = hist[i].amount_cents > 0 ? money(hist[i].amount_cents) : "";
        arr[i]["method"]       = hist[i].method;
        arr[i]["covers_until"] = hist[i].covers_until;
        arr[i]["note"]         = hist[i].note;
        arr[i]["recorded_by"]  = hist[i].recorded_by_name;
        arr[i]["can_delete"]   = a.can_treasury();
        arr[i]["can_edit"]     = a.can_treasury();
    }
    if (editing > 0 && a.can_treasury()) {
        if (auto p = dues.find(editing, m.id)) {
            crow::json::wvalue e;
            e["id"] = p->id; e["member_id"] = m.id;
            e["paid_on"] = p->paid_on; e["covers_until"] = p->covers_until;
            e["amount"] = p->amount_cents > 0 ? money(p->amount_cents).substr(1) : "";
            e["method"] = p->method; e["note"] = p->note;
            ctx["editing"] = std::move(e);
        }
    }
    ctx["payments"] = std::move(arr);
    ctx["has_payments"] = !hist.empty();
    return crow::mustache::load("members/_dues.html").render(ctx).dump();
}

} // namespace

void register_dues_routes(LugApp& app, MemberService& members, std::shared_ptr<DuesRepository> dues,
                          SettingsRepository& settings, AuditService& audit) {

    // GET /members/<id>/dues - payment history (chapter lead+, or the member themselves)
    CROW_ROUTE(app, "/members/<int>/dues")([&app, &members, dues, &settings](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        if (!a.is_chapter_lead() && a.member_id != id) { res.code = 403; return res; }
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *m, *dues, settings));
        return res;
    });

    // POST /members/<id>/dues - record a payment (chapter lead+)
    CROW_ROUTE(app, "/members/<int>/dues").methods("POST"_method)(
        [&app, &members, dues, &settings, &audit](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        auto params = crow::query_string("?" + req.body);
        auto gp = [&](const char* k) { const char* v = params.get(k); return v ? std::string(v) : ""; };
        std::string paid_on = gp("paid_on").empty() ? today_ymd() : gp("paid_on");
        std::string covers  = gp("covers_until");
        int64_t cents = parse_cents(gp("amount"));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!is_ymd(paid_on) || !is_ymd(covers) || cents < 0) {
            res.code = 400;
            res.write(render_panel(req, app, *m, *dues, settings, "Enter a valid paid date, 'covers until' date and amount."));
            return res;
        }
        dues->record(m->id, paid_on, cents, gp("method").substr(0, 50), covers, gp("note").substr(0, 500),
                     app.get_context<AuthMiddleware>(req).auth.member_id);
        audit.log(req, app, "member.dues_payment", "member", m->id, m->display_name,
                  "Recorded payment " + money(cents) + " covering until " + covers);
        auto fresh = members.get(id);
        res.add_header("HX-Trigger", "duesUpdated");
        res.write(render_panel(req, app, fresh ? *fresh : *m, *dues, settings, "Payment recorded."));
        return res;
    });

    // POST /members/<id>/dues/<pid>/delete - admins/treasurers; if the payment
    // set paid_until, it falls back to the latest remaining payment
    CROW_ROUTE(app, "/members/<int>/dues/<int>/delete").methods("POST"_method)(
        [&app, &members, dues, &settings, &audit](const crow::request& req, int id, int pid) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        std::string flash = "Record deleted.";
        if (auto p = dues->find(pid, id); p && dues->remove(pid, id)) {
            const std::string before = m->paid_until, after = dues->refit_paid_until(id, p->covers_until);
            audit.log(req, app, "member.dues_payment_delete", "member", m->id, m->display_name,
                      "Deleted dues payment #" + std::to_string(pid) + " (" + money(p->amount_cents) + ", paid " + p->paid_on +
                      ", covered until " + p->covers_until + ")" + (after != before ? "; paid until " + before + " -> " + after : ""));
            if (after != before) flash += " Paid until is now " + after + ".";
            else if (p->covers_until == before) flash += " Paid until is unchanged (no other payment covers it) - adjust it with Dues if needed.";
        }
        auto fresh = members.get(id);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.add_header("HX-Trigger", "duesUpdated");
        res.write(render_panel(req, app, fresh ? *fresh : *m, *dues, settings, flash));
        return res;
    });

    // GET /members/<id>/dues/<pid>/edit - the panel with that payment's edit form
    CROW_ROUTE(app, "/members/<int>/dues/<int>/edit")([&app, &members, dues, &settings](const crow::request& req, int id, int pid) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        if (!dues->find(pid, id)) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *m, *dues, settings, "", pid));
        return res;
    });

    // POST /members/<id>/dues/<pid> - change a payment (admins/treasurers)
    CROW_ROUTE(app, "/members/<int>/dues/<int>").methods("POST"_method)(
        [&app, &members, dues, &settings, &audit](const crow::request& req, int id, int pid) {
        crow::response res;
        if (!require_auth(req, res, app, "treasurer")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        auto before = dues->find(pid, id);
        if (!before) { res.code = 404; return res; }
        FormBody f(req);
        std::string paid_on = f.get("paid_on", 10), covers = f.get("covers_until", 10);
        int64_t cents = parse_cents(f.get("amount", 20));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        if (!is_ymd(paid_on) || !is_ymd(covers) || cents < 0) {
            res.code = 400;
            res.write(render_panel(req, app, *m, *dues, settings, "Enter a valid paid date, 'covers until' date and amount.", pid));
            return res;
        }
        const std::string method = f.get("method", 50), note = f.get("note", 500);
        dues->update(pid, id, paid_on, cents, method, covers, note);
        std::string changes;
        auto diff = [&](const char* what, const std::string& a, const std::string& b) {
            if (a != b) changes += std::string(changes.empty() ? "" : "; ") + what + ": " + (a.empty() ? "-" : a) + " -> " + (b.empty() ? "-" : b);
        };
        diff("paid on", before->paid_on, paid_on); diff("amount", money(before->amount_cents), money(cents));
        diff("method", before->method, method); diff("covers until", before->covers_until, covers); diff("note", before->note, note);
        auto fresh = members.get(id);
        if (fresh && fresh->paid_until != m->paid_until) diff("member paid until", m->paid_until, fresh->paid_until);
        audit.log(req, app, "member.dues_payment_edit", "member", m->id, m->display_name,
                  "Edited dues payment #" + std::to_string(pid) + (changes.empty() ? " (no changes)" : ": " + changes));
        res.add_header("HX-Trigger", "duesUpdated");
        res.write(render_panel(req, app, fresh ? *fresh : *m, *dues, settings, "Payment updated."));
        return res;
    });

    // GET /members/<id>/dues/suggest?paid_on=YYYY-MM-DD - the suggested amount
    // and "covers until" for that date (record-a-payment form)
    CROW_ROUTE(app, "/members/<int>/dues/suggest")([&app, &members, &settings](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "chapter_lead")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        const char* d = req.url_params.get("paid_on");
        std::string paid_on = d && is_ymd(d) ? d : today_ymd();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_suggestion(*m, settings, paid_on));
        return res;
    });

    // Settings > Dues
    auto settings_page = [&settings, dues](const std::string& flash) {
        auto c = dues_config(settings);
        crow::mustache::context ctx;
        ctx["amount"] = c.amount_cents > 0 ? money(c.amount_cents).substr(1) : "";
        ctx["prorate"] = c.prorate;
        ctx["grace_days"] = c.grace_days;
        static const char* names[] = {"January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December"};
        crow::json::wvalue months = crow::json::wvalue::list();
        for (int i = 0; i < 12; ++i) {
            months[i]["value"] = i + 1;
            months[i]["name"] = names[i];
            months[i]["selected"] = c.year_end_month == i + 1;
        }
        ctx["months"] = std::move(months);
        ctx["dues_on"] = Features::on("dues");
        // An example: what someone joining today would be asked for
        auto ex = dues::suggest(c, today_ymd(), "");
        ctx["example"] = c.amount_cents > 0 ? "Someone joining today: " + money(ex.cents) + " covering until " +
                                                  ex.covers_until + (ex.explain.empty() ? "" : " (" + ex.explain + ")") : "";
        // Backfill: members marked paid with no payment on record
        auto missing = dues->paid_without_payment();
        ctx["backfill_count"] = static_cast<int>(missing.size());
        ctx["has_backfill"] = !missing.empty();
        ctx["today"] = today_ymd();
        if (!flash.empty()) ctx["flash"] = flash;
        return crow::mustache::load("settings/_dues.html").render(ctx).dump();
    };
    CROW_ROUTE(app, "/settings/dues")([&app, settings_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, settings_page(""), "Dues", "active_dues_settings");
    });
    CROW_ROUTE(app, "/settings/dues").methods("POST"_method)([&app, &settings, &audit, settings_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        FormBody f(req);
        int64_t cents = parse_cents(f.get("dues_amount"));
        if (cents < 0) return html_page(req, app, settings_page("Enter the amount like 20 or 20.00."), "Dues", "active_dues_settings", 400);
        int month = 12;
        try { month = std::stoi(f.get("dues_year_end_month")); } catch (...) {}
        if (month < 1 || month > 12) month = 12;
        settings.set("dues_amount", cents > 0 ? money(cents).substr(1) : "");
        settings.set("dues_year_end_month", std::to_string(month));
        settings.set("dues_prorate", f.get("dues_prorate") == "1" ? "1" : "0");
        int grace = 0;
        try { grace = std::stoi(f.get("dues_grace_days")); } catch (...) {}
        settings.set("dues_grace_days", std::to_string(grace < 0 ? 0 : grace > 365 ? 365 : grace));
        audit.log(req, app, "settings.update", "settings", 0, "Dues", "Updated dues settings");
        return html_page(req, app, settings_page("Saved."), "Dues", "active_dues_settings");
    });

    // POST /settings/dues/backfill - record one payment for each member marked
    // paid with none on record (amount and date as entered), covering to their
    // current paid_until, so the dues totals match who's paid. Paid-until
    // dates don't change.
    CROW_ROUTE(app, "/settings/dues/backfill").methods("POST"_method)(
        [&app, &settings, &audit, dues, settings_page](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        FormBody f(req);
        int64_t cents = parse_cents(f.get("amount"));
        std::string paid_on = f.get("paid_on");
        if (cents <= 0 || !is_ymd(paid_on))
            return html_page(req, app, settings_page("Enter an amount and the date to record the payments on."),
                             "Dues", "active_dues_settings", 400);
        const std::string method = f.get("method", 50);
        const int64_t by = app.get_context<AuthMiddleware>(req).auth.member_id;
        int n = 0;
        for (const auto& m : dues->paid_without_payment()) {
            dues->record(m.member_id, paid_on, cents, method, m.paid_until, "Recorded afterwards (backfill)", by);
            ++n;
        }
        audit.log(req, app, "member.dues_backfill", "settings", 0, "Dues",
                  "Recorded " + money(cents) + " paid " + paid_on + " for " + std::to_string(n) + " members marked paid");
        return html_page(req, app, settings_page("Recorded " + std::to_string(n) + " payments."), "Dues", "active_dues_settings");
    });
}
