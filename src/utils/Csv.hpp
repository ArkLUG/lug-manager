#pragma once
#include <string>

// One quoted CSV field. Values starting with = + - @ get a leading quote so
// spreadsheets don't evaluate them as formulas (CSV/formula injection).
inline std::string csv_field(const std::string& s) {
    std::string out = "\"";
    if (!s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@')) out += '\'';
    for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
    return out + "\"";
}
