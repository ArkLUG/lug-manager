#pragma once
// Date/place formatting shared by the chat message placeholders.
#include "integrations/discord/DiscordClient.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace chat::fmt {

// "2026-10-14..." -> "10/14"
inline std::string md(const std::string& iso) {
    if (iso.size() < 10) return iso;
    try {
        int month = std::stoi(iso.substr(5, 2)), day = std::stoi(iso.substr(8, 2));
        if (month >= 1 && month <= 12) return std::to_string(month) + "/" + std::to_string(day);
    } catch (...) {}
    return iso.substr(0, 10);
}

// "2026-10-14..." -> "10/14/26"
inline std::string mdy(const std::string& iso) {
    if (iso.size() < 10) return iso;
    try {
        int month = std::stoi(iso.substr(5, 2)), day = std::stoi(iso.substr(8, 2));
        if (month >= 1 && month <= 12) return std::to_string(month) + "/" + std::to_string(day) + "/" + iso.substr(2, 2);
    } catch (...) {}
    return iso.substr(0, 10);
}

// "10/14 - 10/15", or "10/14" for one day
inline std::string range(const std::string& start, const std::string& end) {
    std::string a = md(start), b = md(end);
    return (a == b || b.empty()) ? a : a + " - " + b;
}
inline std::string range_year(const std::string& start, const std::string& end) {
    std::string a = mdy(start), b = mdy(end);
    return (a == b || b.empty()) ? a : a + "-" + b;
}

// "10/14 7:00 PM CDT"
inline std::string md_time(const std::string& iso, const std::string& tz) {
    if (iso.size() < 16) return iso;
    try {
        int month = std::stoi(iso.substr(5, 2)), day = std::stoi(iso.substr(8, 2));
        int hour = std::stoi(iso.substr(11, 2)), min = std::stoi(iso.substr(14, 2));
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;
        char b[40];
        std::snprintf(b, sizeof(b), "%d/%d %d:%02d %s", month, day, h12, min, hour >= 12 ? "PM" : "AM");
        std::string out = b;
        if (tz == "UTC") out += " UTC";
        else if (!tz.empty()) {
            std::string ab = DiscordClient::tz_abbrev(iso, tz);
            if (!ab.empty()) out += " " + ab;
        }
        return out;
    } catch (...) {}
    return iso.substr(0, 16);
}

// "10/14 7:00 PM CDT – 10/14 9:00 PM CDT"
inline std::string time_range(const std::string& start, const std::string& end, const std::string& tz) {
    std::string a = md_time(start, tz), b = md_time(end, tz);
    if (a.empty()) return "";
    return (b.empty() || b == a) ? a : a + " – " + b;
}

// "Expo Hall, 1 Main St, Little Rock, AR 72201" -> "Little Rock, AR"
inline std::string short_place(const std::string& loc) {
    std::vector<std::string> parts;
    std::istringstream ss(loc);
    std::string part;
    while (std::getline(ss, part, ',')) {
        size_t s = part.find_first_not_of(" \t"), e = part.find_last_not_of(" \t");
        if (s != std::string::npos) parts.push_back(part.substr(s, e - s + 1));
    }
    if (parts.size() < 3) return loc;
    auto is_country = [](const std::string& s) {
        if (s.size() <= 2) return false;
        for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) return false;
        return true;
    };
    size_t last = parts.size() - 1;
    if (is_country(parts[last]) && parts.size() >= 4) last--;
    std::string state = parts[last], city = parts[last - 1];
    size_t sp = state.find(' ');
    if (sp != std::string::npos) state = state.substr(0, sp);
    return city + ", " + state;
}

inline std::vector<std::string> csv(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) out.push_back(t);
    return out;
}

} // namespace chat::fmt
