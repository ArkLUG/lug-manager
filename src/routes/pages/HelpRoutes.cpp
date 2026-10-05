#include "routes/pages/HelpRoutes.hpp"
#include "utils/web/AssetVersion.hpp"
#include <crow.h>
#include <crow/mustache.h>

#ifndef LUG_VERSION
#define LUG_VERSION "dev"   // set by CMake (git describe, or the release tag in CI)
#endif

namespace {
const char* kRepoUrl = "https://github.com/ArkLUG/lug-manager";
}

void register_help_routes(LugApp& app, ChapterMemberRepository& chapter_members) {

    // GET /help — role-specific onboarding guide
    CROW_ROUTE(app, "/help")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;

        auto& auth = app.get_context<AuthMiddleware>(req).auth;
        bool is_admin = auth.is_admin();
        bool is_chapter_lead = auth.is_chapter_lead();

        // Check if user has event_manager role in any chapter
        bool is_event_manager = is_admin || is_chapter_lead;
        if (!is_event_manager && auth.member_id > 0) {
            auto memberships = chapter_members.find_by_member(auth.member_id);
            for (const auto& cm : memberships) {
                if (cm.chapter_role == "event_manager" || cm.chapter_role == "lead") {
                    is_event_manager = true;
                    break;
                }
            }
        }

        crow::mustache::context ctx;
        ctx["asset_v"] = asset_version();
        ctx["is_admin"] = is_admin;
        ctx["is_chapter_lead_role"] = is_chapter_lead;
        ctx["is_event_manager"] = is_event_manager;
        ctx["role_label"] = (is_admin || is_chapter_lead) ? Features::role_label(auth.role)
                          : is_event_manager ? "Event Manager"
                          : "Member";
        Features::add_flags(ctx);
        bool ch = Features::on("chapters");
        ctx["leads_and_mods"] = ch ? "Chapter leads, moderators" : "Moderators";
        ctx["lead_power_title"] = ch ? "Chapter Lead Powers" : "Moderator Powers";
        ctx["lead_guide_title"] = Features::on("chapters") ? "Chapter lead and moderator guide" : "Moderator guide";

        res.add_header("Content-Type", "text/html; charset=utf-8");
        auto content_tmpl = crow::mustache::load("help/_content.html");
        std::string content = content_tmpl.render(ctx).dump();
        return html_page(req, app, content, "Help & Getting Started", "active_help");
    });

    // GET /about-lug-manager - about the application itself: version, source
    // code and licence, credits, trademark notice
    CROW_ROUTE(app, "/about-lug-manager")([&](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app)) return res;
        crow::mustache::context ctx;
        ctx["asset_v"] = asset_version();
        ctx["version"] = LUG_VERSION;
        ctx["repo"] = kRepoUrl;
        std::string body = crow::mustache::load("help/_about_app.html").render(ctx).dump();
        return html_page(req, app, body, "About LUG Manager", "active_about_app");
    });
}
