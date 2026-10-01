#include "routes/HelpRoutes.hpp"
#include <crow.h>
#include <crow/mustache.h>

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
        ctx["lead_guide_title"] = Features::on("chapters") ? "Chapter Lead & Moderator Guide" : "Moderator Guide";

        res.add_header("Content-Type", "text/html; charset=utf-8");
        bool is_htmx = req.get_header_value("HX-Request") == "true";
        auto content_tmpl = crow::mustache::load("help/_content.html");
        std::string content = content_tmpl.render(ctx).dump();

        if (is_htmx) {
            res.write(content);
        } else {
            res.write(render_in_layout(req, app, content, "Help & Getting Started", "active_help"));
        }
        return res;
    });
}
