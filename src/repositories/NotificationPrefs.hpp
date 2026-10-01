#pragma once
#include "db/SqliteDatabase.hpp"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Which notifications a member wants. Opt-out model: no row = wanted.
class NotificationPrefs {
public:
    struct Kind { const char* key; const char* label; const char* help; };

    static const std::vector<Kind>& kinds() {
        static const std::vector<Kind> k = {
            {"event_reminder", "Event reminders", "A Discord DM before events you RSVP'd to."},
            {"shift_reminder", "Volunteer shift reminders", "A Discord DM before a shift you signed up for."},
            {"waitlist",       "Waitlist spot opened", "A Discord DM when you move off an event waitlist."},
            {"dues_reminder",  "Dues renewal reminders", "A Discord DM shortly before your dues run out."},
        };
        return k;
    }
    static bool is_kind(const std::string& key) {
        for (const auto& k : kinds()) if (key == k.key) return true;
        return false;
    }

    explicit NotificationPrefs(SqliteDatabase& db) : db_(db) {}

    bool wants(int64_t member_id, const std::string& kind) {
        auto st = db_.prepare("SELECT 1 FROM notification_optouts WHERE member_id=? AND kind=?");
        st.bind(1, member_id); st.bind(2, kind);
        return !st.step();
    }

    std::set<std::string> optouts(int64_t member_id) {
        auto st = db_.prepare("SELECT kind FROM notification_optouts WHERE member_id=?");
        st.bind(1, member_id);
        std::set<std::string> out;
        while (st.step()) out.insert(st.col_text(0));
        return out;
    }

    void set(int64_t member_id, const std::string& kind, bool wanted) {
        if (!is_kind(kind)) return;
        auto st = db_.prepare(wanted
            ? "DELETE FROM notification_optouts WHERE member_id=? AND kind=?"
            : "INSERT OR IGNORE INTO notification_optouts (member_id, kind) VALUES (?,?)");
        st.bind(1, member_id); st.bind(2, kind);
        st.step();
    }

private:
    SqliteDatabase& db_;
};
