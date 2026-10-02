#include "integrations/ical/CalendarGenerator.hpp"
#include "utils/LocalTime.hpp"
#include <cstdio>
#include <unordered_map>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <ctime>

CalendarGenerator::CalendarGenerator(MeetingRepository& meetings,
                                     EventRepository& events,
                                     const Config& config,
                                     ChapterRepository* chapters)
    : meetings_(meetings), events_(events), config_(config), chapters_(chapters),
      timezone_(config.ical_timezone),
      calendar_name_(config.ical_calendar_name) {}

void CalendarGenerator::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_valid_ = false;
    variant_cache_.clear();
}

void CalendarGenerator::set_timezone(const std::string& tz) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!tz.empty()) timezone_ = tz;
    cache_valid_ = false;
    variant_cache_.clear();
}

std::string CalendarGenerator::get_ics() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    if (cache_valid_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - cache_time_).count();
        if (elapsed < 300) {
            return cached_ics_;
        }
    }
    cached_ics_  = generate_ics();
    cache_time_  = now;
    cache_valid_ = true;
    return cached_ics_;
}

std::string CalendarGenerator::escape_ical(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\')      result += "\\\\";
        else if (c == ',')  result += "\\,";
        else if (c == ';')  result += "\\;";
        else if (c == '\r') {
            // CRLF, or a lone CR (some clients treat it as a line break, so
            // leaving it raw would let a title inject extra iCal properties)
            result += "\\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        }
        else if (c == '\n') result += "\\n";
        else                result += c;
    }
    return result;
}

std::string CalendarGenerator::iso_to_ical_dt(const std::string& iso) {
    // "2026-04-15T19:00:00" -> "20260415T190000"
    std::string result;
    result.reserve(15);
    for (char c : iso) {
        if (c == '-' || c == ':') continue;
        if (c == 'Z' || c == '+') break;
        result += c;
    }
    return result;
}

std::string CalendarGenerator::fold_line(const std::string& prop, const std::string& val) {
    std::string line = prop + ":" + val;
    if (line.size() <= 75) {
        return line + "\r\n";
    }
    std::string result;
    size_t pos = 0;
    bool first = true;
    while (pos < line.size()) {
        size_t take = first ? 75 : 74;
        if (pos + take >= line.size()) {
            if (!first) result += ' ';
            result += line.substr(pos);
            result += "\r\n";
            break;
        } else {
            if (!first) result += ' ';
            result += line.substr(pos, take);
            result += "\r\n";
            pos += take;
            first = false;
        }
    }
    return result;
}

// Generate current UTC timestamp in iCal format: "20260415T190000Z"
static std::string utc_now_ical() {
    time_t now = time(nullptr);
    struct tm utc = {};
    gmtime_r(&now, &utc);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &utc);
    return buf;
}

std::string CalendarGenerator::make_vevent(const std::string& uid,
                                            const std::string& summary,
                                            const std::string& description,
                                            const std::string& location,
                                            const std::string& start,
                                            const std::string& end,
                                            const std::string& status,
                                            const std::string& last_modified,
                                            const std::string& timezone,
                                            bool all_day) {
    std::string block;
    block += "BEGIN:VEVENT\r\n";
    block += fold_line("UID", uid);
    block += fold_line("DTSTAMP", utc_now_ical());
    block += fold_line("SUMMARY", escape_ical(summary));
    if (!description.empty())
        block += fold_line("DESCRIPTION", escape_ical(description));
    if (!location.empty())
        block += fold_line("LOCATION", escape_ical(location));

    if (all_day) {
        // All-day: VALUE=DATE with YYYYMMDD format, no time, no timezone
        auto to_ical_date = [](const std::string& iso) -> std::string {
            if (iso.size() < 10) return iso;
            return iso.substr(0, 4) + iso.substr(5, 2) + iso.substr(8, 2);
        };
        // iCal DTEND for all-day is exclusive (same as Google Calendar)
        auto next_day = [](const std::string& iso) -> std::string {
            if (iso.size() < 10) return iso;
            try {
                struct tm t = {};
                t.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
                t.tm_mon  = std::stoi(iso.substr(5, 2)) - 1;
                t.tm_mday = std::stoi(iso.substr(8, 2)) + 1;
                t.tm_isdst = -1;
                mktime(&t);
                char buf[9];
                strftime(buf, sizeof(buf), "%Y%m%d", &t);
                return buf;
            } catch (...) {}
            return iso.substr(0, 4) + iso.substr(5, 2) + iso.substr(8, 2);
        };
        block += fold_line("DTSTART;VALUE=DATE", to_ical_date(start));
        std::string end_dt = end.empty() ? start : end;
        block += fold_line("DTEND;VALUE=DATE", next_day(end_dt));
    } else {
        // Timed event with TZID
        std::string ical_start = iso_to_ical_dt(start);
        if (!timezone.empty() && timezone != "UTC") {
            block += fold_line("DTSTART;TZID=" + timezone, ical_start);
        } else {
            block += fold_line("DTSTART", ical_start + "Z");   // UTC: say so (no suffix = "floating")
        }
        if (!end.empty()) {
            std::string ical_end = iso_to_ical_dt(end);
            if (!timezone.empty() && timezone != "UTC") {
                block += fold_line("DTEND;TZID=" + timezone, ical_end);
            } else {
                block += fold_line("DTEND", ical_end + "Z");
            }
        }
    }

    std::string ical_status = (status == "cancelled") ? "CANCELLED"
                            : (status == "tentative") ? "TENTATIVE"
                            : "CONFIRMED";
    block += fold_line("STATUS", ical_status);
    if (!last_modified.empty())
        block += fold_line("LAST-MODIFIED", iso_to_ical_dt(last_modified));
    block += "END:VEVENT\r\n";
    return block;
}

std::string CalendarGenerator::get_ics(const Filter& f) {
    // Variant feeds are cached like the main one (5 minutes, cleared by invalidate()).
    std::string key = std::to_string(f.chapter_id) + (f.include_lug_wide ? "w" : "") +
                      (f.full_details ? "p" : "") + "|" + f.name_suffix;
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    auto it = variant_cache_.find(key);
    if (it != variant_cache_.end() && now - it->second.first < std::chrono::seconds(300))
        return it->second.second;
    std::string ics = generate_ics(f);
    variant_cache_[key] = {now, ics};
    return ics;
}

// The VTIMEZONE block that TZID=<zone> refers to. Google and Apple know IANA
// zone names anyway, but Outlook and some others need the rules spelled out:
// here, every UTC-offset change (daylight saving) from two years back to three
// years ahead, read from the time-zone database.
static std::string vtimezone(const std::string& tz) {
    if (tz.empty() || tz == "UTC") return "";
    struct Change { std::time_t at; long from, to; bool dst; std::string name; };
    std::vector<Change> changes;
    long first_offset = 0;
    bool first_dst = false;
    std::string first_name;
    {
        std::lock_guard<std::mutex> l(tz_env_mutex());
        const char* old = std::getenv("TZ");
        std::string saved = old ? old : "";
        setenv("TZ", tz.c_str(), 1);
        tzset();
        std::tm now{};
        std::time_t t0 = std::time(nullptr);
        gmtime_r(&t0, &now);
        std::tm from{}; from.tm_year = now.tm_year - 2; from.tm_mon = 0; from.tm_mday = 1;
        std::tm to{};   to.tm_year = now.tm_year + 4;   to.tm_mon = 0; to.tm_mday = 1;
        std::time_t a = timegm(&from), b = timegm(&to);
        std::tm lt{};
        localtime_r(&a, &lt);
        long prev = lt.tm_gmtoff;
        first_offset = prev; first_dst = lt.tm_isdst > 0; first_name = lt.tm_zone ? lt.tm_zone : "";
        for (std::time_t t = a + 1800; t < b; t += 1800) {
            localtime_r(&t, &lt);
            if (lt.tm_gmtoff != prev) {
                changes.push_back({t, prev, lt.tm_gmtoff, lt.tm_isdst > 0, lt.tm_zone ? lt.tm_zone : ""});
                prev = lt.tm_gmtoff;
            }
        }
        if (old) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
        tzset();
    }
    auto off = [](long s) {
        char b[24];
        long a = s < 0 ? -s : s;
        std::snprintf(b, sizeof(b), "%c%02ld%02ld", s < 0 ? '-' : '+', (a / 3600) % 100, (a % 3600) / 60);
        return std::string(b);
    };
    auto wall = [](std::time_t t, long offset) {   // the local clock just before the change
        std::time_t w = t + offset;
        std::tm g{};
        gmtime_r(&w, &g);
        char b[20];
        std::strftime(b, sizeof(b), "%Y%m%dT%H%M%S", &g);
        return std::string(b);
    };
    std::string out = "BEGIN:VTIMEZONE\r\nTZID:" + tz + "\r\n";
    if (changes.empty()) {
        out += std::string("BEGIN:STANDARD\r\nDTSTART:19700101T000000\r\nTZOFFSETFROM:") + off(first_offset) +
               "\r\nTZOFFSETTO:" + off(first_offset) + "\r\n" + (first_name.empty() ? "" : "TZNAME:" + first_name + "\r\n") +
               "END:STANDARD\r\n";
    }
    (void)first_dst;
    for (const auto& c : changes) {
        const char* kind = c.dst ? "DAYLIGHT" : "STANDARD";
        out += std::string("BEGIN:") + kind + "\r\nDTSTART:" + wall(c.at, c.from) + "\r\nTZOFFSETFROM:" + off(c.from) +
               "\r\nTZOFFSETTO:" + off(c.to) + "\r\n" + (c.name.empty() ? "" : "TZNAME:" + c.name + "\r\n") +
               "END:" + kind + "\r\n";
    }
    return out + "END:VTIMEZONE\r\n";
}

std::string CalendarGenerator::generate_ics() const {
    return generate_ics(Filter());
}

std::string CalendarGenerator::generate_ics(const Filter& f) const {
    std::ostringstream oss;

    oss << "BEGIN:VCALENDAR\r\n";
    oss << "VERSION:2.0\r\n";
    oss << "PRODID:-//LUG-Manager//LUG-Manager 1.0//EN\r\n";
    oss << fold_line("X-WR-CALNAME", calendar_name_ + f.name_suffix);
    oss << fold_line("X-WR-TIMEZONE", timezone_);
    oss << "CALSCALE:GREGORIAN\r\n";
    oss << "METHOD:PUBLISH\r\n";
    oss << vtimezone(timezone_);

    // Helper to build prefixed calendar title
    // Chapter shorthands, loaded once per feed (was one lookup per item).
    std::unordered_map<int64_t, std::string> shorthand;
    if (chapters_)
        for (const auto& ch : chapters_->find_all()) shorthand[ch.id] = ch.shorthand;

    auto cal_title = [&shorthand](const std::string& title, const std::string& scope,
                                  int64_t chapter_id, const std::string& status = "") -> std::string {
        std::string prefix;
        if (status == "tentative") prefix += "[Tentative] ";
        if (scope == "non_lug")        prefix += "[External] ";
        else if (scope == "lug_wide")  prefix += "[Group-wide] ";
        if (chapter_id > 0) {
            auto it = shorthand.find(chapter_id);
            if (it != shorthand.end() && !it->second.empty()) prefix = "[" + it->second + "] " + prefix;
        }
        return prefix + title;
    };

    // Add meetings
    auto wanted = [&f](const std::string& scope, int64_t chapter_id) {
        if (f.chapter_id <= 0) return true;
        return chapter_id == f.chapter_id || (f.include_lug_wide && scope == "lug_wide");
    };

    auto meetings = meetings_.find_all();
    for (const auto& m : meetings) {
        if (!wanted(m.scope, m.chapter_id)) continue;
        std::string uid = m.ical_uid.empty()
            ? ("meeting-" + std::to_string(m.id) + "@lug-manager")
            : m.ical_uid;
        // This feed is served with no auth at all (see calendar.ics) - private
        // meetings get the same generic placeholder treatment as the Google
        // Calendar sync, so it shows the LUG is busy without exposing details.
        if (m.is_private && !f.full_details) {
            oss << make_vevent(uid, "Private LUG Meeting", "", "",
                               m.start_time, m.end_time, m.status, m.updated_at,
                               timezone_);
        } else {
            oss << make_vevent(uid, cal_title(m.title, m.scope, m.chapter_id),
                               m.description, m.location,
                               m.start_time, m.end_time, m.status, m.updated_at,
                               timezone_);
        }
    }

    // Add LUG events
    auto events = events_.find_all();
    for (const auto& e : events) {
        if (!wanted(e.scope, e.chapter_id)) continue;
        std::string uid = e.ical_uid.empty()
            ? ("event-" + std::to_string(e.id) + "@lug-manager")
            : e.ical_uid;
        if (e.is_private && !f.full_details) {
            oss << make_vevent(uid, "Private LUG Event", "", "",
                               e.start_time, e.end_time, e.status, e.updated_at,
                               timezone_, true);
        } else {
            oss << make_vevent(uid, cal_title(e.title, e.scope, e.chapter_id, e.status),
                               e.description, e.location,
                               e.start_time, e.end_time, e.status, e.updated_at,
                               timezone_, true);
        }
    }

    oss << "END:VCALENDAR\r\n";
    return oss.str();
}
