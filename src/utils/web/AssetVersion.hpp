#pragma once
#include <ctime>
#include <string>

// Cache-buster appended to /static URLs (?v=...). Static responses carrying
// it are cached for a year as immutable, so it must change whenever the
// files might have: it's fixed per process start, i.e. per deploy/restart.
inline const std::string& asset_version() {
    static const std::string v = std::to_string(static_cast<long long>(std::time(nullptr)));
    return v;
}
