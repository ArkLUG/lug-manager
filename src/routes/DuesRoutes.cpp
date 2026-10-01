#include "routes/DuesRoutes.hpp"
#include "utils/LocalTime.hpp"
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

// "25", "25.5", "$25.00" -> cents; -1 if unparseable.
int64_t parse_cents(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '$'), s.end());
    if (s.empty()) return 0;
    static const std::regex re(R"((\d{1,7})(?:\.(\d{1,2}))?)");
    std::smatch m;
    if (!std::regex_match(s, m, re)) return -1;
    int64_t cents = std::stoll(m[1].str()) * 100;
    if (m[2].matched) cents += std::stoll(m[2].str().size() == 1 ? m[2].str() + "0" : m[2].str());
    return cents;
}

std::string money(int64_t cents) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "$%lld.%02lld", static_cast<long long>(cents / 100),
                  static_cast<long long>(cents % 100));
    return buf;
}

std::string render_panel(const crow::request& req, LugApp& app, const Member& m,
                         DuesRepository& dues, const std::string& flash = "") {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    crow::mustache::context ctx;
    ctx["member_id"]  = m.id;
    ctx["can_record"] = a.is_chapter_lead();
    ctx["can_delete"] = a.is_admin();
    ctx["today"]      = today_ymd();
    ctx["flash"]      = flash;
    ctx["is_paid"]    = m.is_paid;
    ctx["paid_until"] = m.paid_until;
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
        arr[i]["can_delete"]   = a.is_admin();
    }
    ctx["payments"] = std::move(arr);
    ctx["has_payments"] = !hist.empty();
    return crow::mustache::load("members/_dues.html").render(ctx).dump();
}

} // namespace

void register_dues_routes(LugApp& app, MemberService& members,
                          std::shared_ptr<DuesRepository> dues, AuditService& audit) {

    // GET /members/<id>/dues - payment history (chapter lead+, or the member themselves)
    CROW_ROUTE(app, "/members/<int>/dues")([&app, &members, dues](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        if (!a.is_chapter_lead() && a.member_id != id) { res.code = 403; return res; }
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *m, *dues));
        return res;
    });

    // POST /members/<id>/dues - record a payment (chapter lead+)
    CROW_ROUTE(app, "/members/<int>/dues").methods("POST"_method)(
        [&app, &members, dues, &audit](const crow::request& req, int id) {
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
            res.write(render_panel(req, app, *m, *dues, "Enter a valid paid date, 'covers until' date and amount."));
            return res;
        }
        dues->record(m->id, paid_on, cents, gp("method").substr(0, 50), covers, gp("note").substr(0, 500),
                     app.get_context<AuthMiddleware>(req).auth.member_id);
        audit.log(req, app, "member.dues_payment", "member", m->id, m->display_name,
                  "Recorded payment " + money(cents) + " covering until " + covers);
        auto fresh = members.get(id);
        res.add_header("HX-Trigger", "duesUpdated");
        res.write(render_panel(req, app, fresh ? *fresh : *m, *dues, "Payment recorded."));
        return res;
    });

    // POST /members/<id>/dues/<pid>/delete - admin; doesn't change paid_until
    CROW_ROUTE(app, "/members/<int>/dues/<int>/delete").methods("POST"_method)(
        [&app, &members, dues, &audit](const crow::request& req, int id, int pid) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto m = members.get(id);
        if (!m) { res.code = 404; return res; }
        if (dues->remove(pid, id))
            audit.log(req, app, "member.dues_payment_delete", "member", m->id, m->display_name,
                      "Deleted dues payment record #" + std::to_string(pid));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_panel(req, app, *m, *dues,
                               "Record deleted. Paid-until is unchanged - adjust it with Dues if needed."));
        return res;
    });
}
