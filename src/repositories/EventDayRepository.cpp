#include "repositories/EventDayRepository.hpp"
#include <ctime>
#include <sstream>

EventDayRepository::EventDayRepository(SqliteDatabase& db) : db_(db) {}

static std::string iso_date_part(const std::string& iso) {
    return iso.size() >= 10 ? iso.substr(0, 10) : iso;
}

// Parse YYYY-MM-DD into a tm struct (UTC).
static bool parse_ymd(const std::string& s, std::tm& out) {
    if (s.size() < 10) return false;
    try {
        int y = std::stoi(s.substr(0, 4));
        int m = std::stoi(s.substr(5, 2));
        int d = std::stoi(s.substr(8, 2));
        if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
        out = {};
        out.tm_year = y - 1900;
        out.tm_mon  = m - 1;
        out.tm_mday = d;
        return true;
    } catch (...) {
        return false;
    }
}

static std::string format_ymd(const std::tm& tm_in) {
    std::tm t = tm_in;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return buf;
}

// Expand a start..end date range (inclusive) into a vector of YYYY-MM-DD.
static std::vector<std::string> expand_date_range(const std::string& start_ymd,
                                                   const std::string& end_ymd) {
    std::vector<std::string> dates;
    std::tm start_tm{}, end_tm{};
    if (!parse_ymd(start_ymd, start_tm)) return dates;
    if (!parse_ymd(end_ymd, end_tm))     end_tm = start_tm;

    std::time_t t_start = timegm(&start_tm);
    std::time_t t_end   = timegm(&end_tm);
    if (t_end < t_start) t_end = t_start;

    for (std::time_t t = t_start; t <= t_end; t += 86400) {
        std::tm gm;
        gmtime_r(&t, &gm);
        dates.push_back(format_ymd(gm));
        // cap at 31 days to prevent runaway in case of bad data
        if (dates.size() >= 31) break;
    }
    return dates;
}

std::vector<EventDay> EventDayRepository::find_by_event(int64_t event_id) {
    auto stmt = db_.prepare(
        "SELECT id, event_id, day_date, day_number FROM event_days "
        "WHERE event_id=? ORDER BY day_number ASC");
    stmt.bind(1, event_id);
    std::vector<EventDay> result;
    while (stmt.step()) {
        EventDay d;
        d.id         = stmt.col_int(0);
        d.event_id   = stmt.col_int(1);
        d.day_date   = stmt.col_text(2);
        d.day_number = static_cast<int>(stmt.col_int(3));
        result.push_back(d);
    }
    return result;
}

std::optional<EventDay> EventDayRepository::find_by_event_and_date(int64_t event_id,
                                                                    const std::string& day_date) {
    auto stmt = db_.prepare(
        "SELECT id, event_id, day_date, day_number FROM event_days "
        "WHERE event_id=? AND day_date=?");
    stmt.bind(1, event_id);
    stmt.bind(2, day_date);
    if (stmt.step()) {
        EventDay d;
        d.id         = stmt.col_int(0);
        d.event_id   = stmt.col_int(1);
        d.day_date   = stmt.col_text(2);
        d.day_number = static_cast<int>(stmt.col_int(3));
        return d;
    }
    return std::nullopt;
}

std::optional<EventDay> EventDayRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        "SELECT id, event_id, day_date, day_number FROM event_days WHERE id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        EventDay d;
        d.id         = stmt.col_int(0);
        d.event_id   = stmt.col_int(1);
        d.day_date   = stmt.col_text(2);
        d.day_number = static_cast<int>(stmt.col_int(3));
        return d;
    }
    return std::nullopt;
}

void EventDayRepository::sync_for_event(int64_t event_id,
                                         const std::string& start_time,
                                         const std::string& end_time) {
    std::string start_ymd = iso_date_part(start_time);
    std::string end_ymd   = iso_date_part(end_time);
    auto wanted = expand_date_range(start_ymd, end_ymd);
    if (wanted.empty()) return;

    // Delete days outside the new range (cascades to attendance on dropped days).
    {
        std::ostringstream sql;
        sql << "DELETE FROM event_days WHERE event_id=? AND day_date NOT IN (";
        for (size_t i = 0; i < wanted.size(); ++i) {
            if (i > 0) sql << ",";
            sql << "?";
        }
        sql << ")";
        auto stmt = db_.prepare(sql.str());
        int idx = 1;
        stmt.bind(idx++, event_id);
        for (const auto& d : wanted) stmt.bind(idx++, d);
        stmt.step();
    }

    // Upsert each wanted day with the correct day_number.
    for (size_t i = 0; i < wanted.size(); ++i) {
        int day_num = static_cast<int>(i) + 1;
        auto upd = db_.prepare(
            "UPDATE event_days SET day_number=? WHERE event_id=? AND day_date=?");
        upd.bind(1, static_cast<int64_t>(day_num));
        upd.bind(2, event_id);
        upd.bind(3, wanted[i]);
        upd.step();

        auto ins = db_.prepare(
            "INSERT OR IGNORE INTO event_days (event_id, day_date, day_number) "
            "VALUES (?,?,?)");
        ins.bind(1, event_id);
        ins.bind(2, wanted[i]);
        ins.bind(3, static_cast<int64_t>(day_num));
        ins.step();
    }
}
