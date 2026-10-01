#include "routes/settings/FeatureRoutes.hpp"
#include "services/Features.hpp"
#include <crow/mustache.h>

namespace {
int chapter_lead_count(SettingsRepository& settings) {
    auto st = settings.db().prepare("SELECT COUNT(*) FROM members WHERE role='chapter_lead'");
    return st.step() ? static_cast<int>(st.col_int(0)) : 0;
}

std::string render(SettingsRepository& settings, const std::string& flash = "") {
    crow::mustache::context ctx;
    if (!Features::on("chapters")) {
        int n = chapter_lead_count(settings);
        if (n > 0) ctx["stray_leads"] = n;
    }
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
        return html_page(req, app, body, "Features", "active_features");
    });

    // POST /settings/features/leads-to-moderators - with chapters off, turn
    // remaining Chapter Leads into Moderators (same permissions).
    CROW_ROUTE(app, "/settings/features/leads-to-moderators").methods("POST"_method)(
        [&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        int n = 0;
        if (!Features::on("chapters")) {
            auto st = settings.db().prepare("UPDATE members SET role='moderator', role_source='manual' "
                                            "WHERE role='chapter_lead' RETURNING id, display_name");
            std::vector<std::pair<int64_t, std::string>> changed;
            while (st.step()) changed.emplace_back(st.col_int(0), st.col_text(1));
            st.reset();
            for (const auto& [id, name] : changed)
                audit.log(req, app, "member.update", "member", id, name, "role: chapter_lead -> moderator (chapters off)");
            n = static_cast<int>(changed.size());
        }
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(settings, std::to_string(n) + " Chapter Lead(s) are now Moderators."));
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
