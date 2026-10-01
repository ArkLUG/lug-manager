#include "services/SeriesService.hpp"
#include <cstdio>
#include <ctime>

namespace {

// Date helpers on YYYY-MM-DD strings, via timegm (no timezone involved).
bool parse_ymd(const std::string& s, std::tm& t) {
    t = std::tm{};
    if (std::sscanf(s.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return false;
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_hour = 12;
    timegm(&t);
    return true;
}
std::string fmt_ymd(const std::tm& t) {
    char b[16];
    std::strftime(b, sizeof(b), "%Y-%m-%d", &t);
    return b;
}
std::tm add_days(std::tm t, int d) { t.tm_mday += d; timegm(&t); return t; }
long day_number(const std::tm& t) { std::tm c = t; return static_cast<long>(timegm(&c) / 86400); }

} // namespace

MeetingSeries SeriesService::row(Statement& st) {
    MeetingSeries s;
    s.id = st.col_int(0); s.title = st.col_text(1); s.description = st.col_text(2);
    s.location = st.col_text(3); s.scope = st.col_text(4); s.chapter_id = st.col_int(5);
    s.is_virtual = st.col_bool(6); s.discord_voice_channel_id = st.col_text(7);
    s.start_hm = st.col_text(8); s.end_hm = st.col_text(9); s.rule = st.col_text(10);
    s.weekday = static_cast<int>(st.col_int(11)); s.interval_weeks = static_cast<int>(st.col_int(12));
    s.nth = static_cast<int>(st.col_int(13)); s.starts_on = st.col_text(14); s.ends_on = st.col_text(15);
    s.days_ahead = static_cast<int>(st.col_int(16)); s.suppress_discord = st.col_bool(17);
    s.suppress_calendar = st.col_bool(18); s.is_private = st.col_bool(19); s.excludes_perks = st.col_bool(20);
    s.active = st.col_bool(21); s.created_by = st.col_int(22);
    return s;
}

static const char* kCols =
    "SELECT id, title, description, location, scope, chapter_id, is_virtual, discord_voice_channel_id, "
    "start_hm, end_hm, rule, weekday, interval_weeks, nth, starts_on, ends_on, days_ahead, "
    "suppress_discord, suppress_calendar, is_private, excludes_perks, active, COALESCE(created_by,0) "
    "FROM meeting_series";

int64_t SeriesService::create(const MeetingSeries& s) {
    auto st = db_.prepare(
        "INSERT INTO meeting_series (title, description, location, scope, chapter_id, is_virtual, "
        "discord_voice_channel_id, start_hm, end_hm, rule, weekday, interval_weeks, nth, starts_on, ends_on, "
        "days_ahead, suppress_discord, suppress_calendar, is_private, excludes_perks, created_by) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) RETURNING id");
    int i = 1;
    st.bind(i++, s.title); st.bind(i++, s.description); st.bind(i++, s.location); st.bind(i++, s.scope);
    st.bind(i++, s.chapter_id); st.bind(i++, s.is_virtual); st.bind(i++, s.discord_voice_channel_id);
    st.bind(i++, s.start_hm); st.bind(i++, s.end_hm); st.bind(i++, s.rule);
    st.bind(i++, static_cast<int64_t>(s.weekday)); st.bind(i++, static_cast<int64_t>(s.interval_weeks));
    st.bind(i++, static_cast<int64_t>(s.nth)); st.bind(i++, s.starts_on); st.bind(i++, s.ends_on);
    st.bind(i++, static_cast<int64_t>(s.days_ahead)); st.bind(i++, s.suppress_discord);
    st.bind(i++, s.suppress_calendar); st.bind(i++, s.is_private); st.bind(i++, s.excludes_perks);
    if (s.created_by > 0) st.bind(i++, s.created_by); else st.bind_null(i++);
    if (!st.step()) throw DbError("INSERT ... RETURNING id produced no row");
    return st.col_int(0);
}

std::optional<MeetingSeries> SeriesService::get(int64_t id) {
    auto st = db_.prepare(std::string(kCols) + " WHERE id=?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return row(st);
}

std::vector<MeetingSeries> SeriesService::list(bool active_only) {
    auto st = db_.prepare(std::string(kCols) + (active_only ? " WHERE active=1" : "") + " ORDER BY active DESC, title");
    std::vector<MeetingSeries> out;
    while (st.step()) out.push_back(row(st));
    return out;
}

std::vector<std::string> SeriesService::occurrences(const MeetingSeries& s, const std::string& from,
                                                    const std::string& to) {
    std::vector<std::string> out;
    std::tm start{}, a{}, b{};
    if (!parse_ymd(s.starts_on, start) || !parse_ymd(from, a) || !parse_ymd(to, b)) return out;
    std::string lo = std::max(from, s.starts_on);
    std::string hi = s.ends_on.empty() ? to : std::min(to, s.ends_on);
    if (lo > hi) return out;
    if (s.rule == "weekly") {
        int interval = s.interval_weeks < 1 ? 1 : s.interval_weeks;
        // First matching weekday on/after starts_on anchors the cadence.
        std::tm first = add_days(start, (s.weekday - start.tm_wday + 7) % 7);
        long anchor = day_number(first);
        std::tm cur{}; parse_ymd(lo, cur);
        cur = add_days(cur, (s.weekday - cur.tm_wday + 7) % 7);
        for (int guard = 0; guard < 1000 && fmt_ymd(cur) <= hi; ++guard, cur = add_days(cur, 7)) {
            long diff = day_number(cur) - anchor;
            if (diff >= 0 && (diff / 7) % interval == 0) out.push_back(fmt_ymd(cur));
        }
    } else { // monthly: nth weekday (or last) of each month
        std::tm m{}; parse_ymd(lo, m);
        m.tm_mday = 1; timegm(&m);
        for (int guard = 0; guard < 240; ++guard) {
            std::tm first = m;
            std::tm d = add_days(first, (s.weekday - first.tm_wday + 7) % 7);
            std::tm pick{};
            bool ok = false;
            if (s.nth >= 1) {
                pick = add_days(d, 7 * (s.nth - 1));
                ok = pick.tm_mon == first.tm_mon;
            } else { // last
                pick = d;
                while (true) { std::tm n = add_days(pick, 7); if (n.tm_mon != first.tm_mon) break; pick = n; }
                ok = true;
            }
            std::string ds = fmt_ymd(pick);
            if (ok && ds >= lo && ds <= hi) out.push_back(ds);
            if (fmt_ymd(first) > hi) break;
            m.tm_mon += 1; m.tm_mday = 1; timegm(&m);
            if (fmt_ymd(m) > hi) break;
        }
    }
    return out;
}

int SeriesService::materialize(const std::string& today) {
    int created = 0;
    for (const auto& s : list(true)) {
        std::tm t{};
        if (!parse_ymd(today, t)) return created;
        std::string until = fmt_ymd(add_days(t, s.days_ahead < 1 ? 45 : s.days_ahead));
        for (const auto& day : occurrences(s, today, until)) {
            {
                auto ex = db_.prepare("SELECT 1 FROM meetings WHERE series_id=? AND substr(start_time,1,10)=?");
                ex.bind(1, s.id);
                ex.bind(2, day);
                if (ex.step()) continue; // already created (even if since edited/cancelled)
            }
            Meeting m;
            m.title = s.title; m.description = s.description; m.location = s.location;
            m.scope = s.scope; m.chapter_id = s.chapter_id; m.is_virtual = s.is_virtual;
            m.discord_voice_channel_id = s.discord_voice_channel_id;
            m.start_time = day + "T" + s.start_hm + ":00";
            m.end_time   = day + "T" + s.end_hm + ":00";
            m.suppress_discord = s.suppress_discord; m.suppress_calendar = s.suppress_calendar;
            m.is_private = s.is_private; m.excludes_perks = s.excludes_perks;
            Meeting made = meetings_.create(m);
            auto link = db_.prepare("UPDATE meetings SET series_id=? WHERE id=?");
            link.bind(1, s.id);
            link.bind(2, made.id);
            link.step();
            ++created;
        }
    }
    return created;
}

int SeriesService::stop(int64_t id, const std::string& now_local_iso) {
    {
        auto st = db_.prepare("UPDATE meeting_series SET active=0 WHERE id=?");
        st.bind(1, id);
        st.step();
    }
    std::vector<int64_t> future;
    {
        auto st = db_.prepare("SELECT id FROM meetings WHERE series_id=? AND start_time > ?");
        st.bind(1, id);
        st.bind(2, now_local_iso);
        while (st.step()) future.push_back(st.col_int(0));
    }
    for (int64_t mid : future) meetings_.cancel(mid); // removes Discord/calendar entries too
    return static_cast<int>(future.size());
}

std::string SeriesService::describe(const MeetingSeries& s) {
    static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* nths[] = {"", "1st", "2nd", "3rd", "4th"};
    int h = 0, mi = 0;
    std::sscanf(s.start_hm.c_str(), "%d:%d", &h, &mi);
    char tm[16];
    std::snprintf(tm, sizeof(tm), "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, mi, h >= 12 ? "PM" : "AM");
    std::string day = days[(s.weekday % 7 + 7) % 7];
    std::string when;
    if (s.rule == "weekly")
        when = s.interval_weeks <= 1 ? "Every " + day : "Every " + std::to_string(s.interval_weeks) + " weeks on " + day;
    else
        when = (s.nth == -1 ? std::string("Last") : std::string(nths[s.nth >= 1 && s.nth <= 4 ? s.nth : 1])) +
               " " + day + " of every month";
    return when + ", " + tm;
}
