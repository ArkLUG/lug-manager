#pragma once
#include "db/SqliteDatabase.hpp"
#include <crow/mustache.h>
#include <string>
#include <utility>
#include <vector>

// LEGO Fan CoLab (which replaced the LEGO Ambassador Network in 2026):
// whether this group is a Recognized LEGO Fan Community, and who its
// Community Ambassador is. Set on the Setup page (settings
// fan_colab_recognized, community_ambassador_id); shown on reports and the
// public shows page.
struct FanCoLabInfo {
    bool recognized = false;
    int64_t ambassador_id = 0;
    std::string ambassador;   // display name, "" if none / member gone
    std::string lug_name;
};

inline FanCoLabInfo fan_colab_info(SqliteDatabase& db) {
    FanCoLabInfo out;
    auto get = [&](const char* key) {
        auto st = db.prepare("SELECT value FROM lug_settings WHERE key=?");
        st.bind(1, std::string(key));
        return st.step() ? st.col_text(0) : std::string();
    };
    out.recognized = get("fan_colab_recognized") == "1";
    out.lug_name = get("lug_name");
    try { out.ambassador_id = std::stoll(get("community_ambassador_id")); } catch (...) {}
    if (out.ambassador_id > 0) {
        auto st = db.prepare("SELECT display_name FROM members WHERE id=?");
        st.bind(1, out.ambassador_id);
        if (st.step()) out.ambassador = st.col_text(0);
    }
    return out;
}

// fan_colab_recognized / community_ambassador / fan_colab_name for templates.
inline void add_fan_colab(crow::mustache::context& ctx, SqliteDatabase& db) {
    auto f = fan_colab_info(db);
    ctx["fan_colab_recognized"] = f.recognized;
    if (f.recognized && !f.ambassador.empty()) ctx["community_ambassador"] = f.ambassador;
    ctx["fan_colab_name"] = f.lug_name.empty() ? std::string("This group") : f.lug_name;
}

// Makes `new_id` (0 = nobody) the Community Ambassador from `today`
// (YYYY-MM-DD): the serving term is closed and a new one opened, so
// community_ambassador_terms keeps the history. The caller saves the
// community_ambassador_id setting. No-op when nothing changes.
inline void record_ambassador_change(SqliteDatabase& db, int64_t new_id, const std::string& today) {
    {
        std::vector<int64_t> serving;
        auto st = db.prepare("SELECT COALESCE(member_id,0) FROM community_ambassador_terms WHERE ended_on IS NULL");
        while (st.step()) serving.push_back(st.col_int(0));
        bool unchanged = new_id == 0 ? serving.empty() : serving.size() == 1 && serving[0] == new_id;
        if (unchanged) return;
    }
    {
        // A term started today and ended today was a mistake: drop it.
        auto st = db.prepare("DELETE FROM community_ambassador_terms WHERE ended_on IS NULL AND started_on >= ?");
        st.bind(1, today);
        st.step();
    }
    {
        auto st = db.prepare("UPDATE community_ambassador_terms SET ended_on=? WHERE ended_on IS NULL");
        st.bind(1, today);
        st.step();
    }
    if (new_id > 0) {
        auto st = db.prepare("INSERT INTO community_ambassador_terms (member_id, member_name, started_on) "
                             "SELECT id, display_name, ? FROM members WHERE id=?");
        st.bind(1, today);
        st.bind(2, new_id);
        st.step();
    }
}

struct AmbassadorTerm {
    int64_t id = 0;
    int64_t member_id = 0;
    std::string name, started_on, ended_on;   // ended_on "" = still serving
};

// All terms, newest first; or only those overlapping `year` when year > 0.
inline std::vector<AmbassadorTerm> ambassador_terms(SqliteDatabase& db, int year = 0) {
    std::vector<AmbassadorTerm> out;
    std::string sql = "SELECT t.id, COALESCE(t.member_id,0), COALESCE(m.display_name, t.member_name), t.started_on, "
                      "COALESCE(t.ended_on,'') FROM community_ambassador_terms t LEFT JOIN members m ON m.id = t.member_id ";
    if (year > 0) sql += "WHERE t.started_on < ?1 AND COALESCE(t.ended_on, '9999') >= ?2 ";
    sql += "ORDER BY t.started_on DESC, t.id DESC";
    auto st = db.prepare(sql);
    if (year > 0) {
        st.bind(1, std::to_string(year + 1) + "-01-01");
        st.bind(2, std::to_string(year) + "-01-01");
    }
    while (st.step()) out.push_back({st.col_int(0), st.col_int(1), st.col_text(2), st.col_text(3), st.col_text(4)});
    return out;
}

// Yearly to-do progress: {done, total} for `year`.
inline std::pair<int, int> fan_colab_todo(SqliteDatabase& db, int year) {
    auto st = db.prepare("SELECT COUNT(d.task_id), COUNT(*) FROM fan_colab_tasks t "
                         "LEFT JOIN fan_colab_task_done d ON d.task_id = t.id AND d.year = ?");
    st.bind(1, static_cast<int64_t>(year));
    if (!st.step()) return {0, 0};
    return {static_cast<int>(st.col_int(0)), static_cast<int>(st.col_int(1))};
}
