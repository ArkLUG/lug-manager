#pragma once
#include <cctype>
#include <cstdio>
#include <string>

// Percent-encodes everything except RFC 3986 unreserved characters - for
// putting user text (e.g. a search term) into a query-string value.
inline std::string url_encode_component(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}
