#pragma once
#include "auth/AuthService.hpp"
#include "auth/SessionStore.hpp"
#include "repositories/members/ChapterMemberRepository.hpp"
#include "repositories/admin/SettingsRepository.hpp"
#include "services/Features.hpp"
#include "services/Palettes.hpp"
#include "utils/web/AssetVersion.hpp"
#include <crow.h>
#include <string>

// True for requests made by htmx (they want a fragment, not a whole page).
inline bool is_htmx(const crow::request& req) { return req.get_header_value("HX-Request") == "true"; }

// Returns the value of cookie `name` from the Cookie header, or "" if absent.
// Matches whole cookie names only (a bare find("session=") also matched e.g.
// "xsession=").
inline std::string get_cookie(const crow::request& req, const std::string& name) {
    const std::string header = req.get_header_value("Cookie");
    size_t pos = 0;
    while (pos < header.size()) {
        size_t end = header.find(';', pos);
        if (end == std::string::npos) end = header.size();
        size_t start = header.find_first_not_of(' ', pos);
        if (start < end) {
            size_t eq = header.find('=', start);
            if (eq < end && header.compare(start, eq - start, name) == 0)
                return header.substr(eq + 1, end - eq - 1);
        }
        pos = end + 1;
    }
    return "";
}

struct AuthContext {
    bool        authenticated = false;
    int64_t     member_id     = 0;
    std::string role;
    std::string discord_username;
    std::string display_name;
    bool        treasurer     = false;
    bool        must_setup_2fa = false;   // two-factor required but not set up (see before_handle)

    bool is_admin()        const { return role == "admin"; }
    // Treasury pages and recording dues there: admins and the treasurer(s).
    bool can_treasury()    const { return is_admin() || treasurer; }
    bool is_moderator()    const { return role == "moderator"; }
    // Same privilege tier as chapter_lead - moderator is granted manually
    // (like chapter_lead), not via Discord role-mapping sync.
    bool is_chapter_lead() const { return role == "admin" || role == "chapter_lead" || role == "moderator"; }
};

// Crow middleware that reads session cookie and populates AuthContext.
// Does NOT block unauthenticated requests - routes decide what to do.
struct AuthMiddleware {
    AuthService*        auth_service = nullptr; // Set before server starts
    // Read by set_layout_auth() below to show the admin-uploaded logo (if any)
    // in the sidebar/favicon on every page, without threading a
    // SettingsRepository& through the ~20 call sites that build a layout
    // context - same "nullable pointer set once at startup" pattern as
    // auth_service above and ApiKeyMiddleware::api_keys.
    SettingsRepository* settings     = nullptr; // Set before server starts

    struct context {
        AuthContext auth;
        std::string csp_nonce; // per-request nonce for inline <script> (see after_handle)
        std::string renewed_session;   // the session was extended: re-send its cookie (after_handle)
    };

    void before_handle(crow::request& req, crow::response& res, context& ctx) {
        ctx.csp_nonce = SessionStore::generate_token().substr(0, 32);
        // Pages of a switched-off feature don't exist (Settings > Features).
        if (Features::blocking(req.url)) {
            res.code = 404;
            res.end();
            return;
        }
        if (!auth_service) return;

        std::string token = get_cookie(req, "session");

        if (token.empty()) return;

        // Cross-site request forgery: SameSite=Lax keeps the session cookie off
        // POSTs from other sites, but not from other subdomains of the same
        // site. Browsers say where a request came from in Sec-Fetch-Site; a
        // signed-in change must come from our own pages.
        if (is_cross_site_change(req)) {
            res.code = 403;
            res.write("This request came from another site.");
            res.end();
            return;
        }

        auto session_opt = auth_service->validate_session(token);
        if (!session_opt) return;

        ctx.auth.authenticated = true;
        ctx.auth.member_id     = session_opt->member_id;
        ctx.auth.role          = session_opt->role;
        ctx.auth.display_name  = session_opt->display_name;
        ctx.auth.treasurer     = session_opt->treasurer;
        if (session_opt->renewed) ctx.renewed_session = token;

        // Two-factor required for this member but not set up yet: everything
        // except the setup page (and signing out) sends them there.
        if (settings) {
            std::string need = settings->get("auth_require_2fa", "off");
            bool required = need == "everyone" || (need == "staff" && ctx.auth.role != "member");
            if (required && !has_two_factor(ctx.auth.member_id) && !allowed_without_2fa(req.url)) {
                ctx.auth.must_setup_2fa = true;
                if (is_htmx(req)) {
                    res.code = 200;
                    res.add_header("HX-Redirect", "/account/security");
                } else {
                    res.code = 303;
                    res.add_header("Location", "/account/security");
                }
                res.end();
            }
        }
    }

    static bool is_cross_site_change(const crow::request& req) {
        if (req.method == crow::HTTPMethod::GET || req.method == crow::HTTPMethod::HEAD ||
            req.method == crow::HTTPMethod::OPTIONS)
            return false;
        const std::string site = req.get_header_value("Sec-Fetch-Site");
        return site == "cross-site" || site == "same-site";
    }

    bool has_two_factor(int64_t member_id) {
        auto st = settings->db().prepare("SELECT totp_enabled_at IS NOT NULL FROM members WHERE id=?");
        st.bind(1, member_id);
        return st.step() && st.col_int(0) != 0;
    }
    static bool allowed_without_2fa(const std::string& url) {
        for (const char* p : {"/account/security", "/account/2fa/", "/auth/", "/static/", "/branding/", "/manifest.webmanifest", "/sw.js"})
            if (url.rfind(p, 0) == 0) return true;
        return url == "/login" || url == "/favicon.ico";
    }

    // Baseline security headers on every response (this middleware runs for
    // all routes), plus a nonce-based Content-Security-Policy: scripts must
    // come from this origin or carry this request's nonce. Every inline
    // <script> we render gets the nonce stamped on here; inline event
    // handlers (onclick=...) are refused by the browser, so templates use
    // data-action attributes wired up by /static/app.js instead. htmx is told
    // the nonce (htmx-config meta) so scripts inside swapped-in fragments run.
    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        // Crow's redirect() is a 307, which makes browsers repeat a form POST
        // against the target (e.g. Sign Out -> POST /login -> 405). After a
        // non-GET request the follow-up must be a GET: 303 See Other.
        if (res.code == 307 && req.method != crow::HTTPMethod::Get && req.method != crow::HTTPMethod::Head)
            res.code = 303;
        auto set_default = [&](const char* name, const char* value) {
            if (res.get_header_value(name).empty()) res.set_header(name, value);
        };
        set_default("X-Content-Type-Options", "nosniff");
        set_default("X-Frame-Options", "DENY");
        set_default("Referrer-Policy", "strict-origin-when-cross-origin");
        // A session in use is extended (SessionStore::find); the browser's
        // cookie gets the same new lifetime, unless this response signs out.
        if (!ctx.renewed_session.empty() && res.get_header_value("Set-Cookie").find("session=") == std::string::npos) {
            const bool https = req.get_header_value("X-Forwarded-Proto") == "https";
            res.add_header("Set-Cookie", "session=" + ctx.renewed_session + "; HttpOnly; Path=/; Max-Age=" +
                           std::to_string(SessionStore::kSessionHours * 3600) + "; SameSite=Lax" + (https ? "; Secure" : ""));
        }

        const std::string& nonce = ctx.csp_nonce;
        if (!nonce.empty() && res.get_header_value("Content-Type").find("text/html") != std::string::npos &&
            !res.body.empty()) {
            const std::string tagged = "<script nonce=\"" + nonce + "\">";
            for (size_t p = 0; (p = res.body.find("<script>", p)) != std::string::npos; p += tagged.size())
                res.body.replace(p, 8, tagged);
            // Pages that don't pick a colour theme themselves (sign-in, public
            // pages, check-in) get the LUG's default (palettes.css).
            const std::string bare = "<html lang=\"en\">";
            size_t html = res.body.find(bare);
            if (html != std::string::npos && html < 200 && settings)
                res.body.replace(html, bare.size(), "<html lang=\"en\" data-palette=\"" +
                                 palettes::resolve("", settings->get("default_palette", "classic")) + "\">");
            size_t head = res.body.find("<head>");
            if (head != std::string::npos)
                res.body.insert(head + 6, "<meta name=\"htmx-config\" content='{\"inlineScriptNonce\":\"" + nonce + "\"}'>");
        }
        if (res.get_header_value("Content-Security-Policy").empty())
            res.set_header("Content-Security-Policy",
                "default-src 'self'; "
                // cdn.tailwindcss.com: only the dev fallback when tailwind.min.css isn't built.
                "script-src 'self' 'nonce-" + nonce + "' https://cdn.tailwindcss.com; "
                "style-src 'self' 'unsafe-inline' https://maxcdn.bootstrapcdn.com; "
                "font-src 'self' data: https://maxcdn.bootstrapcdn.com; "
                "img-src 'self' data: blob: https:; "
                "connect-src 'self'; "
                "worker-src 'self'; manifest-src 'self'; "
                "object-src 'none'; base-uri 'self'; form-action 'self'; frame-ancestors 'none'");
    }
};

// Helper: populate layout context with user info for sidebar display
template<typename App>
inline void set_layout_auth(const crow::request& req, App& app,
                            crow::mustache::context& layout_ctx) {
    auto& ctx = app.template get_context<AuthMiddleware>(req);
    layout_ctx["is_admin"]        = ctx.auth.is_admin();
    layout_ctx["is_chapter_lead"] = ctx.auth.is_chapter_lead();
    layout_ctx["can_treasury"]    = ctx.auth.can_treasury();
    Features::add_flags(layout_ctx);
    // Sidebar's "Chapter Tools" accordion: chapter leads/moderators who are NOT
    // admin get their own small accordion (admins already see Discord Matches
    // nested inside the "Settings" accordion, so they don't need a second copy).
    layout_ctx["is_chapter_lead_not_admin"] = ctx.auth.is_chapter_lead() && !ctx.auth.is_admin();
    layout_ctx["display_name"] = ctx.auth.display_name;
    layout_ctx["asset_v"]      = asset_version();
    layout_ctx["role"]         = ctx.auth.role;
    if (!ctx.auth.display_name.empty())
        layout_ctx["display_name_initial"] = std::string(1, ctx.auth.display_name[0]);

    // Custom logo (sidebar mark + favicon) - see BrandingRoutes.cpp. Reads
    // through the middleware's own pointer (see the struct above) rather
    // than a function parameter, so every page picks this up automatically.
    auto& mw = app.template get_middleware<AuthMiddleware>();
    layout_ctx["lug_name"] = mw.settings ? mw.settings->get("lug_name", "LEGO fan community") : "LEGO fan community";
    {
        // Colour theme: theirs, else the LUG default (services/Palettes.hpp).
        std::string mine;
        if (mw.settings && ctx.auth.member_id > 0) {
            auto st = mw.settings->db().prepare("SELECT palette FROM members WHERE id=?");
            st.bind(1, ctx.auth.member_id);
            if (st.step()) mine = st.col_text(0);
        }
        const std::string lug_default = mw.settings ? mw.settings->get("default_palette", "classic") : "classic";
        const std::string palette = palettes::resolve(mine, lug_default);
        layout_ctx["palette"] = palette;
        crow::json::wvalue opts = crow::json::wvalue::list();
        int i = 0;
        for (const auto& p : palettes::all()) {
            opts[i]["key"] = p.key; opts[i]["name"] = p.name;
            opts[i]["selected"] = palette == p.key;
            ++i;
        }
        layout_ctx["palettes"] = std::move(opts);
    }
    if (mw.settings) {
        std::string ext = mw.settings->get("branding_logo_extension", "");
        if (!ext.empty()) {
            layout_ctx["has_logo"] = true;
            layout_ctx["logo_url"] = "/branding/logo?v=" +
                mw.settings->get("branding_logo_updated_at", "0");
        }
    }
}

// Renders `content` (an already-rendered page fragment) inside layout.html
// with the sidebar/auth context filled in. `active_key` is the sidebar flag
// to highlight, e.g. "active_members".
template<typename App>
inline std::string render_in_layout(const crow::request& req, App& app,
                                    const std::string& content,
                                    const std::string& page_title,
                                    const std::string& active_key) {
    crow::mustache::context layout_ctx;
    layout_ctx["content"]    = content;
    layout_ctx["page_title"] = page_title;
    layout_ctx[active_key]   = true;
    // Meetings and events live under Schedule in the sidebar
    if (active_key == "active_meetings" || active_key == "active_events") layout_ctx["active_schedule"] = true;
    set_layout_auth(req, app, layout_ctx);
    return crow::mustache::load("layout.html").render(layout_ctx).dump();
}

// An HTML response: the fragment for htmx requests, else the whole page.
template<typename App>
inline crow::response html_page(const crow::request& req, App& app, const std::string& content,
                                const std::string& page_title, const std::string& active_key, int code = 200) {
    crow::response res;
    res.code = code;
    res.add_header("Content-Type", "text/html; charset=utf-8");
    res.write(is_htmx(req) ? content : render_in_layout(req, app, content, page_title, active_key));
    return res;
}

// Helper: check auth in route handlers.
// Returns false and sets response if not authenticated/authorized.
template<typename App>
inline bool require_auth(const crow::request& req, crow::response& res, App& app,
                         const std::string& min_role = "member") {
    auto& ctx = app.template get_context<AuthMiddleware>(req);
    if (!ctx.auth.authenticated) {
        const bool htmx = is_htmx(req);
        if (htmx) {
            res.code = 401;
            res.write(R"(<div class="text-red-500 p-4">Session expired. <a href="/login" class="underline">Login again</a></div>)");
        } else {
            res.redirect("/login");
        }
        return false;
    }
    bool allowed = (min_role == "admin")        ? ctx.auth.is_admin()
                 : (min_role == "chapter_lead") ? ctx.auth.is_chapter_lead()
                 : (min_role == "treasurer")    ? ctx.auth.can_treasury()
                 : true;
    if (!allowed) {
        res.code = 403;
        if (is_htmx(req)) {
            res.add_header("Content-Type", "text/html; charset=utf-8");
            res.write(R"(<div class="text-red-500 text-sm p-2">You don't have permission to do that.</div>)");
        } else {
            res.add_header("Content-Type", "application/json");
            res.write(R"({"error":"forbidden"})");
        }
        return false;
    }
    return true;
}

// Chapter role rank: lead(2) > event_manager(1) > member(0)
inline int chapter_role_rank(const std::string& r) {
    if (r == "lead")          return 2;
    if (r == "event_manager") return 1;
    return 0;
}

// Returns true if the user can create/manage content for a specific chapter.
// Admins and chapter_leads always pass; others need a chapter_members entry with
// sufficient role (at least min_chapter_role: "lead" or "event_manager").
template<typename App>
inline bool can_manage_chapter_content(const crow::request& req, crow::response& res,
                                        App& app, int64_t chapter_id,
                                        ChapterMemberRepository& chapter_members,
                                        const std::string& min_chapter_role = "event_manager") {
    auto& ctx = app.template get_context<AuthMiddleware>(req);
    if (!ctx.auth.authenticated) {
        res.code = 401;
        res.write(R"({"error":"not authenticated"})");
        return false;
    }
    if (ctx.auth.role == "admin") return true;

    auto role_opt = chapter_members.get_chapter_role(ctx.auth.member_id, chapter_id);
    if (role_opt && chapter_role_rank(*role_opt) >= chapter_role_rank(min_chapter_role)) {
        return true;
    }

    const bool htmx = is_htmx(req);
    res.code = 403;
    if (htmx) {
        res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">You don't have permission to manage content for this chapter.</div>)");
    } else {
        res.write(R"({"error":"insufficient chapter permissions"})");
    }
    return false;
}
