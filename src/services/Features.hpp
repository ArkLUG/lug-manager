#pragma once
#include "repositories/SettingsRepository.hpp"
#include <crow/mustache.h>
#include <map>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

// Optional parts of the app an admin can switch off (Settings > Features).
// Stored as lug_settings "feature_<key>" = "0"/"1"; everything is on unless
// turned off. Turning a feature off hides it (sidebar, panels, form fields),
// makes its pages return 404, and stops its background jobs - its data is
// kept, so turning it back on restores everything. The /api/v1 API is not
// affected.
class Features {
public:
    struct Feature {
        const char* key;
        const char* label;
        const char* help;
        const char* paths;   // regex of page URLs owned by the feature ("" = none)
        bool        def = true;          // state before an admin has chosen
        const char* setting = nullptr;   // settings key if not "feature_<key>"
    };

    static const std::vector<Feature>& all() {
        static const std::vector<Feature> f = {
            {"chapters",   "Chapters", "Sub-groups with their own leads, Discord channels, calendars and chapter-only events/meetings. Off for a LUG without chapters.",
                           R"(^/(chapters(/.*)?|calendar/chapter/.*)$)"},
            {"dues",       "Membership dues", "Dues status, payment ledger, automatic expiry and renewal reminders.",
                           R"(^/(members/\d+/dues(/.*)?|treasury/dues)$)"},
            {"perks",      "Perk levels", "Attendance tiers with Discord role rewards and the 'Almost there' list.",
                           R"(^/(perks(/.*)?|api/perks/.*)$)"},
            {"rsvps",      "Event RSVPs", "RSVPs with capacity, waitlist and reminder messages.",
                           R"(^/events/\d+/rsvp(/.*)?$)"},
            {"displays",   "Display requests", "Members request table space for their MOCs at shows.",
                           R"(^/events/\d+/displays(/.*|\.csv)?$)"},
            {"shifts",     "Volunteer shifts", "Sign-up shifts (set-up, booth, tear-down) on events.",
                           R"(^/events/\d+/shifts(/.*)?$)"},
            {"photos",     "Event photos", "Members add photos to events.",
                           R"(^/events/\d+/photos(/.*)?$)"},
            {"challenges", "Build challenges", "Themed build contests with entries and voting.",
                           R"(^/challenges(/.*)?$)"},
            {"inventory",  "Inventory", "LUG-owned items, storage locations, check-out and due reminders, and event pack lists.",
                           R"(^/(inventory(/.*|\.csv)?|events/\d+/pack(/.*)?)$)"},
            {"treasury",   "Treasury", "Income, expenses, receipts and yearly balance.",
                           R"(^/treasury(/.*|\.csv)?$)"},
            {"series",     "Recurring meetings", "Schedules that create meetings automatically.",
                           R"(^/meetings/series(/.*)?$)"},
            {"reports",    "Reports", "Annual report and printable per-event reports.",
                           R"(^/(reports/.*|events/\d+/report)$)"},
            {"kiosk",      "Kiosk & visitor counter", "Venue-screen check-in kiosk and the public visitor tap counter.",
                           R"(^/(events/\d+/(kiosk|counter)(/.*)?|meetings/\d+/kiosk|kiosk/.*)$)"},
            {"qr_checkin", "QR self check-in", "Members scan a QR code to check themselves in (also used by the kiosk).",
                           R"(^/(checkin/.*|(events|meetings)/\d+/generate-checkin)$)"},
            {"calendar",   "Calendar feeds", "Subscribable iCal feeds (/calendar.ics and personal private feeds).",
                           R"(^/(calendar(\.ics|/.*)|account/calendar-token)$)"},
            {"discord_reports", "Discord reports", "Post an attendance/report summary of a meeting or event to a Discord forum.",
                           R"(^/(events|meetings)/\d+/publish-report$)"},
            {"guardians",  "Young member consent", "Guardian contact, signed consent and photo-release tracking for KFOL/TFOL members.",
                           ""},
            {"digest",     "Weekly digest", "Monday-morning message to each member with the week's meetings and events, their RSVPs and shifts, and dues or borrowed items coming due. Members can opt out.",
                           "", false},
            {"public_shows", "Public shows page", "A no-login page (/shows) listing upcoming public events, with an embed and JSON feed. Set its title and intro under Settings > Discord.",
                           R"(^/shows(\.json|/.*)?$)", false, "public_shows_enabled"},
        };
        return f;
    }

    // Points the cache at this LUG's settings (startup / each test fixture).
    static void bind(SettingsRepository* settings) {
        std::lock_guard<std::mutex> l(mu());
        settings_() = settings;
        cache().clear();
    }

    static bool on(const std::string& key) {
        std::lock_guard<std::mutex> l(mu());
        auto it = cache().find(key);
        if (it != cache().end()) return it->second;
        const Feature* f = find(key);
        bool def = f ? f->def : true;
        std::string skey = f && f->setting ? f->setting : "feature_" + key;
        bool v = def;
        if (settings_()) {
            std::string s = settings_()->get(skey, "");
            if (s == "1") v = true; else if (s == "0") v = false;
        }
        cache()[key] = v;
        return v;
    }

    static void set(const std::string& key, bool enabled) {
        std::lock_guard<std::mutex> l(mu());
        const Feature* f = find(key);
        if (settings_()) settings_()->set(f && f->setting ? f->setting : "feature_" + key, enabled ? "1" : "0");
        cache()[key] = enabled;
    }

    // The switched-off feature that owns this URL path, if any.
    static const Feature* blocking(const std::string& path) {
        for (const auto& f : all()) {
            if (!*f.paths || on(f.key)) continue;
            static std::map<std::string, std::regex> compiled;
            static std::mutex cm;
            std::regex* re;
            {
                std::lock_guard<std::mutex> l(cm);
                auto it = compiled.find(f.key);
                if (it == compiled.end()) it = compiled.emplace(f.key, std::regex(f.paths)).first;
                re = &it->second;
            }
            if (std::regex_match(path, *re)) return &f;
        }
        return nullptr;
    }

    // f_<key> = on/off for templates, plus features_off ("chapters dues ...")
    // which the layout puts on <body> so `.f-<key>` elements hide via CSS.
    static void add_flags(crow::mustache::context& ctx) {
        std::string off;
        for (const auto& f : all()) {
            bool v = on(f.key);
            ctx[std::string("f_") + f.key] = v;
            if (!v) off += (off.empty() ? "" : " ") + std::string(f.key);
        }
        ctx["features_off"] = off;
    }

    // Scope radio state for the event/meeting forms. With chapters off, new
    // items default to LUG-wide and the Chapter choice is hidden (an existing
    // chapter item keeps showing it so editing doesn't silently move it).
    static void scope_flags(crow::mustache::context& ctx, const std::string& scope, bool is_new) {
        bool chapters = on("chapters");
        std::string s = scope.empty() ? "chapter" : scope;
        if (!chapters && s == "chapter" && is_new) s = "lug_wide";
        ctx["scope_chapter"] = s == "chapter";
        ctx["scope_lug_wide"] = s == "lug_wide";
        ctx["scope_non_lug"] = s == "non_lug";
        ctx["show_chapter_scope"] = chapters || s == "chapter";
    }

    // The "Chapter Lead" LUG role only makes sense with chapters. While they're
    // off it isn't offered (Moderator is the same tier); someone who already
    // has it keeps it until an admin changes it.
    static std::string normalize_role(const std::string& requested, const std::string& current = "") {
        if (requested == "chapter_lead" && current != "chapter_lead" && !on("chapters")) return "moderator";
        return requested;
    }
    static std::string role_label(const std::string& role) {
        if (role == "admin") return "Admin";
        if (role == "chapter_lead") return "Chapter Lead";
        if (role == "moderator") return "Moderator";
        return "Member";
    }

    // Chapter-scoped items can't be created while chapters are off.
    static std::string normalize_scope(const std::string& scope) {
        return scope == "chapter" && !on("chapters") ? "lug_wide" : scope;
    }

    static const Feature* find(const std::string& key) {
        for (const auto& f : all()) if (key == f.key) return &f;
        return nullptr;
    }

private:
    static std::mutex& mu() { static std::mutex m; return m; }
    static SettingsRepository*& settings_() { static SettingsRepository* s = nullptr; return s; }
    static std::map<std::string, bool>& cache() { static std::map<std::string, bool> c; return c; }
};
