#include "routes/settings/SettingsOverviewRoutes.hpp"
#include <crow/mustache.h>

namespace {
struct Link { const char* href; const char* label; const char* help; const char* feature; };
struct Group { const char* title; const char* feature; std::vector<Link> links; };

const std::vector<Group>& groups() {
    static const std::vector<Group> g = {
        {"Getting started", "", {
            {"/setup", "Setup checklist", "What's left to set up, step by step.", ""},
            {"/settings/features", "Features", "Switch parts of LUG Manager on or off.", ""},
        }},
        {"Your group", "", {
            {"/settings/branding", "Logo and colours", "Your club logo and the default colour theme.", ""},
            {"/settings/about", "Public pages", "The About page and the upcoming shows page anyone can see.", ""},
            {"/settings/sign-in", "Sign-in", "Passwords, emailed links, two-factor and Discord sign-in.", ""},
        }},
        {"Messages", "", {
            {"/settings/site", "Email and address", "This site's address and the email server.", ""},
            {"/settings/messages", "Message wording", "The text of every Discord post, DM and email.", ""},
            {"/settings/reminders", "Reminders", "Before meetings, events, shifts and dues running out.", ""},
        }},
        {"Members and money", "", {
            {"/settings/dues", "Dues", "Yearly amount, dues year, proration and grace period.", "f-dues"},
            {"/settings/treasury", "Treasury", "Who can see the money: totals, ledger, receipts.", "f-treasury"},
            {"/perks", "Perk levels", "Recognition levels for coming to meetings and events.", "f-perks"},
        }},
        {"Discord", "f-discord", {
            {"/settings", "Discord", "Server, channels, roles and what gets posted.", ""},
            {"/settings/roles", "Discord roles", "Which Discord roles make someone an admin, lead or member.", ""},
            {"/settings/discord-matches", "Discord matches", "Link new Discord members to member records.", ""},
            {"/settings/chat-activity", "Activity log", "Everything sent to Discord, with retry.", ""},
            {"/settings/discord-times", "Repair times", "Fix times in posts made by older versions.", ""},
        }},
        {"Calendars", "f-calendar", {
            {"/settings/calendar", "Calendar", "Time zone and the calendar feeds' name.", ""},
            {"/settings/google-calendar", "Google Calendar", "Sync meetings and events to a Google calendar.", ""},
        }},
        {"System", "", {
            {"/settings/backups", "Backups", "Daily backups and downloads.", ""},
            {"/settings/api-keys", "API keys", "Keys for scripts and other apps.", ""},
            {"/audit", "Audit log", "Who changed what, and when.", ""},
        }},
    };
    return g;
}
}

void register_settings_overview_routes(LugApp& app) {
    CROW_ROUTE(app, "/settings/overview")([&app](const crow::request& req) {
        crow::response res;
        if (!require_auth(req, res, app, "admin")) return res;
        crow::mustache::context ctx;
        crow::json::wvalue gs = crow::json::wvalue::list();
        int i = 0;
        for (const auto& g : groups()) {
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
