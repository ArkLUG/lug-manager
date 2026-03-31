#include "integrations/CalendarGenerator.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <ctime>

CalendarGenerator::CalendarGenerator(MeetingRepository& meetings,
                                     EventRepository& events,
                                     const Config& config)
    : meetings_(meetings), events_(events), config_(config),
      timezone_(config.ical_timezone),
      calendar_name_(config.ical_calendar_name) {}

void CalendarGenerator::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_valid_ = false;
}

void CalendarGenerator::set_timezone(const std::string& tz) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!tz.empty()) timezone_ = tz;
    cache_valid_ = false;
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
        else if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') {
            result += "\\n"; ++i;
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
                                            const std::string& timezone) {
    std::string block;
    block += "BEGIN:VEVENT\r\n";
    block += fold_line("UID", uid);
    block += fold_line("DTSTAMP", utc_now_ical());
    block += fold_line("SUMMARY", escape_ical(summary));
    if (!description.empty())
        block += fold_line("DESCRIPTION", escape_ical(description));
    if (!location.empty())
        block += fold_line("LOCATION", escape_ical(location));

    // Emit DTSTART/DTEND with TZID for proper timezone handling
    std::string ical_start = iso_to_ical_dt(start);
    if (!timezone.empty() && timezone != "UTC") {
        block += fold_line("DTSTART;TZID=" + timezone, ical_start);
    } else {
        block += fold_line("DTSTART", ical_start);
    }
    if (!end.empty()) {
        std::string ical_end = iso_to_ical_dt(end);
        if (!timezone.empty() && timezone != "UTC") {
            block += fold_line("DTEND;TZID=" + timezone, ical_end);
        } else {
            block += fold_line("DTEND", ical_end);
        }
    }

    std::string ical_status = (status == "cancelled") ? "CANCELLED" : "CONFIRMED";
    block += fold_line("STATUS", ical_status);
    if (!last_modified.empty())
        block += fold_line("LAST-MODIFIED", iso_to_ical_dt(last_modified));
    block += "END:VEVENT\r\n";
    return block;
}

std::string CalendarGenerator::generate_ics() const {
    std::ostringstream oss;

    oss << "BEGIN:VCALENDAR\r\n";
    oss << "VERSION:2.0\r\n";
    oss << "PRODID:-//LUG-Manager//LUG-Manager 1.0//EN\r\n";
    oss << fold_line("X-WR-CALNAME", calendar_name_);
    oss << fold_line("X-WR-TIMEZONE", timezone_);
    oss << "CALSCALE:GREGORIAN\r\n";
    oss << "METHOD:PUBLISH\r\n";

    // Add meetings
    auto meetings = meetings_.find_all();
    for (const auto& m : meetings) {
        std::string uid = m.ical_uid.empty()
            ? ("meeting-" + std::to_string(m.id) + "@lug-manager")
            : m.ical_uid;
        oss << make_vevent(uid, m.title, m.description, m.location,
                           m.start_time, m.end_time, m.status, m.updated_at,
                           timezone_);
    }

    // Add LUG events
    auto events = events_.find_all();
    for (const auto& e : events) {
        std::string uid = e.ical_uid.empty()
            ? ("event-" + std::to_string(e.id) + "@lug-manager")
            : e.ical_uid;
        oss << make_vevent(uid, e.title, e.description, e.location,
                           e.start_time, e.end_time, e.status, e.updated_at,
                           timezone_);
    }

    oss << "END:VCALENDAR\r\n";
    return oss.str();
}
