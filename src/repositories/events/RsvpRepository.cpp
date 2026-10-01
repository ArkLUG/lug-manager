#include "repositories/events/RsvpRepository.hpp"

RsvpRepository::RsvpRepository(SqliteDatabase& db) : db_(db) {}

int RsvpRepository::count(int64_t event_id, const std::string& status) {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM event_rsvps WHERE event_id=? AND status=?");
    stmt.bind(1, event_id);
    stmt.bind(2, status);
    return stmt.step() ? static_cast<int>(stmt.col_int(0)) : 0;
}

std::optional<std::string> RsvpRepository::status_of(int64_t event_id, int64_t member_id) {
    auto stmt = db_.prepare("SELECT status FROM event_rsvps WHERE event_id=? AND member_id=?");
    stmt.bind(1, event_id);
    stmt.bind(2, member_id);
    if (!stmt.step()) return std::nullopt;
    return stmt.col_text(0);
}

std::string RsvpRepository::rsvp(int64_t event_id, int64_t member_id, int max_attendees) {
    // Count-then-insert must be atomic or two last-spot RSVPs could both get in.
    Transaction tx(db_);
    if (auto existing = status_of(event_id, member_id)) {
        tx.commit();
        return *existing;
    }
    bool room = max_attendees <= 0 || count(event_id, "going") < max_attendees;
    std::string status = room ? "going" : "waitlist";
    auto stmt = db_.prepare("INSERT INTO event_rsvps (event_id, member_id, status) VALUES (?,?,?)");
    stmt.bind(1, event_id);
    stmt.bind(2, member_id);
    stmt.bind(3, status);
    stmt.step();
    tx.commit();
    return status;
}

std::optional<int64_t> RsvpRepository::cancel(int64_t event_id, int64_t member_id, int max_attendees) {
    Transaction tx(db_);
    auto was = status_of(event_id, member_id);
    if (!was) { tx.commit(); return std::nullopt; }
    {
        auto del = db_.prepare("DELETE FROM event_rsvps WHERE event_id=? AND member_id=?");
        del.bind(1, event_id);
        del.bind(2, member_id);
        del.step();
    }
    std::optional<int64_t> promoted;
    if (*was == "going" && (max_attendees <= 0 || count(event_id, "going") < max_attendees)) {
        auto next = db_.prepare(
            "UPDATE event_rsvps SET status='going' WHERE id = ("
            "  SELECT id FROM event_rsvps WHERE event_id=? AND status='waitlist' "
            "  ORDER BY created_at, id LIMIT 1) RETURNING member_id");
        next.bind(1, event_id);
        if (next.step()) promoted = next.col_int(0);
    }
    tx.commit();
    return promoted;
}

int RsvpRepository::waitlist_position(int64_t event_id, int64_t member_id) {
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM event_rsvps w, event_rsvps me "
        "WHERE me.event_id=? AND me.member_id=? AND me.status='waitlist' "
        "  AND w.event_id=me.event_id AND w.status='waitlist' "
        "  AND (w.created_at < me.created_at OR (w.created_at = me.created_at AND w.id <= me.id))");
    stmt.bind(1, event_id);
    stmt.bind(2, member_id);
    return stmt.step() ? static_cast<int>(stmt.col_int(0)) : 0;
}

std::vector<EventRsvp> RsvpRepository::list(int64_t event_id) {
    auto stmt = db_.prepare(
        "SELECT r.id, r.event_id, r.member_id, r.status, r.created_at, COALESCE(m.display_name,'') "
        "FROM event_rsvps r LEFT JOIN members m ON m.id = r.member_id "
        "WHERE r.event_id=? "
        "ORDER BY CASE r.status WHEN 'going' THEN 0 ELSE 1 END, r.created_at, r.id");
    stmt.bind(1, event_id);
    std::vector<EventRsvp> out;
    while (stmt.step()) {
        EventRsvp r;
        r.id = stmt.col_int(0);
        r.event_id = stmt.col_int(1);
        r.member_id = stmt.col_int(2);
        r.status = stmt.col_text(3);
        r.created_at = stmt.col_text(4);
        r.member_display_name = stmt.col_text(5);
        out.push_back(std::move(r));
    }
    return out;
}
