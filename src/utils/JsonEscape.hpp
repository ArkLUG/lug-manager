#pragma once
#include <string>
#include <cstdio>

// Escapes a string for safe inclusion inside a JSON string literal (i.e. the
// content between the quotes) - matches the escaping nlohmann::json would
// produce, for the handful of places in this codebase that build JSON by
// hand via std::ostringstream rather than through the library (e.g. an
// hx-vals='{"...":"..."}' HTML attribute built server-side). Does NOT add
// the surrounding quotes.
inline std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (unsigned char c : s) {
        if      (c == '"')  r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else if (c == '\n') r += "\\n";
        else if (c == '\r') r += "\\r";
        else if (c == '\t') r += "\\t";
        else if (c < 0x20) { char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); r += buf; }
        else r += static_cast<char>(c);
    }
    return r;
}
