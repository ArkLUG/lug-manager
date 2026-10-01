#include "routes/SessionRoutes.hpp"
#include <crow/mustache.h>

namespace {

// "Firefox on Linux" from a User-Agent string - just enough to recognise a device.
std::string describe_ua(const std::string& ua) {
    if (ua.empty()) return "Unknown device";
    auto has = [&](const char* s) { return ua.find(s) != std::string::npos; };
    std::string browser = has("Edg/") ? "Edge" : has("OPR/") ? "Opera" : has("Firefox/") ? "Firefox"
                        : has("Chrome/") ? "Chrome" : has("Safari/") ? "Safari" : "Browser";
    std::string os = has("iPhone") || has("iPad") ? "iOS" : has("Android") ? "Android"
                   : has("Windows") ? "Windows" : has("Mac OS X") ? "macOS" : has("CrOS") ? "ChromeOS"
                   : has("Linux") ? "Linux" : "";
    return os.empty() ? browser : browser + " on " + os;
}

std::string render(const crow::request& req, LugApp& app, AuthService& auth, const std::string& flash) {
    auto& a = app.get_context<AuthMiddleware>(req).auth;
    auto list = auth.sessions().list_for_member(a.member_id, get_cookie(req, "session"));
    crow::mustache::context ctx;
    crow::json::wvalue arr = crow::json::wvalue::list();
    int others = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        arr[i]["device"]  = describe_ua(list[i].user_agent);
        arr[i]["since"]   = list[i].created_at.substr(0, 16);
        arr[i]["current"] = list[i].current;
        if (!list[i].current) ++others;
    }
    ctx["sessions"] = std::move(arr);
    ctx["has_others"] = others > 0;
    ctx["flash"] = flash;
    return crow::mustache::load("dashboard/_sessions.html").render(ctx).dump();
}

} // namespace

void register_session_routes(LugApp& app, AuthService& auth, AuditService& audit) {
    CROW_ROUTE(app, "/account/sessions")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, auth, ""));
        return res;
    });

    CROW_ROUTE(app, "/account/sessions/revoke-others").methods("POST"_method)([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        auto& a = app.get_context<AuthMiddleware>(req).auth;
        int n = auth.sessions().remove_all_for_member(a.member_id, get_cookie(req, "session"));
        audit.log(req, app, "auth.sessions_revoked", "member", a.member_id, a.display_name,
                  "Signed out " + std::to_string(n) + " other session(s)");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(req, app, auth, "Signed out of " + std::to_string(n) + " other device(s)."));
        return res;
    });

    CROW_ROUTE(app, "/members/<int>/sessions/revoke").methods("POST"_method)([&](const crow::request& req, int id) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int n = auth.sessions().remove_all_for_member(id);
        audit.log(req, app, "auth.sessions_revoked", "member", id, "",
                  "Admin signed member out of " + std::to_string(n) + " session(s)");
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write("<span class=\"text-xs text-green-700\">Signed out of " + std::to_string(n) + " session(s).</span>");
        return res;
    });
}
