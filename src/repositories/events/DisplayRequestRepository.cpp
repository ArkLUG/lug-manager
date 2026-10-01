#include "repositories/events/DisplayRequestRepository.hpp"

DisplayRequestRepository::DisplayRequestRepository(SqliteDatabase& db) : db_(db) {}

bool DisplayRequestRepository::accepts_displays(int64_t event_id) {
    auto stmt = db_.prepare("SELECT accepts_displays FROM lug_events WHERE id=?");
    stmt.bind(1, event_id);
    return stmt.step() && stmt.col_int(0) != 0;
}

void DisplayRequestRepository::set_accepts_displays(int64_t event_id, bool on) {
    auto stmt = db_.prepare("UPDATE lug_events SET accepts_displays=? WHERE id=?");
    stmt.bind(1, static_cast<int64_t>(on ? 1 : 0));
    stmt.bind(2, event_id);
    stmt.step();
}

DisplayRequest DisplayRequestRepository::create(const DisplayRequest& r) {
    auto stmt = db_.prepare(
        "INSERT INTO event_display_requests (event_id, member_id, title, description, width_in, "
        "depth_in, needs_power, notes) VALUES (?,?,?,?,?,?,?,?) RETURNING id");
    stmt.bind(1, r.event_id);
    stmt.bind(2, r.member_id);
    stmt.bind(3, r.title);
    stmt.bind(4, r.description);
    stmt.bind(5, static_cast<int64_t>(r.width_in));
    stmt.bind(6, static_cast<int64_t>(r.depth_in));
    stmt.bind(7, static_cast<int64_t>(r.needs_power ? 1 : 0));
    stmt.bind(8, r.notes);
    if (!stmt.step()) throw DbError("INSERT ... RETURNING id produced no row");
    int64_t id = stmt.col_int(0);
    return find(id).value_or(r);
}

std::vector<DisplayRequest> DisplayRequestRepository::query(const std::string& where, int64_t a,
                                                            int64_t b, bool two) {
    auto stmt = db_.prepare(
        "SELECT d.id, d.event_id, d.member_id, COALESCE(m.display_name,''), d.title, d.description, "
        "d.width_in, d.depth_in, d.needs_power, d.notes, d.status, d.table_assignment, d.created_at "
        "FROM event_display_requests d LEFT JOIN members m ON m.id = d.member_id WHERE " + where +
        " ORDER BY CASE d.status WHEN 'approved' THEN 0 WHEN 'pending' THEN 1 ELSE 2 END, d.created_at, d.id");
    stmt.bind(1, a);
    if (two) stmt.bind(2, b);
    std::vector<DisplayRequest> out;
    while (stmt.step()) {
        DisplayRequest r;
        r.id = stmt.col_int(0);
        r.event_id = stmt.col_int(1);
        r.member_id = stmt.col_int(2);
        r.member_display_name = stmt.col_text(3);
        r.title = stmt.col_text(4);
        r.description = stmt.col_text(5);
        r.width_in = static_cast<int>(stmt.col_int(6));
        r.depth_in = static_cast<int>(stmt.col_int(7));
        r.needs_power = stmt.col_int(8) != 0;
        r.notes = stmt.col_text(9);
        r.status = stmt.col_text(10);
        r.table_assignment = stmt.col_text(11);
        r.created_at = stmt.col_text(12);
        out.push_back(std::move(r));
    }
    return out;
}

std::optional<DisplayRequest> DisplayRequestRepository::find(int64_t id) {
    auto rows = query("d.id=?", id);
    if (rows.empty()) return std::nullopt;
    return rows.front();
}

std::vector<DisplayRequest> DisplayRequestRepository::list_for_event(int64_t event_id) {
    return query("d.event_id=?", event_id);
}

std::vector<DisplayRequest> DisplayRequestRepository::list_for_member(int64_t event_id, int64_t member_id) {
    return query("d.event_id=? AND d.member_id=?", event_id, member_id, true);
}

bool DisplayRequestRepository::review(int64_t id, const std::string& status,
                                      const std::string& table_assignment) {
    auto stmt = db_.prepare(
        "UPDATE event_display_requests SET status=?, table_assignment=?, updated_at=datetime('now') "
        "WHERE id=? RETURNING id");
    stmt.bind(1, status);
    stmt.bind(2, table_assignment);
    stmt.bind(3, id);
    return stmt.step();
}

bool DisplayRequestRepository::remove(int64_t id) {
    auto stmt = db_.prepare("DELETE FROM event_display_requests WHERE id=? RETURNING id");
    stmt.bind(1, id);
    return stmt.step();
}
