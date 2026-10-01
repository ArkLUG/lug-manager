#pragma once
#include <string>

// Escapes a string for safe inclusion in HTML text/attribute context. Use
// this any time a value that isn't a compile-time literal - especially
// anything that traces back to user input (member display names, Discord
// usernames/role/channel names synced from Discord, chapter/event/meeting
// names, notes) - gets concatenated directly into an HTML string via
// std::ostringstream/string += rather than passed through a
// crow::mustache {{tag}} (which auto-escapes) or {{{triple-brace}}} (which
// does NOT). Every hand-built <option>/<span>/etc. HTML fragment in this
// codebase's route handlers needs this at each interpolation point.
inline std::string html_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}
