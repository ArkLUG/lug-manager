#pragma once
// Colour themes, chosen per member (My Account, or the sidebar) on top of
// light/dark mode; admins pick the LUG's default (Settings > Branding), which
// public pages use too. The colours themselves are in static/palettes.css.
#include <string>
#include <vector>

namespace palettes {

struct Palette { const char* key; const char* name; const char* description; };

inline const std::vector<Palette>& all() {
    static const std::vector<Palette> list = {
        {"classic",   "Classic",            "The original LUG Manager look: yellow on charcoal."},
        {"fancolab",  "LEGO Fan CoLab",     "Fan CoLab purple with the minifigure-head yellow."},
        {"creator",   "Fan CoLab Creator",  "The pink and purple of the Recognized Fan Creator badge."},
        {"community", "Fan CoLab Community","The blue of the Recognized Community Ambassador badge."},
    };
    return list;
}

inline bool valid(const std::string& key) {
    for (const auto& p : all()) if (key == p.key) return true;
    return false;
}

// A member's own choice if they made one, else the LUG default, else classic.
inline std::string resolve(const std::string& mine, const std::string& lug_default) {
    if (valid(mine)) return mine;
    if (valid(lug_default)) return lug_default;
    return "classic";
}

} // namespace palettes
