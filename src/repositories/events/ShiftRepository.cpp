#include "repositories/events/ShiftRepository.hpp"

int64_t ShiftRepository::create(const EventShift& s) {
    auto st = db_.prepare("INSERT INTO event_shifts (event_id, title, starts_at, ends_at, slots, notes) "
                          "VALUES (?,?,?,?,?,?) RETURNING id");
    st.bind(1, s.event_id); st.bind(2, s.title); st.bind(3, s.starts_at); st.bind(4, s.ends_at);
    st.bind(5, static_cast<int64_t>(s.slots)); st.bind(6, s.notes);
    if (!st.step()) throw DbError("INSERT ... RETURNING id produced no row");
    return st.col_int(0);
}

bool ShiftRepository::remove(int64_t shift_id, int64_t event_id) {
    auto st = db_.prepare("DELETE FROM event_shifts WHERE id=? AND event_id=? RETURNING id");
    st.bind(1, shift_id); st.bind(2, event_id);
    return st.step();
}

std::optional<EventShift> ShiftRepository::find(int64_t shift_id) {
    auto st = db_.prepare("SELECT id, event_id, title, starts_at, ends_at, slots, notes FROM event_shifts WHERE id=?");
    st.bind(1, shift_id);
    if (!st.step()) return std::nullopt;
    EventShift s;
    s.id = st.col_int(0); s.event_id = st.col_int(1); s.title = st.col_text(2); s.starts_at = st.col_text(3);
    s.ends_at = st.col_text(4); s.slots = static_cast<int>(st.col_int(5)); s.notes = st.col_text(6);
    return s;
}

std::vector<EventShift> ShiftRepository::for_event(int64_t event_id) {
    std::vector<EventShift> out;
    {
        auto st = db_.prepare("SELECT id, event_id, title, starts_at, ends_at, slots, notes FROM event_shifts "
                              "WHERE event_id=? ORDER BY starts_at, id");
        st.bind(1, event_id);
        while (st.step()) {
            EventShift s;
            s.id = st.col_int(0); s.event_id = st.col_int(1); s.title = st.col_text(2); s.starts_at = st.col_text(3);
            s.ends_at = st.col_text(4); s.slots = static_cast<int>(st.col_int(5)); s.notes = st.col_text(6);
            out.push_back(std::move(s));
        }
    }
    for (auto& s : out) {
        auto st = db_.prepare("SELECT u.member_id, COALESCE(m.display_name,'') FROM event_shift_signups u "
                              "LEFT JOIN members m ON m.id = u.member_id WHERE u.shift_id=? ORDER BY u.created_at, u.id");
        st.bind(1, s.id);
        while (st.step()) s.volunteers.emplace_back(st.col_int(0), st.col_text(1));
        s.taken = static_cast<int>(s.volunteers.size());
    }
    return out;
}

bool ShiftRepository::sign_up(int64_t shift_id, int64_t member_id) {
    // Count + insert atomically so the last slot can't be taken twice.
    // Statements are scoped so they're finalized before COMMIT (SQLite refuses
    // to commit while a statement is still mid-result).
    Transaction tx(db_);
    bool room = false;
    {
        auto cap = db_.prepare("SELECT s.slots, (SELECT COUNT(*) FROM event_shift_signups WHERE shift_id=s.id) "
                               "FROM event_shifts s WHERE s.id=?");
        cap.bind(1, shift_id);
        room = cap.step() && cap.col_int(1) < cap.col_int(0);
    }
    bool ok = false;
    if (room) {
        auto ins = db_.prepare("INSERT OR IGNORE INTO event_shift_signups (shift_id, member_id) VALUES (?,?) RETURNING id");
        ins.bind(1, shift_id); ins.bind(2, member_id);
        ok = ins.step();
    }
    tx.commit();
    return ok;
}

bool ShiftRepository::withdraw(int64_t shift_id, int64_t member_id) {
    auto st = db_.prepare("DELETE FROM event_shift_signups WHERE shift_id=? AND member_id=? RETURNING id");
    st.bind(1, shift_id); st.bind(2, member_id);
    return st.step();
}

bool ShiftRepository::is_signed_up(int64_t shift_id, int64_t member_id) {
    auto st = db_.prepare("SELECT 1 FROM event_shift_signups WHERE shift_id=? AND member_id=?");
    st.bind(1, shift_id); st.bind(2, member_id);
    return st.step();
}

std::vector<ShiftRepository::Due> ShiftRepository::due_reminders(const std::string& from, const std::string& to) {
    auto st = db_.prepare(
        "SELECT u.id, u.member_id, COALESCE(m.discord_user_id,''), COALESCE(m.display_name,''), s.title, "
        "COALESCE(e.title,''), s.starts_at FROM event_shift_signups u "
        "JOIN event_shifts s ON s.id = u.shift_id JOIN members m ON m.id = u.member_id "
        "LEFT JOIN lug_events e ON e.id = s.event_id "
        "WHERE u.reminded_at IS NULL AND s.starts_at >= ? AND s.starts_at <= ? "
        "AND NOT EXISTS (SELECT 1 FROM notification_optouts o WHERE o.member_id = u.member_id AND o.kind = 'shift_reminder')");
    st.bind(1, from); st.bind(2, to);
    std::vector<Due> out;
    while (st.step())
        out.push_back({st.col_int(0), st.col_int(1), st.col_text(2), st.col_text(3), st.col_text(4), st.col_text(5), st.col_text(6)});
    return out;
}

bool ShiftRepository::claim_reminder(int64_t signup_id) {
    auto st = db_.prepare("UPDATE event_shift_signups SET reminded_at=datetime('now') "
                          "WHERE id=? AND reminded_at IS NULL RETURNING id");
    st.bind(1, signup_id);
    return st.step();
}
