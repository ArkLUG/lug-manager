#include "repositories/ChapterMemberRepository.hpp"
#include <map>

ChapterMemberRepository::ChapterMemberRepository(SqliteDatabase& db) : db_(db) {}

std::optional<std::string> ChapterMemberRepository::get_chapter_role(
        int64_t member_id, int64_t chapter_id) const {
    auto stmt = db_.prepare(
        "SELECT chapter_role FROM chapter_members WHERE member_id=? AND chapter_id=?");
    stmt.bind(1, member_id);
    stmt.bind(2, chapter_id);
    if (stmt.step()) {
        return stmt.col_text(0);
    }
    return std::nullopt;
}

std::vector<ChapterMember> ChapterMemberRepository::find_by_chapter(int64_t chapter_id) const {
    auto stmt = db_.prepare(
        "SELECT cm.member_id, cm.chapter_id, cm.chapter_role, "
        "       COALESCE(cm.granted_by, 0), COALESCE(cm.granted_at, ''), "
        "       m.display_name, m.discord_username "
        "FROM chapter_members cm "
        "JOIN members m ON m.id = cm.member_id "
        "WHERE cm.chapter_id=? "
        "ORDER BY CASE cm.chapter_role WHEN 'lead' THEN 0 WHEN 'event_manager' THEN 1 ELSE 2 END, "
        "         m.display_name ASC");
    stmt.bind(1, chapter_id);
    std::vector<ChapterMember> result;
    while (stmt.step()) {
        ChapterMember cm;
        cm.member_id       = stmt.col_int(0);
        cm.chapter_id      = stmt.col_int(1);
        cm.chapter_role    = stmt.col_text(2);
        cm.granted_by      = stmt.col_int(3);
        cm.granted_at      = stmt.col_text(4);
        cm.display_name    = stmt.col_text(5);
        cm.discord_username= stmt.col_text(6);
        result.push_back(std::move(cm));
    }
    return result;
}

std::vector<ChapterMember> ChapterMemberRepository::find_by_member(int64_t member_id) const {
    auto stmt = db_.prepare(
        "SELECT cm.member_id, cm.chapter_id, cm.chapter_role, "
        "       COALESCE(cm.granted_by, 0), COALESCE(cm.granted_at, ''), "
        "       '', '', c.name "
        "FROM chapter_members cm "
        "JOIN chapters c ON c.id = cm.chapter_id "
        "WHERE cm.member_id=? ORDER BY c.name ASC");
    stmt.bind(1, member_id);
    std::vector<ChapterMember> result;
    while (stmt.step()) {
        ChapterMember cm;
        cm.member_id    = stmt.col_int(0);
        cm.chapter_id   = stmt.col_int(1);
        cm.chapter_role = stmt.col_text(2);
        cm.granted_by   = stmt.col_int(3);
        cm.granted_at   = stmt.col_text(4);
        // col 5 and 6 are empty strings (not needed when querying by member)
        cm.chapter_name = stmt.col_text(7);
        result.push_back(std::move(cm));
    }
    return result;
}

std::map<int64_t, ChapterStats> ChapterMemberRepository::get_all_chapter_stats() const {
    std::map<int64_t, ChapterStats> stats;

    // Member count + paid count per chapter
    {
        auto stmt = db_.prepare(
            "SELECT cm.chapter_id, COUNT(*), "
            "       SUM(CASE WHEN m.is_paid = 1 THEN 1 ELSE 0 END) "
            "FROM chapter_members cm "
            "JOIN members m ON m.id = cm.member_id "
            "GROUP BY cm.chapter_id");
        while (stmt.step()) {
            int64_t cid           = stmt.col_int(0);
            stats[cid].member_count = static_cast<int>(stmt.col_int(1));
            stats[cid].paid_count   = static_cast<int>(stmt.col_int(2));
        }
    }

    // Lead names per chapter
    {
        auto stmt = db_.prepare(
            "SELECT cm.chapter_id, m.display_name "
            "FROM chapter_members cm "
            "JOIN members m ON m.id = cm.member_id "
            "WHERE cm.chapter_role = 'lead' "
            "ORDER BY m.display_name ASC");
        while (stmt.step()) {
            int64_t cid = stmt.col_int(0);
            stats[cid].lead_names.push_back(stmt.col_text(1));
        }
    }

    return stats;
}

void ChapterMemberRepository::upsert(int64_t member_id, int64_t chapter_id,
                                      const std::string& chapter_role, int64_t granted_by) {
    auto stmt = db_.prepare(
        "INSERT INTO chapter_members(member_id, chapter_id, chapter_role, granted_by) "
        "VALUES(?,?,?,?) ON CONFLICT(member_id, chapter_id) DO UPDATE SET "
        "chapter_role=excluded.chapter_role, granted_by=excluded.granted_by, "
        "granted_at=datetime('now')");
    stmt.bind(1, member_id);
    stmt.bind(2, chapter_id);
    stmt.bind(3, chapter_role);
    if (granted_by == 0) stmt.bind_null(4); else stmt.bind(4, granted_by);
    stmt.step();
}

void ChapterMemberRepository::remove(int64_t member_id, int64_t chapter_id) {
    auto stmt = db_.prepare(
        "DELETE FROM chapter_members WHERE member_id=? AND chapter_id=?");
    stmt.bind(1, member_id);
    stmt.bind(2, chapter_id);
    stmt.step();
}
