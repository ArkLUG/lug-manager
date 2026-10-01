#pragma once
#include <algorithm>
#include <cstdint>
#include <regex>
#include <string>

// "25", "25.5", "$25.00", "1,250.00" -> cents; 0 for empty; -1 if unparseable.
inline int64_t parse_cents(std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '$' || c == ',' || c == ' '; }), s.end());
    if (s.empty()) return 0;
    static const std::regex re(R"((\d{1,7})(?:\.(\d{1,2}))?)");
    std::smatch m;
    if (!std::regex_match(s, m, re)) return -1;
    int64_t cents = std::stoll(m[1].str()) * 100;
    if (m[2].matched) cents += std::stoll(m[2].str().size() == 1 ? m[2].str() + "0" : m[2].str());
    return cents;
}

// 123456 -> "$1,234.56"; -500 -> "-$5.00"
inline std::string money(int64_t cents) {
    bool neg = cents < 0;
    uint64_t c = neg ? static_cast<uint64_t>(-(cents + 1)) + 1 : static_cast<uint64_t>(cents);
    std::string whole = std::to_string(c / 100);
    for (int i = static_cast<int>(whole.size()) - 3; i > 0; i -= 3) whole.insert(static_cast<size_t>(i), ",");
    std::string frac = std::to_string(c % 100);
    if (frac.size() < 2) frac = "0" + frac;
    return std::string(neg ? "-$" : "$") + whole + "." + frac;
}
