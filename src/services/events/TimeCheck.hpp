#pragma once
// Start and end of a meeting or event as entered ("YYYY-MM-DDTHH:MM[:SS]",
// local). An end before the start can't be shown anywhere (Discord refuses the
// scheduled event, calendars hide it), so it's turned away at the door.
#include <stdexcept>
#include <string>

inline void check_start_end(const std::string& start, const std::string& end) {
    if (start.empty() || end.empty()) return;
    // Compare like with like: "2026-10-14T19:00" vs "2026-10-14T19:00:00".
    auto norm = [](std::string s) { if (s.size() == 16) s += ":00"; return s; };
    if (norm(end) < norm(start)) throw std::invalid_argument("The end is before the start.");
}
