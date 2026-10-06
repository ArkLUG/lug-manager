#pragma once
#include "db/SqliteDatabase.hpp"
#include <string>
#include <utility>
#include <vector>

// Meeting RSVPs (migration 077): going / can't make it, no capacity.
class MeetingRsvps {
public:
    explicit MeetingRsvps(SqliteDatabase& db) : db_(db) {}

    // "going", "not_going", or "" (hasn't said)
    std::string status_of(int64_t meeting_id, int64_t member_id) {
        auto st = db_.prepare("SELECT going FROM meeting_rsvps WHERE meeting_id=? AND member_id=?");
        st.bind(1, meeting_id); st.bind(2, member_id);
        if (!st.step()) return "";
        return st.col_int(0) ? "going" : "not_going";
    }
    void set(int64_t meeting_id, int64_t member_id, bool going) {
        auto st = db_.prepare("INSERT INTO meeting_rsvps (meeting_id, member_id, going) VALUES (?,?,?) "
                              "ON CONFLICT(meeting_id, member_id) DO UPDATE SET going=excluded.going, updated_at=datetime('now')");
        st.bind(1, meeting_id); st.bind(2, member_id); st.bind(3, static_cast<int64_t>(going ? 1 : 0));
        st.step();
    }
    void clear(int64_t meeting_id, int64_t member_id) {
        auto st = db_.prepare("DELETE FROM meeting_rsvps WHERE meeting_id=? AND member_id=?");
        st.bind(1, meeting_id); st.bind(2, member_id);
        st.step();
    }
    struct Counts { int going = 0, not_going = 0; };
    Counts counts(int64_t meeting_id) {
        Counts c;
        auto st = db_.prepare("SELECT COALESCE(SUM(going),0), COALESCE(SUM(1-going),0) FROM meeting_rsvps WHERE meeting_id=?");
        st.bind(1, meeting_id);
        if (st.step()) { c.going = static_cast<int>(st.col_int(0)); c.not_going = static_cast<int>(st.col_int(1)); }
        return c;
    }
    // Members going: {member id, display name}, by name
    std::vector<std::pair<int64_t, std::string>> going(int64_t meeting_id) {
        std::vector<std::pair<int64_t, std::string>> out;
        auto st = db_.prepare("SELECT m.id, m.display_name FROM meeting_rsvps r JOIN members m ON m.id=r.member_id "
                              "WHERE r.meeting_id=? AND r.going=1 ORDER BY m.display_name COLLATE NOCASE");
        st.bind(1, meeting_id);
        while (st.step()) out.emplace_back(st.col_int(0), st.col_text(1));
        return out;
    }

private:
    SqliteDatabase& db_;
};
