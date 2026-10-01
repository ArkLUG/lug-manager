#include "routes/FeatureRoutes.hpp"
#include "services/Features.hpp"
#include <crow/mustache.h>

namespace {
std::string render(SettingsRepository&, const std::string& flash = "") {
    crow::mustache::context ctx;
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    for (const auto& f : Features::all()) {
        arr[i]["key"] = f.key; arr[i]["label"] = f.label; arr[i]["help"] = f.help;
        arr[i]["on"] = Features::on(f.key);
        ++i;
    }
    ctx["features"] = std::move(arr);
    if (!flash.empty()) ctx["flash"] = flash;
    return crow::mustache::load("settings/_features.html").render(ctx).dump();
}
}

void register_feature_routes(LugApp& app, SettingsRepository& settings, AuditService& audit) {
    CROW_ROUTE(app, "/settings/features")([&app, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        std::string body = render(settings);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(req.get_header_value("HX-Request") == "true" ? body
                  : render_in_layout(req, app, body, "Features", "active_features"));
        return res;
    });

    // POST /settings/features - ticked = on. Reloads the page so the sidebar follows.
    CROW_ROUTE(app, "/settings/features").methods("POST"_method)([&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto p = crow::query_string("?" + req.body);
        std::string changes;
        for (const auto& f : Features::all()) {
            bool want = p.get(f.key) != nullptr;
            if (want == Features::on(f.key)) continue;
            Features::set(f.key, want);
            changes += (changes.empty() ? "" : ", ") + std::string(want ? "+" : "-") + f.key;
        }
        if (!changes.empty()) {
            audit.log(req, app, "settings.features", "settings", 0, "Features", changes);
            res.add_header("HX-Refresh", "true");
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(settings, changes.empty() ? "No changes." : "Saved."));
        return res;
    });
}
