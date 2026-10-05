#include "utils/web/Breadcrumbs.hpp"
#include "utils/text/HtmlEscape.hpp"
#include <cctype>

const std::vector<SettingsGroup>& settings_groups() {
    static const std::vector<SettingsGroup> g = {
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

namespace {
bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

// "/events/12/photos" -> {"events", "12", "photos"}
std::vector<std::string> segments(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}
}

std::vector<Crumb> breadcrumbs_for(const std::string& raw_path, const std::string& deeper_title, bool is_admin) {
    std::string path = raw_path.substr(0, raw_path.find('?'));
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    const auto seg = segments(path);
    if (seg.empty()) return {};

    // Settings: Settings > group > page [> deeper page]. The Audit log has its
    // own place in the sidebar, so it gets no trail.
    if (is_admin) {
        const SettingsGroup* best_group = nullptr;
        const SettingsLink* best = nullptr;
        for (const auto& g : settings_groups())
            for (const auto& l : g.links) {
                const std::string href = l.href;
                // "/settings" itself only matches exactly; everything else also
                // matches the pages below it.
                bool hit = path == href || (href != "/settings" && starts_with(path, href + "/"));
                if (hit && (!best || href.size() > std::string(best->href).size())) { best = &l; best_group = &g; }
            }
        if (best && std::string(best->href) != "/audit") {
            std::vector<Crumb> t = {{"Settings", "/settings/overview"}, {best_group->title, ""}};
            if (path != best->href && !deeper_title.empty()) {
                t.push_back({best->label, best->href});
                t.push_back({deeper_title, ""});
            } else {
                t.push_back({best->label, ""});
            }
            return t;
        }
    }

    const std::string& top = seg[0];
    // Meetings and events live under Schedule.
    if (top == "meetings" || top == "events") {
        const std::string list = top == "meetings" ? "Meetings" : "Events";
        std::vector<Crumb> t = {{"Schedule", "/schedule"}};
        if (seg.size() == 1) { t.push_back({list, ""}); return t; }
        if (deeper_title.empty()) return {};
        t.push_back({list, "/" + top});
        t.push_back({deeper_title, ""});
        return t;
    }
    // Detail pages one step below a sidebar page.
    struct Section { const char* top; const char* label; };
    static const Section sections[] = {
        {"chapters", "Chapters"}, {"challenges", "Build Challenges"}, {"account", "My Account"},
    };
    for (const auto& s : sections) {
        if (top != s.top || seg.size() != 2 || deeper_title.empty()) continue;
        // Chapters and challenges by id; My Account's own sub-pages by name.
        if (top != "account" && !is_number(seg[1])) continue;
        return {{s.label, std::string("/") + s.top}, {deeper_title, ""}};
    }
    return {};
}

std::string breadcrumb_html(const std::vector<Crumb>& trail) {
    if (trail.empty()) return "";
    std::string h = "<nav aria-label=\"Breadcrumb\" class=\"mb-4 text-sm\">"
                    "<ol class=\"flex flex-wrap items-center gap-x-1.5 gap-y-1 text-gray-500\">";
    for (size_t i = 0; i < trail.size(); ++i) {
        const auto& c = trail[i];
        const bool last = i + 1 == trail.size();
        h += "<li class=\"flex items-center gap-1.5 min-w-0\">";
        if (i > 0)
            h += "<svg class=\"w-3.5 h-3.5 text-gray-400 shrink-0\" aria-hidden=\"true\" fill=\"none\" stroke=\"currentColor\" "
                 "viewBox=\"0 0 24 24\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"2\" d=\"M9 5l7 7-7 7\"/></svg>";
        if (last)
            h += "<span aria-current=\"page\" class=\"font-medium text-gray-800 truncate max-w-[16rem] sm:max-w-md\">" + html_escape(c.label) + "</span>";
        else if (!c.href.empty())
            h += "<a href=\"" + html_escape(c.href) + "\" hx-get=\"" + html_escape(c.href) +
                 "\" hx-target=\"#main-content\" hx-push-url=\"true\" hx-swap=\"innerHTML\" "
                 "class=\"hover:text-gray-800 hover:underline cursor-pointer\">" + html_escape(c.label) + "</a>";
        else
            h += "<span>" + html_escape(c.label) + "</span>";
        h += "</li>";
    }
    h += "</ol></nav>";
    return h;
}
