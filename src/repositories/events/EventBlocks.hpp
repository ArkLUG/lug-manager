#pragma once
// A show's day-by-day schedule (migration 074): time blocks within its days,
// each setup / public / teardown / other. The event's own start and end stay
// its public dates; blocks add the hours, and setup or teardown days around
// them. Without blocks an event is all-day dates, as before.
#include "db/SqliteDatabase.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct EventBlock {
    int64_t id = 0, event_id = 0;
    std::string day, kind, starts, ends, label;   // day YYYY-MM-DD, times HH:MM
    std::string start_iso() const { return day + "T" + starts + ":00"; }
    std::string end_iso() const { return day + "T" + ends + ":00"; }
    bool is_public() const { return kind == "public"; }
};

namespace event_blocks {

inline const char* kind_label(const std::string& k) {
    return k == "setup" ? "Setup" : k == "teardown" ? "Teardown" : k == "public" ? "Open to the public" : "Other";
}
inline bool valid_kind(const std::string& k) { return k == "setup" || k == "public" || k == "teardown" || k == "other"; }

inline std::vector<EventBlock> list(SqliteDatabase& db, int64_t event_id) {
    std::vector<EventBlock> out;
    auto st = db.prepare("SELECT id, event_id, day_date, kind, starts_at, ends_at, label FROM event_blocks "
                         "WHERE event_id=? ORDER BY day_date, starts_at, id");
    st.bind(1, event_id);
    while (st.step())
        out.push_back({st.col_int(0), st.col_int(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_text(5), st.col_text(6)});
    return out;
}

// First public block's start to the last public block's end ("YYYY-MM-DDTHH:MM:00").
inline std::optional<std::pair<std::string, std::string>> public_span(const std::vector<EventBlock>& blocks) {
    std::optional<std::pair<std::string, std::string>> out;
    for (const auto& b : blocks) {
        if (!b.is_public()) continue;
        if (!out) out = std::make_pair(b.start_iso(), b.end_iso());
        else { if (b.start_iso() < out->first) out->first = b.start_iso(); if (b.end_iso() > out->second) out->second = b.end_iso(); }
    }
    return out;
}

// Every day the event covers, setup and teardown days included.
inline std::pair<std::string, std::string> day_range(const std::vector<EventBlock>& blocks, const std::string& start, const std::string& end) {
    std::string first = start.substr(0, 10), last = (end.size() >= 10 ? end : start).substr(0, 10);
    for (const auto& b : blocks) { if (b.day < first) first = b.day; if (b.day > last) last = b.day; }
    return {first, last};
}
inline std::pair<std::string, std::string> day_range(SqliteDatabase& db, int64_t event_id, const std::string& start, const std::string& end) {
    return day_range(list(db, event_id), start, end);
}

} // namespace event_blocks
