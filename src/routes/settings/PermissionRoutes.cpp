#include "routes/settings/PermissionRoutes.hpp"
#include "auth/Permissions.hpp"
#include "services/Features.hpp"
#include <crow/mustache.h>
#include <map>

namespace {
// Roles shown on the page: Chapter lead only while chapters are on (or
// someone still has it).
std::vector<perms::Role> shown_roles(SettingsRepository& settings) {
    std::vector<perms::Role> out;
    for (const auto& r : perms::editable_roles()) {
        if (std::string(r.key) == "chapter_lead" && !Features::on("chapters")) {
            auto st = settings.db().prepare("SELECT COUNT(*) FROM members WHERE role='chapter_lead'");
            if (!(st.step() && st.col_int(0) > 0)) continue;
        }
        out.push_back(r);
    }
    return out;
}

std::string render(SettingsRepository& settings, const std::string& flash = "") {
    const auto roles = shown_roles(settings);
    std::map<std::string, std::set<std::string>> granted;
    for (const auto& r : roles) granted[r.key] = perms::load(settings.db(), r.key);

    crow::mustache::context ctx;
    crow::json::wvalue rs = crow::json::wvalue::list();
    for (size_t i = 0; i < roles.size(); ++i) {
        rs[i]["key"] = roles[i].key; rs[i]["label"] = roles[i].label; rs[i]["help"] = roles[i].help;
    }
    ctx["roles"] = std::move(rs);

    // Rows grouped under headings, in the order of perms::all()
    crow::json::wvalue groups = crow::json::wvalue::list();
    int gi = -1, pi = 0;
    std::string last;
    for (const auto& p : perms::all()) {
        if (last != p.group) {
            ++gi; pi = 0; last = p.group;
            groups[gi]["title"] = p.group;
            groups[gi]["perms"] = crow::json::wvalue::list();
        }
        auto& row = groups[gi]["perms"][pi++];
        row["key"] = p.key; row["label"] = p.label; row["help"] = p.help;
        row["cells"] = crow::json::wvalue::list();
        for (size_t i = 0; i < roles.size(); ++i) {
            auto& c = row["cells"][i];
            c["role"] = roles[i].key; c["role_label"] = roles[i].label; c["perm"] = p.key; c["label"] = p.label;
            c["on"] = granted[roles[i].key].count(p.key) > 0;
        }
    }
    ctx["groups"] = std::move(groups);
    if (!flash.empty()) ctx["flash"] = flash;
    return crow::mustache::load("settings/_permissions.html").render(ctx).dump();
}
}

void register_permission_routes(LugApp& app, SettingsRepository& settings, AuditService& audit) {
    CROW_ROUTE(app, "/settings/permissions")([&app, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, render(settings), "Roles and permissions", "active_permissions");
    });

    // POST /settings/permissions - grant=<role>|<permission> for every ticked box.
    // Admin only, and admin itself isn't editable, so no one can raise their
    // own access here. Each change is written to the audit log.
    CROW_ROUTE(app, "/settings/permissions").methods("POST"_method)([&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        auto q = crow::query_string("?" + req.body);
        std::map<std::string, std::set<std::string>> want;
        for (const auto& r : shown_roles(settings)) want[r.key];
        for (auto* v : q.get_list("grant", false)) {
            if (!v) continue;
            std::string s = v;
            auto bar = s.find('|');
            if (bar == std::string::npos) continue;
            std::string role = s.substr(0, bar), perm = s.substr(bar + 1);
            if (!want.count(role) || !perms::known(perm)) continue;   // unknown or hidden role: ignored
            want[role].insert(perm);
        }
        std::vector<std::string> changes;
        {
            Transaction tx(settings.db());
            for (const auto& [role, perms_wanted] : want) {
                auto had = perms::load(settings.db(), role);
                if (had == perms_wanted) continue;
                std::string diff;
                for (const auto& p : perms_wanted) if (!had.count(p)) diff += (diff.empty() ? "" : ", ") + ("+" + p);
                for (const auto& p : had) if (!perms_wanted.count(p)) diff += (diff.empty() ? "" : ", ") + ("-" + p);
                perms::save(settings.db(), role, perms_wanted);
                changes.push_back(role + ": " + diff);
            }
            tx.commit();
        }
        for (const auto& c : changes)
            audit.log(req, app, "settings.permissions", "settings", 0, "Roles and permissions", c);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render(settings, changes.empty() ? "No changes." : "Saved. It applies on everyone's next page."));
        return res;
    });
}
