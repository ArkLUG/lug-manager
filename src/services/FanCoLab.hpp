#pragma once
#include "db/SqliteDatabase.hpp"
#include <crow/mustache.h>
#include <string>

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
