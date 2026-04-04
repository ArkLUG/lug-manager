#pragma once
#include "auth/AuthService.hpp"
#include "repositories/ChapterMemberRepository.hpp"
#include <crow.h>
#include <string>

struct AuthContext {
    bool        authenticated = false;
    int64_t     member_id     = 0;
    std::string role;
    std::string discord_username;
    std::string display_name;

    bool is_admin()        const { return role == "admin"; }
    bool is_chapter_lead() const { return role == "admin" || role == "chapter_lead"; }
};

// Crow middleware that reads session cookie and populates AuthContext.
// Does NOT block unauthenticated requests - routes decide what to do.
struct AuthMiddleware {
    AuthService* auth_service = nullptr; // Set before server starts

    struct context {
        AuthContext auth;
    };

    void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
        if (!auth_service) return;

        // Extract session cookie
        std::string cookie_header = req.get_header_value("Cookie");
        std::string token;
        size_t pos = cookie_header.find("session=");
        if (pos != std::string::npos) {
            pos += 8; // len("session=")
            size_t end = cookie_header.find(';', pos);
            token = cookie_header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }

        if (token.empty()) return;

        auto session_opt = auth_service->validate_session(token);
        if (!session_opt) return;

        ctx.auth.authenticated = true;
        ctx.auth.member_id     = session_opt->member_id;
        ctx.auth.role          = session_opt->role;
        ctx.auth.display_name  = session_opt->display_name;
    }

    void after_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {}
};

// Helper: populate layout context with user info for sidebar display
template<typename App>
inline void set_layout_auth(const crow::request& req, App& app,
                            crow::mustache::context& layout_ctx) {
    auto& ctx = app.template get_context<AuthMiddleware>(req);
    layout_ctx["is_admin"]     = ctx.auth.is_admin();
    layout_ctx["display_name"] = ctx.auth.display_name;
    layout_ctx["role"]         = ctx.auth.role;
    if (!ctx.auth.display_name.empty())
        layout_ctx["display_name_initial"] = std::string(1, ctx.auth.display_name[0]);
}

// Helper: check auth in route handlers.
// Returns false and sets response if not authenticated/authorized.
template<typename App>
inline bool require_auth(const crow::request& req, crow::response& res, App& app,
                         const std::string& min_role = "member") {
    auto& ctx = app.template get_context<AuthMiddleware>(req);
    if (!ctx.auth.authenticated) {
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        if (is_htmx) {
            res.code = 401;
            res.write(R"(<div class="text-red-500 p-4">Session expired. <a href="/login" class="underline">Login again</a></div>)");
        } else {
            res.redirect("/login");
        }
        return false;
    }
    if (min_role == "admin" && !ctx.auth.is_admin()) {
        res.code = 403;
        res.write(R"({"error":"forbidden"})");
        return false;
    }
    if (min_role == "chapter_lead" && !ctx.auth.is_chapter_lead()) {
        res.code = 403;
        res.write(R"({"error":"forbidden"})");
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

    bool is_htmx = req.get_header_value("HX-Request") == "true";
    res.code = 403;
    if (is_htmx) {
        res.write(R"(<div class="bg-red-50 border border-red-200 text-red-700 px-4 py-3 rounded">You don't have permission to manage content for this chapter.</div>)");
    } else {
        res.write(R"({"error":"insufficient chapter permissions"})");
    }
    return false;
}
