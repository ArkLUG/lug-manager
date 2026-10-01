#pragma once
// Repairs the Discord copies of meeting/event times written by images that
// had no time-zone data (everything built before 2026-10). Those treated the
// LUG's local times as UTC, so:
//   - scheduled events (Discord's Events tab) start 5-6 hours early for US
//     Central, and Discord's "starting now" alert fires at that wrong time;
//   - meeting announcements read "7:00 PM America" instead of "7:00 PM CDT".
// Google Calendar was always right (it's sent the local time plus the zone
// name), and event announcements / thread titles only carry dates.
//
// run({apply=false}) only reads from Discord. run({apply=true}) changes just
// the items found wrong, by editing them in place: the scheduled event's
// start/end, and the announcement's text with no mentions allowed. It never
// posts, DMs, renames threads or touches Google Calendar, so members get no
// notifications. Scheduled events that have already started can't be moved
// (Discord refuses), so they're reported and left alone.
#include "db/SqliteDatabase.hpp"
#include "integrations/DiscordClient.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <regex>
#include <string>
#include <thread>
#include <vector>

struct TimeRepairItem {
    std::string kind;        // "event" | "meeting"
    int64_t id = 0;
    std::string title, start;
    std::string part;        // "scheduled event" | "announcement" | "chapter announcement"
    std::string status;      // ok | wrong | fixed | past | missing | error
    std::string note;
};

class DiscordTimeRepair {
public:
    struct Options {
        bool apply = false;
        bool include_past_messages = false;   // also fix the text of past meetings' announcements
        int pause_ms = 400;                   // between Discord calls when changing things
    };

    DiscordTimeRepair(SqliteDatabase& db, DiscordClient& discord) : db_(db), discord_(discord) {}

    std::vector<TimeRepairItem> run(const Options& opt) {
        std::vector<TimeRepairItem> out;
        const std::string tz = discord_.get_timezone();
        const std::time_t now = std::time(nullptr);
        if (discord_.get_guild_id().empty()) return out;

        // ── Scheduled events ──
        struct Sched { std::string kind; int64_t id; std::string title, start, end, discord_id; };
        std::vector<Sched> sched;
        for (const char* table : {"lug_events", "meetings"}) {
            auto st = db_.prepare(std::string("SELECT id, title, start_time, COALESCE(end_time,''), discord_event_id FROM ") + table +
                                  " WHERE COALESCE(discord_event_id,'') <> '' AND suppress_discord = 0 AND status <> 'cancelled' "
                                  "ORDER BY start_time");
            while (st.step())
                sched.push_back({std::string(table) == "meetings" ? "meeting" : "event", st.col_int(0), st.col_text(1),
                                 st.col_text(2), st.col_text(3), st.col_text(4)});
        }
        for (const auto& s : sched) {
            if (s.start.size() < 16) continue;                // all-day: no time of day to get wrong
            TimeRepairItem it{s.kind, s.id, s.title, s.start, "scheduled event", "", ""};
            std::time_t right = DiscordClient::local_to_epoch(s.start, tz);
            if (right <= now) { it.status = "past"; it.note = "Already started or over; Discord doesn't allow moving it."; out.push_back(it); continue; }
            auto j = parse(discord_.sync_get("/guilds/" + discord_.get_guild_id() + "/scheduled-events/" + s.discord_id));
            if (!j.is_object() || !j.contains("scheduled_start_time") || !j["scheduled_start_time"].is_string()) {
                it.status = "missing";
                it.note = "Not found on Discord" + (j.is_object() && j.contains("message") ? " (" + j["message"].get<std::string>() + ")" : std::string("")) + ".";
                out.push_back(it);
                continue;
            }
            if (j.value("status", 1) != 1) { it.status = "past"; it.note = "Discord shows it as started or finished."; out.push_back(it); continue; }
            long diff = static_cast<long>(epoch(j["scheduled_start_time"].get<std::string>()) - right);
            if (std::labs(diff) < 60) { it.status = "ok"; out.push_back(it); continue; }
            it.status = "wrong";
            it.note = "Discord has it " + hours(diff) + ".";
            if (opt.apply) {
                std::string end = s.end.empty() ? s.start : s.end;
                auto r = parse(discord_.sync_patch_scheduled_event_times(s.discord_id, discord_.utc_iso(s.start), discord_.utc_iso(end)));
                if (r.is_object() && r.contains("scheduled_start_time") && r["scheduled_start_time"].is_string() &&
                    std::labs(static_cast<long>(epoch(r["scheduled_start_time"].get<std::string>()) - right)) < 60) {
                    it.status = "fixed";
                } else {
                    it.status = "error";
                    it.note += " Discord refused the change: " + r.dump().substr(0, 200);
                }
                pause(opt);
            }
            out.push_back(it);
        }

        // ── Meeting announcements: "7:00 PM America" -> "7:00 PM CDT" ──
        size_t slash = tz.find('/');
        if (slash == std::string::npos) return out;           // e.g. "UTC": nothing was mislabelled
        const std::regex bogus("(\\d{1,2}:\\d{2} [AP]M) " + tz.substr(0, slash) + "(?![A-Za-z/_])");
        struct Msg { int64_t id; std::string title, start, channel, message, part; };
        std::vector<Msg> msgs;
        {
            auto st = db_.prepare(
                "SELECT m.id, m.title, m.start_time, COALESCE(m.discord_lug_message_id,''), COALESCE(m.discord_chapter_message_id,''), "
                "COALESCE(c.discord_announcement_channel_id,'') FROM meetings m LEFT JOIN chapters c ON c.id = m.chapter_id "
                "WHERE m.suppress_discord = 0 AND (COALESCE(m.discord_lug_message_id,'') <> '' OR COALESCE(m.discord_chapter_message_id,'') <> '') "
                "ORDER BY m.start_time");
            while (st.step()) {
                if (!st.col_text(3).empty() && !discord_.get_lug_channel_id().empty())
                    msgs.push_back({st.col_int(0), st.col_text(1), st.col_text(2), discord_.get_lug_channel_id(), st.col_text(3), "announcement"});
                if (!st.col_text(4).empty() && !st.col_text(5).empty())
                    msgs.push_back({st.col_int(0), st.col_text(1), st.col_text(2), st.col_text(5), st.col_text(4), "chapter announcement"});
            }
        }
        for (const auto& m : msgs) {
            if (m.start.size() < 16) continue;
            if (!opt.include_past_messages && DiscordClient::local_to_epoch(m.start, tz) <= now) continue;
            TimeRepairItem it{"meeting", m.id, m.title, m.start, m.part, "", ""};
            auto j = parse(discord_.sync_get("/channels/" + m.channel + "/messages/" + m.message));
            if (!j.is_object() || !j.contains("content") || !j["content"].is_string()) {
                it.status = "missing";
                it.note = "Message not found on Discord.";
                out.push_back(it);
                continue;
            }
            std::string content = j["content"].get<std::string>();
            if (!std::regex_search(content, bogus)) { it.status = "ok"; out.push_back(it); continue; }
            std::string abbrev = DiscordClient::tz_abbrev(m.start, tz);
            std::string fixed = std::regex_replace(content, bogus, "$1 " + abbrev);
            it.status = "wrong";
            it.note = "Says \"" + tz.substr(0, slash) + "\" instead of \"" + abbrev + "\".";
            if (opt.apply) {
                auto r = parse(discord_.sync_edit_message_content(m.channel, m.message, fixed));
                if (r.is_object() && r.value("content", std::string()) == fixed) it.status = "fixed";
                else { it.status = "error"; it.note += " Discord refused the edit: " + r.dump().substr(0, 200); }
                pause(opt);
            }
            out.push_back(it);
        }
        return out;
    }

private:
    static nlohmann::json parse(const std::string& s) { return nlohmann::json::parse(s, nullptr, false); }

    // Discord sends "2026-10-14T00:00:00+00:00" (always UTC).
    static std::time_t epoch(const std::string& iso) {
        std::tm t{};
        if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) < 5)
            return 0;
        t.tm_year -= 1900;
        t.tm_mon -= 1;
        return timegm(&t);
    }

    static std::string hours(long diff) {
        long h = std::labs(diff) / 3600, m = (std::labs(diff) % 3600) / 60;
        std::string amount = std::to_string(h) + (h == 1 ? " hour" : " hours") + (m ? " " + std::to_string(m) + " min" : "");
        return amount + (diff < 0 ? " early" : " late");
    }

    static void pause(const Options& opt) {
        if (opt.pause_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(opt.pause_ms));
    }

    SqliteDatabase& db_;
    DiscordClient& discord_;
};
