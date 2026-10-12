#pragma once
#include "db/SqliteDatabase.hpp"
#include <string>
#include <vector>

// Money by chapter (migration 078): each dues payment's chapter share
// (amount x chapter_pct / 100, rounded down; the LUG keeps the rest and all
// of it when the member had no chapter), plus income and expenses marked for
// a chapter. Everything else is the LUG's. The buckets add up to the LUG-wide
// totals exactly.
namespace chapter_money {

struct Bucket {
    int64_t chapter_id = 0;          // 0 = the LUG in general
    std::string name;
    int64_t dues = 0, income = 0, expense = 0;   // within [from, to)
    int64_t opening = 0;                         // everything before `from`
    int64_t net() const { return dues + income - expense; }
    int64_t closing() const { return opening + net(); }
};

namespace detail {
// cents of the dues share / income / expense for chapter `ch` (0 = all chapters) in [from, to)
inline int64_t dues_share(SqliteDatabase& db, int64_t ch, const std::string& from, const std::string& to) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount_cents * COALESCE(chapter_pct,0) / 100),0) FROM dues_payments "
                         "WHERE chapter_id IS NOT NULL AND (?1 = 0 OR chapter_id = ?1) AND paid_on >= ?2 AND paid_on < ?3");
    st.bind(1, ch); st.bind(2, from); st.bind(3, to);
    return st.step() ? st.col_int(0) : 0;
}
inline int64_t entries(SqliteDatabase& db, int64_t ch, const char* kind, const std::string& from, const std::string& to) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount_cents),0) FROM treasury_entries "
                         "WHERE kind = ?4 AND (CASE WHEN ?1 = 0 THEN chapter_id IS NULL ELSE chapter_id = ?1 END) "
                         "AND entry_on >= ?2 AND entry_on < ?3");
    st.bind(1, ch); st.bind(2, from); st.bind(3, to); st.bind(4, std::string(kind));
    return st.step() ? st.col_int(0) : 0;
}
inline int64_t all_dues(SqliteDatabase& db, const std::string& from, const std::string& to) {
    auto st = db.prepare("SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments WHERE paid_on >= ? AND paid_on < ?");
    st.bind(1, from); st.bind(2, to);
    return st.step() ? st.col_int(0) : 0;
}
}

// The LUG first, then every chapter by name.
inline std::vector<Bucket> buckets(SqliteDatabase& db, const std::string& from, const std::string& to) {
    static const std::string start = "0000-01-01";
    std::vector<Bucket> out;
    Bucket lug; lug.name = "LUG (general)";
    lug.dues = detail::all_dues(db, from, to) - detail::dues_share(db, 0, from, to);
    lug.income = detail::entries(db, 0, "income", from, to);
    lug.expense = detail::entries(db, 0, "expense", from, to);
    lug.opening = detail::all_dues(db, start, from) - detail::dues_share(db, 0, start, from)
                + detail::entries(db, 0, "income", start, from) - detail::entries(db, 0, "expense", start, from);
    out.push_back(lug);
    auto st = db.prepare("SELECT id, name FROM chapters ORDER BY name COLLATE NOCASE");
    while (st.step()) {
        Bucket b; b.chapter_id = st.col_int(0); b.name = st.col_text(1);
        b.dues = detail::dues_share(db, b.chapter_id, from, to);
        b.income = detail::entries(db, b.chapter_id, "income", from, to);
        b.expense = detail::entries(db, b.chapter_id, "expense", from, to);
        b.opening = detail::dues_share(db, b.chapter_id, start, from)
                  + detail::entries(db, b.chapter_id, "income", start, from) - detail::entries(db, b.chapter_id, "expense", start, from);
        out.push_back(b);
    }
    return out;
}

} // namespace chapter_money
