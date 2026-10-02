#include "live/LiveRoutes.hpp"
#include "live/LiveHub.hpp"
#include "middleware/AuthMiddleware.hpp"

namespace {

std::string host_of(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    return url.substr(start, url.find('/', start) - start);
}

// A browser always sends Origin on a websocket handshake. Without this check
// any other site could open one with the visitor's cookie (cross-site
// websocket hijacking); it would only hear change topics, but still.
bool origin_ok(const crow::request& req, const std::string& public_url) {
    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) return true;   // not a browser
    const std::string h = host_of(origin);
    return h == req.get_header_value("Host") || h == req.get_header_value("X-Forwarded-Host") ||
           (!public_url.empty() && h == host_of(public_url));
}

} // namespace

void register_live_routes(LugApp& app, AuthService& auth, const std::string& public_url) {
    live::Hub::get().clear();   // any connections belonged to a previous server
    live::Hub::get().set_session_check([&auth](const std::string& session) {
        return auth.validate_session(session).has_value();
    });

    CROW_WEBSOCKET_ROUTE(app, "/live")
        .onaccept([&auth, public_url](const crow::request& req, void** userdata) {
            if (!origin_ok(req, public_url)) return false;
            const std::string token = get_cookie(req, "session");
            auto session = token.empty() ? std::nullopt : auth.validate_session(token);
            if (!session) return false;
            const char* tab = req.url_params.get("tab");
            auto* v = new live::Viewer;
            v->member_id = session->member_id;
            v->staff = session->role != "member" || session->treasurer;
            v->tab = tab && live::Hub::valid_tab(tab) ? tab : "";
            v->session = token;
            *userdata = v;
            return true;
        })
        .onopen([](crow::websocket::connection& conn) {
            auto* v = static_cast<live::Viewer*>(conn.userdata());
            conn.userdata(nullptr);
            if (!v) { conn.close("not signed in"); return; }
            bool ok = live::Hub::get().add(&conn, std::move(*v));
            delete v;
            if (!ok) conn.close("too many connections");
        })
        .max_payload(4096)   // pages never send anything that big
        .onmessage([](crow::websocket::connection&, const std::string&, bool) {})   // nothing to say back
        .onerror([](crow::websocket::connection& conn, const std::string&) { live::Hub::get().remove(&conn); })
        .onclose([](crow::websocket::connection& conn, const std::string&) { live::Hub::get().remove(&conn); });
}
