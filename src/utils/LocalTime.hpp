#pragma once
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

// Changing the process time zone (TZ) is a process-wide act: every change
// takes this lock (also DiscordClient's per-call time-zone conversions).
inline std::mutex& tz_env_mutex() {
    static std::mutex m;
    return m;
}

// The server's "local time" is the LUG's time zone (Settings > Calendar), not
// whatever TZ the container happens to have: "today", "upcoming", reminder
// windows and SQLite's 'localtime' then match the times people enter. Called
// at start-up and when the setting changes. Unknown zones are ignored.
inline bool set_process_timezone(const std::string& tz) {
    if (tz.empty() || tz.find("..") != std::string::npos) return false;
    if (tz != "UTC" && !std::filesystem::exists("/usr/share/zoneinfo/" + tz)) return false;
    std::lock_guard<std::mutex> l(tz_env_mutex());
    setenv("TZ", tz.c_str(), 1);
    tzset();
    return true;
}

// The LUG's time zone name as set by set_process_timezone ("" if never set).
inline std::string process_timezone() {
    std::lock_guard<std::mutex> l(tz_env_mutex());
    const char* tz = std::getenv("TZ");
    return tz ? tz : "";
}

// A stored local "YYYY-MM-DDTHH:MM[:SS]" as a UTC epoch (DST-aware), or -1.
inline std::time_t local_iso_to_epoch(const std::string& iso) {
    std::tm t{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return -1;
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = sec;
    t.tm_isdst = -1;
    std::lock_guard<std::mutex> l(tz_env_mutex());
    return std::mktime(&t);
}

// Thread-safe replacement for std::localtime (which returns a pointer to a
// shared static buffer - a data race under Crow's multithreaded handlers).
inline std::tm local_tm(std::time_t t) {
    std::tm out{};
    localtime_r(&t, &out);
    return out;
}

// The current year on the server's local clock.
inline int local_year() { return local_tm(std::time(nullptr)).tm_year + 1900; }

#include <string>

// Local wall-clock time as "YYYY-MM-DDTHH:MM:SS" - the form meeting/event
// times are stored in, so it compares directly with them.
inline std::string local_iso(std::time_t t) {
    std::tm tm = local_tm(t);
    char b[24];
    std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tm);
    return b;
}
inline std::string local_iso_now() { return local_iso(std::time(nullptr)); }

// "2026-06-06..." -> "Sat Jun 6, 2026" ("" stays "").
inline std::string friendly_date(const std::string& iso) {
    if (iso.size() < 10) return iso;
    std::tm t{};
    if (!strptime(iso.substr(0, 10).c_str(), "%Y-%m-%d", &t)) return iso.substr(0, 10);
    t.tm_hour = 12;
    t.tm_isdst = -1;
    std::mktime(&t);
    char b[32];
    std::strftime(b, sizeof(b), "%a %b %d, %Y", &t);
    std::string s = b;
    if (s.size() > 8 && s[8] == '0') s.erase(8, 1);   // "Jun 06" -> "Jun 6"
    return s;
}
