#pragma once
#include <cstdint>
#include <string>

// Parses a request-supplied numeric id. Returns 0 (never a valid row id) for
// empty/garbage input instead of throwing, so a bad form/query value becomes
// an ordinary "not found"/400 path rather than an uncaught 500.
inline int64_t parse_id(const std::string& s) {
    try {
        size_t used = 0;
        long long v = std::stoll(s, &used);
        return (used == s.size() && v > 0) ? static_cast<int64_t>(v) : 0;
    } catch (...) {
        return 0;
    }
}
