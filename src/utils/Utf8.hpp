#pragma once
#include <string>

// Truncates to at most `max_chars` Unicode code points without splitting a
// UTF-8 sequence (a split sequence makes json::dump() throw). Appends an
// ellipsis when anything was cut. Discord limits: message content 2000,
// scheduled-event description 1000, names 100.
inline std::string utf8_truncate(const std::string& s, size_t max_chars) {
    size_t chars = 0, i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (chars == max_chars) break;
        i += len;
        ++chars;
    }
    if (i >= s.size()) return s;
    // Back off one more code point to make room for the ellipsis.
    size_t cut = i;
    while (cut > 0 && (static_cast<unsigned char>(s[cut - 1]) & 0xC0) == 0x80) --cut;
    if (cut > 0) --cut;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\u2026";
}

