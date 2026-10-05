#pragma once
#include <cstdint>
#include <string>

// "1 meeting" / "3 meetings" (the word only, or with the number).
inline std::string plural(int64_t n, const std::string& one, const std::string& many) { return n == 1 ? one : many; }
inline std::string count_of(int64_t n, const std::string& one, const std::string& many) {
    return std::to_string(n) + " " + plural(n, one, many);
}
