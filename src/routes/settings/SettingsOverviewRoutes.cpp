#include "routes/settings/SettingsOverviewRoutes.hpp"
#include "utils/web/Breadcrumbs.hpp"
#include <crow/mustache.h>

void register_settings_overview_routes(LugApp& app) {
    CROW_ROUTE(app, "/settings/overview")([&app](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::mustache::context ctx;
        crow::json::wvalue gs = crow::json::wvalue::list();
        int i = 0;
        for (const auto& g : settings_groups()) {
            gs[i]["title"] = g.title;
            gs[i]["feature_class"] = g.feature;
            int k = 0;
            gs[i]["links"] = crow::json::wvalue::list();
            for (const auto& l : g.links) {
                auto& j = gs[i]["links"][k++];
                j["href"] = l.href; j["label"] = l.label; j["help"] = l.help; j["feature_class"] = l.feature;
            }
            ++i;
        }
        ctx["groups"] = std::move(gs);
        return html_page(req, app, crow::mustache::load("settings/_overview.html").render(ctx).dump(), "Settings", "active_settings_overview");
    });
}
