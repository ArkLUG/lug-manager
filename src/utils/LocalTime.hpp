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
