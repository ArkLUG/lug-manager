#pragma once
#include "services/Features.hpp"
#include "services/Notifier.hpp"
#include "repositories/SettingsRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "utils/LocalTime.hpp"
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Weekly digest (feature "digest", off by default): on Monday from 9:00
// (server-local time) each member gets one message with the coming week -
// meetings and events, their RSVPs and volunteer shifts, dues running out
// and LUG items due back. Sent once per week (setting digest_last_week),
// as a DM or an email via the Notifier; members can opt out (kind "digest").
// Members with nothing to read about get nothing.
class DigestService {
public:
    DigestService(SqliteDatabase& db, SettingsRepository& settings, std::shared_ptr<Notifier> notifier)
        : db_(db), settings_(settings), notifier_(std::move(notifier)) {}

    static std::string week_key(const std::tm& t) {
        char b[16];
        std::strftime(b, sizeof(b), "%G-W%V", &t);
        return b;
    }

    // Returns how many digests went out.
    int run_once(std::time_t now = std::time(nullptr)) {
        if (!Features::on("digest")) return 0;
        std::tm t = local_tm(now);
        if (t.tm_wday != 1 || t.tm_hour < 9) return 0;          // Monday, 9:00 onwards
        std::string key = week_key(t);
        if (settings_.get("digest_last_week", "") == key) return 0;
        settings_.set("digest_last_week", key);                 // first: never send twice
        std::string from = iso(now), to = iso(now + 7 * 86400);
        std::string today = from.substr(0, 10);
        auto common = upcoming(from, to);
        int sent = 0;
        for (const auto& [id, name] : recipients()) {
            std::string text = build(id, common, from, to, today);
            if (text.empty()) continue;
            if (notifier_->notify(id, "digest", "dm.digest", {{"name", name}, {"items", text}}))
                ++sent;
        }
        return sent;
    }

    // The message for one member ("" = nothing worth sending). Public for tests.
    std::string build_for(int64_t member_id, std::time_t now) {
        std::string from = iso(now), to = iso(now + 7 * 86400);
        return build(member_id, upcoming(from, to), from, to, from.substr(0, 10));
    }

private:
    struct Item { std::string kind, title, when, where; int64_t event_id = 0; };

    static std::string iso(std::time_t t) { return local_iso(t); }

    std::string when(const std::string& start) {
        std::string tz = settings_.get("lug_timezone", "America/Chicago");
        return start.size() >= 16 ? DiscordClient::friendly_time(start, tz) : start.substr(0, 10);
    }

    std::vector<Item> upcoming(const std::string& from, const std::string& to) {
        std::vector<Item> out;
        auto m = db_.prepare("SELECT title, start_time, location FROM meetings WHERE status <> 'cancelled' "
                             "AND start_time >= ? AND start_time < ? ORDER BY start_time LIMIT 10");
        m.bind(1, from); m.bind(2, to);
        while (m.step()) out.push_back({"Meeting", m.col_text(0), when(m.col_text(1)), m.col_text(2)});
        auto e = db_.prepare("SELECT title, start_time, location, id FROM lug_events WHERE status <> 'cancelled' "
                             "AND substr(start_time,1,10) >= substr(?,1,10) AND start_time < ? ORDER BY start_time LIMIT 10");
        e.bind(1, from); e.bind(2, to);
        while (e.step()) out.push_back({"Event", e.col_text(0), when(e.col_text(1)), e.col_text(2), e.col_int(3)});
        return out;
    }

    std::vector<std::pair<int64_t, std::string>> recipients() {
        std::vector<std::pair<int64_t, std::string>> out;
        auto st = db_.prepare("SELECT id, display_name FROM members WHERE COALESCE(discord_user_id,'') <> '' "
                              "OR COALESCE(email,'') <> ''");
        while (st.step()) out.emplace_back(st.col_int(0), st.col_text(1));
        return out;
    }

    std::string build(int64_t id, const std::vector<Item>& items, const std::string& from, const std::string& to,
                      const std::string& today) {
        std::string personal;
        // RSVPs
        std::string rsvp_lines;
        if (Features::on("rsvps")) {
            for (const auto& it : items) {
                if (!it.event_id) continue;
                auto st = db_.prepare("SELECT status FROM event_rsvps WHERE event_id=? AND member_id=?");
                st.bind(1, it.event_id); st.bind(2, id);
                if (st.step()) rsvp_lines += "- " + it.title + ": " + (st.col_text(0) == "going" ? "you're going" : "you're on the waitlist") + "\n";
            }
        }
        personal += rsvp_lines;
        if (Features::on("shifts")) {
            auto st = db_.prepare("SELECT s.title, s.starts_at, COALESCE(e.title,'') FROM event_shift_signups u "
                                  "JOIN event_shifts s ON s.id=u.shift_id LEFT JOIN lug_events e ON e.id=s.event_id "
                                  "WHERE u.member_id=? AND s.starts_at >= ? AND s.starts_at < ? ORDER BY s.starts_at");
            st.bind(1, id); st.bind(2, from.substr(0, 16)); st.bind(3, to.substr(0, 16));
            while (st.step()) personal += "- Volunteering: " + st.col_text(0) + " at " + st.col_text(2) + ", " + when(st.col_text(1)) + "\n";
        }
        if (Features::on("dues")) {
            auto st = db_.prepare("SELECT paid_until FROM members WHERE id=? AND is_paid=1 AND COALESCE(paid_until,'') <> '' "
                                  "AND paid_until >= ? AND paid_until <= date(?, '+30 days')");
            st.bind(1, id); st.bind(2, today); st.bind(3, today);
            if (st.step()) personal += "- Your dues run out on " + st.col_text(0) + "\n";
        }
        if (Features::on("inventory")) {
            auto st = db_.prepare("SELECT i.name, l.due_on FROM inventory_loans l JOIN inventory_items i ON i.id=l.item_id "
                                  "WHERE l.member_id=? AND l.returned_at IS NULL AND l.due_on <> '' AND l.due_on <= date(?, '+7 days')");
            st.bind(1, id); st.bind(2, today);
            while (st.step()) personal += "- Please return " + st.col_text(0) + " (due " + st.col_text(1) + ")\n";
        }
        if (items.empty() && personal.empty()) return "";
        std::string out;
        if (!items.empty()) {
            out += "\n**Coming up**\n";
            for (const auto& it : items)
                out += "- " + it.when + " - " + it.title + (it.where.empty() ? "" : " (" + it.where + ")") + "\n";
        }
        if (!personal.empty()) out += "\n**For you**\n" + personal;
        return out;
    }

    SqliteDatabase& db_;
    SettingsRepository& settings_;
    std::shared_ptr<Notifier> notifier_;
};
