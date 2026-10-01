#pragma once
#include <ctime>

// Thread-safe replacement for std::localtime (which returns a pointer to a
// shared static buffer - a data race under Crow's multithreaded handlers).
inline std::tm local_tm(std::time_t t) {
    std::tm out{};
    localtime_r(&t, &out);
    return out;
}

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
