#include "routes/settings/ReminderSettingsRoutes.hpp"
#include "services/Features.hpp"
#include "utils/web/FormBody.hpp"
#include "utils/web/ParseId.hpp"
#include <crow/mustache.h>

namespace {

std::string render(SettingsRepository& settings, const std::string& flash = "") {
    crow::mustache::context ctx;
    ctx["reminders_enabled"]  = settings.get("discord_reminders_enabled") == "1";
    ctx["reminder_hours"]     = settings.get("discord_reminder_hours", "24");
    ctx["reminder_dm_rsvps"]  = settings.get("discord_reminder_dm_rsvps") == "1";
    ctx["dues_reminder_days"] = settings.get("dues_reminder_days", "0");
    ctx["discord_on"]         = Features::on("discord");
    ctx["rsvps_on"]           = Features::on("rsvps") || Features::on("shifts");
    ctx["dues_on"]            = Features::on("dues");
    if (!flash.empty()) ctx["flash"] = flash;
    return crow::mustache::load("settings/_reminders.html").render(ctx).dump();
}

} // namespace

void register_reminder_settings_routes(LugApp& app, SettingsRepository& settings, AuditService& audit) {
    CROW_ROUTE(app, "/settings/reminders")([&app, &settings](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        return html_page(req, app, render(settings), "Reminders", "active_reminders");
    });

    CROW_ROUTE(app, "/settings/reminders").methods("POST"_method)([&app, &settings, &audit](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        FormBody f(req);
        settings.set("discord_reminders_enabled", f.get("reminders_enabled") == "1" ? "1" : "0");
        settings.set("discord_reminder_dm_rsvps", f.get("reminder_dm_rsvps") == "1" ? "1" : "0");
        int hours = static_cast<int>(parse_id(f.get("reminder_hours")));
        if (hours < 1 || hours > 24 * 14) hours = 24;
        settings.set("discord_reminder_hours", std::to_string(hours));
        int dues_days = static_cast<int>(parse_id(f.get("dues_reminder_days")));
        if (dues_days < 0) dues_days = 0;
        if (dues_days > 90) dues_days = 90;
        settings.set("dues_reminder_days", std::to_string(dues_days));   // 0 = off
        audit.log(req, app, "settings.update", "settings", 0, "Reminders", "Updated reminder settings");
        return html_page(req, app, render(settings, "Saved."), "Reminders", "active_reminders");
    });
}
