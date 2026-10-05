#include "repositories/members/DuesRepository.hpp"

DuesRepository::DuesRepository(SqliteDatabase& db) : db_(db) {}

void DuesRepository::record(int64_t member_id, const std::string& paid_on, int64_t amount_cents,
                            const std::string& method, const std::string& covers_until,
                            const std::string& note, int64_t recorded_by) {
    Transaction tx(db_);
    {
        auto ins = db_.prepare(
            "INSERT INTO dues_payments (member_id, paid_on, amount_cents, method, covers_until, note, recorded_by) "
            "VALUES (?,?,?,?,?,?,?)");
        ins.bind(1, member_id);
        ins.bind(2, paid_on);
        ins.bind(3, amount_cents);
        ins.bind(4, method);
        ins.bind(5, covers_until);
        ins.bind(6, note);
        if (recorded_by > 0) ins.bind(7, recorded_by); else ins.bind_null(7);
        ins.step();
    }
    {
        auto upd = db_.prepare(
            "UPDATE members SET is_paid=1, "
            "  paid_until = CASE WHEN COALESCE(paid_until,'') > ? THEN paid_until ELSE ? END, "
            "  updated_at = datetime('now') WHERE id=?");
        upd.bind(1, covers_until);
        upd.bind(2, covers_until);
        upd.bind(3, member_id);
        upd.step();
    }
    tx.commit();
}

std::vector<DuesPayment> DuesRepository::history(int64_t member_id) {
    auto stmt = db_.prepare(
        "SELECT d.id, d.member_id, d.paid_on, d.amount_cents, d.method, d.covers_until, d.note, "
        "COALESCE(r.display_name,'') FROM dues_payments d LEFT JOIN members r ON r.id = d.recorded_by "
        "WHERE d.member_id=? ORDER BY d.paid_on DESC, d.id DESC");
    stmt.bind(1, member_id);
    std::vector<DuesPayment> out;
    while (stmt.step()) {
        DuesPayment p;
        p.id = stmt.col_int(0);
        p.member_id = stmt.col_int(1);
        p.paid_on = stmt.col_text(2);
        p.amount_cents = stmt.col_int(3);
        p.method = stmt.col_text(4);
        p.covers_until = stmt.col_text(5);
        p.note = stmt.col_text(6);
        p.recorded_by_name = stmt.col_text(7);
        out.push_back(std::move(p));
    }
    return out;
}

bool DuesRepository::remove(int64_t payment_id, int64_t member_id) {
    auto stmt = db_.prepare("DELETE FROM dues_payments WHERE id=? AND member_id=? RETURNING id");
    stmt.bind(1, payment_id);
    stmt.bind(2, member_id);
    return stmt.step();
}

std::optional<DuesPayment> DuesRepository::find(int64_t payment_id, int64_t member_id) {
    auto st = db_.prepare("SELECT id, member_id, paid_on, amount_cents, method, covers_until, note FROM dues_payments "
                          "WHERE id=? AND member_id=?");
    st.bind(1, payment_id); st.bind(2, member_id);
    if (!st.step()) return std::nullopt;
    DuesPayment p;
    p.id = st.col_int(0); p.member_id = st.col_int(1); p.paid_on = st.col_text(2); p.amount_cents = st.col_int(3);
    p.method = st.col_text(4); p.covers_until = st.col_text(5); p.note = st.col_text(6);
    return p;
}

namespace {
std::string paid_until_of(SqliteDatabase& db, int64_t member_id) {
    auto st = db.prepare("SELECT COALESCE(paid_until,'') FROM members WHERE id=?");
    st.bind(1, member_id);
    return st.step() ? st.col_text(0) : "";
}
std::string latest_cover(SqliteDatabase& db, int64_t member_id) {
    auto st = db.prepare("SELECT COALESCE(MAX(covers_until),'') FROM dues_payments WHERE member_id=?");
    st.bind(1, member_id);
    return st.step() ? st.col_text(0) : "";
}
void set_paid_until(SqliteDatabase& db, int64_t member_id, const std::string& until) {
    // Moving it into the future marks them paid; an earlier date is left for
    // the daily check (which applies the grace period) to expire.
    auto up = db.prepare("UPDATE members SET paid_until=?, is_paid = CASE WHEN ? >= date('now','localtime') THEN 1 ELSE is_paid END, "
                         "updated_at=datetime('now') WHERE id=?");
    up.bind(1, until); up.bind(2, until); up.bind(3, member_id);
    up.step();
}
}

bool DuesRepository::update(int64_t payment_id, int64_t member_id, const std::string& paid_on, int64_t amount_cents,
                            const std::string& method, const std::string& covers_until, const std::string& note) {
    Transaction tx(db_);
    auto before = find(payment_id, member_id);
    if (!before) return false;
    {
        auto up = db_.prepare("UPDATE dues_payments SET paid_on=?, amount_cents=?, method=?, covers_until=?, note=? WHERE id=? AND member_id=?");
        up.bind(1, paid_on); up.bind(2, amount_cents); up.bind(3, method); up.bind(4, covers_until); up.bind(5, note);
        up.bind(6, payment_id); up.bind(7, member_id);
        up.step();
    }
    const std::string current = paid_until_of(db_, member_id);
    std::string next = current;
    if (before->covers_until == current) next = latest_cover(db_, member_id);   // it set paid_until: follow it
    if (covers_until > next) next = covers_until;
    if (next != current) set_paid_until(db_, member_id, next);
    tx.commit();
    return true;
}

std::string DuesRepository::refit_paid_until(int64_t member_id, const std::string& removed_covers_until) {
    const std::string current = paid_until_of(db_, member_id);
    if (removed_covers_until != current) return current;
    const std::string latest = latest_cover(db_, member_id);
    if (latest.empty() || latest == current) return current;
    set_paid_until(db_, member_id, latest);
    return latest;
}

std::vector<DuesStatusRow> DuesRepository::rows(Statement& stmt) {
    std::vector<DuesStatusRow> out;
    while (stmt.step()) {
        DuesStatusRow r;
        r.member_id = stmt.col_int(0);
        r.display_name = stmt.col_text(1);
        r.discord_user_id = stmt.col_text(2);
        r.paid_until = stmt.col_text(3);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<DuesStatusRow> DuesRepository::expiring_between(const std::string& today, const std::string& until) {
    auto stmt = db_.prepare(
        "SELECT id, display_name, COALESCE(discord_user_id,''), paid_until FROM members "
        "WHERE is_paid=1 AND COALESCE(paid_until,'') <> '' AND paid_until >= ? AND paid_until <= ? "
        "ORDER BY paid_until, display_name");
    stmt.bind(1, today);
    stmt.bind(2, until);
    return rows(stmt);
}

std::vector<DuesStatusRow> DuesRepository::paid_without_payment() {
    auto stmt = db_.prepare(
        "SELECT id, display_name, COALESCE(discord_user_id,''), paid_until FROM members m "
        "WHERE is_paid=1 AND COALESCE(paid_until,'') <> '' AND NOT EXISTS ("
        "  SELECT 1 FROM dues_payments d WHERE d.member_id = m.id AND d.covers_until >= m.paid_until) "
        "ORDER BY display_name");
    return rows(stmt);
}

std::vector<DuesStatusRow> DuesRepository::expire_lapsed(const std::string& today) {
    auto stmt = db_.prepare(
        "UPDATE members SET is_paid=0, updated_at=datetime('now') "
        "WHERE is_paid=1 AND COALESCE(paid_until,'') <> '' AND paid_until < ? "
        "RETURNING id, display_name, COALESCE(discord_user_id,''), paid_until");
    stmt.bind(1, today);
    return rows(stmt);
}

std::vector<DuesStatusRow> DuesRepository::needing_reminder(const std::string& today, const std::string& until) {
    auto stmt = db_.prepare(
        "SELECT id, display_name, COALESCE(discord_user_id,''), paid_until FROM members "
        "WHERE is_paid=1 AND COALESCE(paid_until,'') <> '' AND paid_until >= ? AND paid_until <= ? "
        "  AND (COALESCE(discord_user_id,'') <> '' OR COALESCE(email,'') <> '') AND dues_reminded_for <> paid_until "
        "  AND NOT EXISTS (SELECT 1 FROM notification_optouts o WHERE o.member_id = members.id AND o.kind = 'dues_reminder')");
    stmt.bind(1, today);
    stmt.bind(2, until);
    return rows(stmt);
}

void DuesRepository::mark_reminded(int64_t member_id, const std::string& paid_until) {
    auto stmt = db_.prepare("UPDATE members SET dues_reminded_for=? WHERE id=?");
    stmt.bind(1, paid_until);
    stmt.bind(2, member_id);
    stmt.step();
}
