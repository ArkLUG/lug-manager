#include "routes/DiscordTimeRepairRoutes.hpp"
#include "services/DiscordTimeRepair.hpp"
#include <crow/mustache.h>
#include <map>

namespace {

crow::json::wvalue items_json(const std::vector<TimeRepairItem>& items, std::map<std::string, int>& counts) {
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    for (const auto& it : items) {
        arr[i]["kind"] = it.kind; arr[i]["id"] = it.id; arr[i]["title"] = it.title; arr[i]["start"] = it.start;
        arr[i]["part"] = it.part; arr[i]["status"] = it.status; arr[i]["note"] = it.note;
        arr[i]["is_" + it.status] = true;
        ++counts[it.status];
        ++i;
    }
    return arr;
}

std::string summary(const std::map<std::string, int>& c) {
    std::string out;
    for (const char* k : {"wrong", "fixed", "ok", "past", "missing", "error"}) {
        auto it = c.find(k);
        if (it != c.end()) out += (out.empty() ? "" : ", ") + std::to_string(it->second) + " " + k;
    }
    return out.empty() ? "nothing on Discord to check" : out;
}

std::string render_table(const std::vector<TimeRepairItem>& items, bool applied, bool include_past) {
    std::map<std::string, int> counts;
    crow::mustache::context ctx;
    ctx["items"] = items_json(items, counts);
    ctx["has_items"] = !items.empty();
    ctx["applied"] = applied;
    ctx["include_past"] = include_past;
    ctx["summary"] = summary(counts);
    ctx["wrong"] = counts["wrong"];
    ctx["has_wrong"] = counts["wrong"] > 0;
    return crow::mustache::load("settings/_discord_times_result.html").render(ctx).dump();
}

} // namespace

void register_discord_time_repair_routes(LugApp& app, SqliteDatabase& db, DiscordClient& discord, AuditService& audit) {

    CROW_ROUTE(app, "/settings/discord-times")([&app, &discord](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::mustache::context ctx;
        ctx["timezone"] = discord.get_timezone();
        ctx["configured"] = !discord.get_guild_id().empty();
        std::string page = crow::mustache::load("settings/_discord_times.html").render(ctx).dump();
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(req.get_header_value("HX-Request") == "true" ? page
                  : render_in_layout(req, app, page, "Discord times", "active_settings"));
        return res;
    });

    auto run = [&db, &discord](bool apply, bool include_past) {
        DiscordTimeRepair::Options o;
        o.apply = apply;
        o.include_past_messages = include_past;
        return DiscordTimeRepair(db, discord).run(o);
    };
    auto past_param = [](const crow::request& req) {
        auto q = crow::query_string("?" + req.body);
        const char* b = q.get("include_past");
        const char* u = req.url_params.get("include_past");
        return (b && std::string(b) == "1") || (u && std::string(u) == "1");
    };

    CROW_ROUTE(app, "/settings/discord-times/check").methods("POST"_method)([&app, run, past_param](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        bool past = past_param(req);
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_table(run(false, past), false, past));
        return res;
    });

    CROW_ROUTE(app, "/settings/discord-times/fix").methods("POST"_method)([&app, &audit, run, past_param](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        bool past = past_param(req);
        auto items = run(true, past);
        std::map<std::string, int> counts;
        items_json(items, counts);
        audit.log(req, app, "discord.repair_times", "settings", 0, "Discord times", summary(counts));
        res.add_header("Content-Type", "text/html; charset=utf-8");
        res.write(render_table(items, true, past));
        return res;
    });

    auto api = [&app, &audit, run](const crow::request& req, bool apply) {
        crow::response res;
        if (!require_api_scope(req, res, app, "admin")) return res;
        const char* p = req.url_params.get("include_past");
        bool past = p && std::string(p) == "1";
        auto items = run(apply, past);
        std::map<std::string, int> counts;
        crow::json::wvalue out;
        out["items"] = items_json(items, counts);
        out["applied"] = apply;
        out["summary"] = summary(counts);
        for (const auto& [k, v] : counts) out["counts"][k] = v;
        if (apply) audit.log_system("discord.repair_times", "settings", 0, "Discord times", "API: " + summary(counts));
        res.add_header("Content-Type", "application/json");
        res.write(out.dump());
        return res;
    };
    CROW_ROUTE(app, "/api/v1/maintenance/discord-times").methods("GET"_method)([api](const crow::request& req) { return api(req, false); });
    CROW_ROUTE(app, "/api/v1/maintenance/discord-times").methods("POST"_method)([api](const crow::request& req) { return api(req, true); });
}
