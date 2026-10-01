#pragma once
#include <algorithm>
#include <cctype>
#include <string>

// Discord ids from forms/the API end up spliced into bot-authenticated Discord
// API URL paths. Real ids are numeric snowflakes; this only enforces what
// matters for safety - no characters that could change the URL path or query
// ('/', '.', '%', '?', '#', whitespace, ...) - so legacy/test ids still pass.
inline bool is_safe_discord_id(const std::string& s) {
    return !s.empty() && s.size() <= 32 &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) {
               return std::isalnum(c) || c == '-' || c == '_';
           });
}
