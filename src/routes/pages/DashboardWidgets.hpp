#pragma once
// Dashboard sections built from the database:
//   coming_up        - the member's next meetings/events (their chapters + LUG-wide),
//                      RSVPs, volunteer shifts, items to return, dues running out
//   needs_attention  - for admins: things waiting on someone, with links
#include "db/SqliteDatabase.hpp"
#include "integrations/discord/DiscordClient.hpp"
#include "services/FanCoLab.hpp"
#include "services/Features.hpp"
#include "utils/LocalTime.hpp"
#include <crow/json.h>
#include <crow/mustache.h>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

namespace dashboard {

inline void add_coming_up(crow::mustache::context& ctx, SqliteDatabase& db, int64_t member_id, const std::string& tz) {
    const std::time_t now = std::time(nullptr);
    const std::string from = local_iso(now), to = local_iso(now + 30 * 86400), today = from.substr(0, 10);
    struct Item { std::string start, when, title, place, url, kind, note; bool going = false, waitlist = false, tentative = false; };
    std::vector<Item> items;
    const std::string in_my_chapters =
        "(scope IN ('lug_wide','non_lug') OR chapter_id IN (SELECT chapter_id FROM chapter_members WHERE member_id=?3))";
    {
        auto st = db.prepare("SELECT id, start_time, title, location FROM meetings WHERE status<>'cancelled' AND start_time>=?1 "
                             "AND start_time<?2 AND " + in_my_chapters + " ORDER BY start_time LIMIT 8");
        st.bind(1, from); st.bind(2, to); st.bind(3, member_id);
        while (st.step()) {
            Item it;
            it.start = st.col_text(1); it.title = st.col_text(2); it.place = st.col_text(3);
            it.url = "/meetings/" + std::to_string(st.col_int(0)); it.kind = "meeting";
            items.push_back(it);
        }
    }
    {
        auto st = db.prepare("SELECT e.id, e.start_time, e.title, e.location, e.status, "
                             "COALESCE((SELECT status FROM event_rsvps r WHERE r.event_id=e.id AND r.member_id=?3),'') "
                             "FROM lug_events e WHERE e.status<>'cancelled' AND substr(COALESCE(NULLIF(e.end_time,''), e.start_time),1,10)>=substr(?1,1,10) "
                             "AND e.start_time<?2 AND (e.scope IN ('lug_wide','non_lug') OR e.chapter_id IN "
                             "(SELECT chapter_id FROM chapter_members WHERE member_id=?3)) ORDER BY e.start_time LIMIT 8");
        st.bind(1, from); st.bind(2, to); st.bind(3, member_id);
        bool rsvps = Features::on("rsvps");
        while (st.step()) {
            Item it;
            it.start = st.col_text(1); it.title = st.col_text(2); it.place = st.col_text(3);
            it.url = "/events/" + std::to_string(st.col_int(0)); it.kind = "event";
            it.tentative = st.col_text(4) == "tentative";
            if (rsvps) { it.going = st.col_text(5) == "going"; it.waitlist = st.col_text(5) == "waitlist"; }
            items.push_back(it);
        }
    }
    if (Features::on("shifts")) {
        auto st = db.prepare("SELECT s.starts_at, s.title, COALESCE(e.title,''), e.id FROM event_shift_signups u "
                             "JOIN event_shifts s ON s.id=u.shift_id LEFT JOIN lug_events e ON e.id=s.event_id "
                             "WHERE u.member_id=? AND s.starts_at>=? AND s.starts_at<? ORDER BY s.starts_at LIMIT 5");
        st.bind(1, member_id); st.bind(2, from.substr(0, 16)); st.bind(3, to.substr(0, 16));
        while (st.step()) {
            Item it;
            it.start = st.col_text(0); it.title = "Volunteering: " + st.col_text(1); it.place = st.col_text(2);
            it.url = "/events/" + std::to_string(st.col_int(3)); it.kind = "shift";
            items.push_back(it);
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.start < b.start; });
    if (items.size() > 8) items.resize(8);
    crow::json::wvalue arr = crow::json::wvalue::list();
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        arr[i]["when"] = it.start.size() >= 16 ? DiscordClient::friendly_time(it.start, tz) : it.start.substr(0, 10);
        arr[i]["title"] = it.title;
        arr[i]["place"] = it.place;
        arr[i]["url"] = it.url;
        arr[i]["is_event"] = it.kind == "event";
        arr[i]["is_shift"] = it.kind == "shift";
        arr[i]["going"] = it.going;
        arr[i]["waitlist"] = it.waitlist;
        arr[i]["tentative"] = it.tentative;
    }
    ctx["coming_up"] = std::move(arr);
    ctx["has_coming_up"] = !items.empty();

    // Things only they can do something about
    crow::json::wvalue todo = crow::json::wvalue::list();
    int ti = 0;
    if (Features::on("inventory")) {
        auto st = db.prepare("SELECT i.name, l.due_on FROM inventory_loans l JOIN inventory_items i ON i.id=l.item_id "
                             "WHERE l.member_id=? AND l.returned_at IS NULL AND l.due_on<>'' AND l.due_on<=date(?, '+14 days') ORDER BY l.due_on");
        st.bind(1, member_id); st.bind(2, today);
        while (st.step()) {
            bool late = st.col_text(1) < today;
            todo[ti]["text"] = "Return " + st.col_text(0) + (late ? " (was due " : " (due ") + st.col_text(1) + ")";
            todo[ti]["urgent"] = late;
            ++ti;
        }
    }
    if (Features::on("dues")) {
        auto st = db.prepare("SELECT paid_until FROM members WHERE id=? AND is_paid=1 AND COALESCE(paid_until,'')<>'' "
                             "AND paid_until>=? AND paid_until<=date(?, '+30 days')");
        st.bind(1, member_id); st.bind(2, today); st.bind(3, today);
        if (st.step()) { todo[ti]["text"] = "Your dues run out on " + st.col_text(0); todo[ti]["urgent"] = false; ++ti; }
    }
    ctx["my_todo"] = std::move(todo);
    ctx["has_my_todo"] = ti > 0;
}

inline void add_needs_attention(crow::mustache::context& ctx, SqliteDatabase& db) {
    const std::string today = local_iso(std::time(nullptr)).substr(0, 10);
    auto count = [&](const std::string& sql) {
        auto st = db.prepare(sql);
        st.bind(1, today);
        return st.step() ? st.col_int(0) : 0;
    };
    crow::json::wvalue arr = crow::json::wvalue::list();
    int i = 0;
    auto add = [&](int64_t n, const std::string& text, const std::string& url) {
        if (n <= 0) return;
        arr[i]["count"] = n; arr[i]["text"] = text; arr[i]["url"] = url;
        ++i;
    };
    if (Features::on("displays"))
        add(count("SELECT COUNT(*) FROM event_display_requests r JOIN lug_events e ON e.id=r.event_id "
                  "WHERE r.status='pending' AND substr(COALESCE(NULLIF(e.end_time,''), e.start_time),1,10)>=?"),
            "display request(s) waiting for an answer", "/events");
    if (Features::on("inventory"))
        add(count("SELECT COUNT(*) FROM inventory_loans WHERE returned_at IS NULL AND due_on<>'' AND due_on<?"),
            "borrowed item(s) overdue", "/inventory");
    add(count("SELECT COUNT(*) FROM lug_events WHERE is_private=0 AND status<>'cancelled' "
              "AND substr(COALESCE(NULLIF(end_time,''), start_time),1,10) < ? AND start_time >= date('now','-90 days') "
              "AND public_kids+public_teens+public_adults=0"),
        "recent public event(s) without visitor numbers (they feed your reports)", "/events");
    if (Features::on("discord")) {
        auto st = db.prepare("SELECT COUNT(*) FROM pending_discord_matches WHERE resolved_at IS NULL");
        if (st.step()) add(st.col_int(0), "new Discord member(s) to match up", "/settings/discord-matches");
        auto f = db.prepare("SELECT COUNT(*) FROM chat_activity WHERE ok=0 AND created_at > datetime('now','-7 days')");
        if (f.step()) add(f.col_int(0), "Discord post(s) failed this week", "/settings/chat-activity?failed=1");
    }
    {
        auto st = db.prepare("SELECT value FROM lug_settings WHERE key='auth_require_2fa'");
        std::string req = st.step() ? st.col_text(0) : "off";
        if (req == "staff" || req == "everyone") {
            auto n = db.prepare(std::string("SELECT COUNT(*) FROM members WHERE totp_enabled_at IS NULL") +
                                (req == "staff" ? " AND role<>'member'" : ""));
            if (n.step()) add(n.col_int(0), "member(s) still need to set up two-factor", "/settings/sign-in");
        }
    }
    if (Features::on("fancolab")) {
        auto st = db.prepare("SELECT value FROM lug_settings WHERE key='fan_colab_recognized'");
        if (st.step() && st.col_text(0) == "1") {
            auto [done, total] = fan_colab_todo(db, local_year());
            add(total - done, "thing(s) left on this year's LEGO Fan CoLab to-do list", "/fancolab");
        }
    }
    ctx["attention"] = std::move(arr);
    ctx["has_attention"] = i > 0;
}

} // namespace dashboard
