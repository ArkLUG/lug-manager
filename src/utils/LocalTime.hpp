#pragma once
#include <ctime>

// Thread-safe replacement for std::localtime (which returns a pointer to a
// shared static buffer - a data race under Crow's multithreaded handlers).
inline std::tm local_tm(std::time_t t) {
    std::tm out{};
    localtime_r(&t, &out);
    return out;
}
