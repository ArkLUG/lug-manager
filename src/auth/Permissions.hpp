#pragma once
#include "db/SqliteDatabase.hpp"
#include <set>
#include <string>
#include <vector>

// What each LUG role may do beyond being a member, set by admins on
// Settings > Roles and permissions (role_permissions table, migration 075).
//
// - Admin can always do everything; it isn't in the table and can't be edited.
// - Default-deny: a permission is granted only by a row in role_permissions.
//   A permission added later starts off for every role unless its migration
//   adds rows.
// - Nothing here lets anyone change roles, settings or permissions: those
//   stay admin-only, so no permission can be used to raise your own.
namespace perms {

struct Permission {
    const char* key;
    const char* group;
    const char* label;
    const char* help;
};

inline const std::vector<Permission>& all() {
    static const std::vector<Permission> p = {
        {"members.view_private", "Members", "See contact details and dues status",
         "Members' email, phone, address and other details they shared with staff, and who has paid."},
        {"members.edit", "Members", "Add and edit members",
         "Add members and change their details. Roles and sign-in emails stay admin-only."},
        {"members.export", "Members", "Download the members list", "The members CSV export."},
        {"discord.matches", "Members", "Link Discord accounts to members",
         "The Discord matches page: link a new Discord member to a member record or create one."},
        {"dues.record", "Money", "Record dues payments",
         "Record a payment on a member's Dues panel and mark members paid."},
        {"inventory.manage", "Inventory", "Manage the inventory",
         "Add and edit items and locations, photos, moves, loans and returns."},
        {"schedule.all_chapters", "Schedule", "Manage every meeting and event",
         "Create and edit group-wide meetings and events and those of every chapter, not only their own chapter's."},
        {"challenges.manage", "Community", "Run build challenges",
         "Create challenges, announce the winners and remove any entry."},
        {"fancolab.manage", "Community", "LEGO Fan CoLab",
         "The LEGO Fan CoLab page: recognition, Community Ambassador, to-do list and activity summary."},
        {"attendance.overview", "Reports", "See the attendance overview", "Everyone's attendance, and its CSV."},
        {"reports.annual", "Reports", "See the annual report", "The year's report of meetings, events and visitors."},
        {"audit.view", "Reports", "See the audit log", "Who changed what, and when, and its CSV."},
    };
    return p;
}

inline bool known(const std::string& key) {
    for (const auto& p : all()) if (key == p.key) return true;
    return false;
}

// Roles whose permissions can be edited (admin is fixed: everything).
struct Role { const char* key; const char* label; const char* help; };
inline const std::vector<Role>& editable_roles() {
    static const std::vector<Role> r = {
        {"moderator",    "Moderator",    "Helps run the group across all chapters."},
        {"chapter_lead", "Chapter lead", "The LUG-wide Chapter lead role (chapter roles are set on each chapter)."},
        {"member",       "Member",       "Everyone else who's signed in."},
    };
    return r;
}

inline bool editable_role(const std::string& role) {
    for (const auto& r : editable_roles()) if (role == r.key) return true;
    return false;
}

// The permissions a role has (empty for unknown roles; admin is handled by the caller).
inline std::set<std::string> load(SqliteDatabase& db, const std::string& role) {
    std::set<std::string> out;
    if (!editable_role(role)) return out;
    auto st = db.prepare("SELECT permission FROM role_permissions WHERE role=?");
    st.bind(1, role);
    while (st.step()) {
        std::string k = st.col_text(0);
        if (known(k)) out.insert(k);
    }
    return out;
}

// Replaces a role's permissions; unknown keys are dropped. Caller wraps in a transaction.
inline void save(SqliteDatabase& db, const std::string& role, const std::set<std::string>& granted) {
    if (!editable_role(role)) return;
    auto del = db.prepare("DELETE FROM role_permissions WHERE role=?");
    del.bind(1, role);
    del.step();
    for (const auto& k : granted) {
        if (!known(k)) continue;
        auto ins = db.prepare("INSERT INTO role_permissions(role, permission) VALUES(?, ?)");
        ins.bind(1, role);
        ins.bind(2, k);
        ins.step();
    }
}

// "members.view_private" -> "perm_members_view_private" (template flag)
inline std::string flag(const std::string& key) {
    std::string f = "perm_" + key;
    for (auto& c : f) if (c == '.') c = '_';
    return f;
}

} // namespace perms
