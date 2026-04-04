#include "repositories/MemberRepository.hpp"
#include <set>

static const char* kSelectAllCols =
    "SELECT m.id, m.discord_user_id, m.discord_username, m.display_name, m.email, "
    "m.is_paid, m.paid_until, m.role, "
    "COALESCE((SELECT chapter_id FROM chapter_members WHERE member_id=m.id LIMIT 1),0), "
    "m.created_at, m.updated_at, COALESCE(m.first_name,''), COALESCE(m.last_name,''), "
    "COALESCE(m.birthday,''), COALESCE(m.fol_status,'afol'), "
    "COALESCE(m.phone,''), COALESCE(m.address_line1,''), COALESCE(m.address_line2,''), "
    "COALESCE(m.city,''), COALESCE(m.state,''), COALESCE(m.zip,''), "
    "COALESCE(m.sharing_email,'none'), COALESCE(m.sharing_phone,'none'), "
    "COALESCE(m.sharing_address,'none'), COALESCE(m.sharing_birthday,'none'), "
    "COALESCE(m.sharing_discord,'none') "
    "FROM members m";

MemberRepository::MemberRepository(SqliteDatabase& db) : db_(db) {}

Member MemberRepository::row_to_member(Statement& stmt) {
    Member m;
    m.id               = stmt.col_int(0);
    m.discord_user_id  = stmt.col_text(1);
    m.discord_username = stmt.col_text(2);
    m.display_name     = stmt.col_text(3);
    m.email            = stmt.col_is_null(4) ? "" : stmt.col_text(4);
    m.is_paid          = stmt.col_bool(5);
    m.paid_until       = stmt.col_is_null(6) ? "" : stmt.col_text(6);
    m.role             = stmt.col_text(7);
    m.chapter_id       = stmt.col_int(8); // from chapter_members subquery, 0 if none
    m.created_at       = stmt.col_text(9);
    m.updated_at       = stmt.col_text(10);
    m.first_name       = stmt.col_text(11);
    m.last_name        = stmt.col_text(12);
    m.birthday         = stmt.col_text(13);
    m.fol_status       = stmt.col_text(14);
    m.phone            = stmt.col_text(15);
    m.address_line1    = stmt.col_text(16);
    m.address_line2    = stmt.col_text(17);
    m.city             = stmt.col_text(18);
    m.state            = stmt.col_text(19);
    m.zip              = stmt.col_text(20);
    m.sharing_email    = stmt.col_text(21);
    m.sharing_phone    = stmt.col_text(22);
    m.sharing_address  = stmt.col_text(23);
    m.sharing_birthday = stmt.col_text(24);
    m.sharing_discord  = stmt.col_text(25);
    return m;
}

std::optional<Member> MemberRepository::find_by_id(int64_t id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE m.id=?");
    stmt.bind(1, id);
    if (stmt.step()) {
        return row_to_member(stmt);
    }
    return std::nullopt;
}

std::optional<Member> MemberRepository::find_by_discord_id(const std::string& discord_user_id) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE m.discord_user_id=?");
    stmt.bind(1, discord_user_id);
    if (stmt.step()) {
        return row_to_member(stmt);
    }
    return std::nullopt;
}

std::vector<Member> MemberRepository::find_all() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " ORDER BY m.display_name ASC");
    std::vector<Member> result;
    while (stmt.step()) {
        result.push_back(row_to_member(stmt));
    }
    return result;
}

std::vector<Member> MemberRepository::find_paid() {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE m.is_paid=1 ORDER BY m.display_name ASC");
    std::vector<Member> result;
    while (stmt.step()) {
        result.push_back(row_to_member(stmt));
    }
    return result;
}

std::vector<Member> MemberRepository::find_by_role(const std::string& role) {
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) + " WHERE m.role=? ORDER BY m.display_name ASC");
    stmt.bind(1, role);
    std::vector<Member> result;
    while (stmt.step()) {
        result.push_back(row_to_member(stmt));
    }
    return result;
}

std::vector<Member> MemberRepository::find_by_chapter(int64_t chapter_id) {
    auto stmt = db_.prepare(
        "SELECT m.id, m.discord_user_id, m.discord_username, m.display_name, m.email, "
        "m.is_paid, m.paid_until, m.role, cm.chapter_id, m.created_at, m.updated_at, "
        "COALESCE(m.first_name,''), COALESCE(m.last_name,''), "
        "COALESCE(m.birthday,''), COALESCE(m.fol_status,'afol'), "
        "COALESCE(m.phone,''), COALESCE(m.address_line1,''), COALESCE(m.address_line2,''), "
        "COALESCE(m.city,''), COALESCE(m.state,''), COALESCE(m.zip,''), "
        "m.pii_public "
        "FROM members m JOIN chapter_members cm ON cm.member_id=m.id "
        "WHERE cm.chapter_id=? ORDER BY m.display_name ASC");
    stmt.bind(1, chapter_id);
    std::vector<Member> result;
    while (stmt.step()) {
        result.push_back(row_to_member(stmt));
    }
    return result;
}

Member MemberRepository::create(const Member& m) {
    auto stmt = db_.prepare(
        "INSERT INTO members (discord_user_id, discord_username, first_name, last_name, display_name, "
        "email, is_paid, paid_until, role, birthday, fol_status, "
        "phone, address_line1, address_line2, city, state, zip, "
        "sharing_email, sharing_phone, sharing_address, sharing_birthday, sharing_discord) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    if (m.discord_user_id.empty()) stmt.bind_null(1);
    else                          stmt.bind(1, m.discord_user_id);
    stmt.bind(2, m.discord_username);
    stmt.bind(3, m.first_name);
    stmt.bind(4, m.last_name);
    stmt.bind(5, m.display_name);
    if (m.email.empty()) {
        stmt.bind_null(6);
    } else {
        stmt.bind(6, m.email);
    }
    stmt.bind(7, m.is_paid);
    if (m.paid_until.empty()) {
        stmt.bind_null(8);
    } else {
        stmt.bind(8, m.paid_until);
    }
    stmt.bind(9, m.role.empty() ? std::string("member") : m.role);
    stmt.bind(10, m.birthday);
    stmt.bind(11, m.fol_status.empty() ? std::string("afol") : m.fol_status);
    stmt.bind(12, m.phone);
    stmt.bind(13, m.address_line1);
    stmt.bind(14, m.address_line2);
    stmt.bind(15, m.city);
    stmt.bind(16, m.state);
    stmt.bind(17, m.zip);
    auto sv = [](const std::string& s) { return s.empty() ? std::string("none") : s; };
    stmt.bind(18, sv(m.sharing_email));
    stmt.bind(19, sv(m.sharing_phone));
    stmt.bind(20, sv(m.sharing_address));
    stmt.bind(21, sv(m.sharing_birthday));
    stmt.bind(22, sv(m.sharing_discord));
    stmt.step();

    int64_t new_id = db_.last_insert_rowid();
    auto result = find_by_id(new_id);
    if (!result) {
        throw DbError("Failed to retrieve inserted member with id=" + std::to_string(new_id));
    }
    return *result;
}

bool MemberRepository::update(const Member& m) {
    auto stmt = db_.prepare(
        "UPDATE members SET discord_username=?, first_name=?, last_name=?, display_name=?, email=?, "
        "is_paid=?, paid_until=?, role=?, birthday=?, fol_status=?, "
        "phone=?, address_line1=?, address_line2=?, city=?, state=?, zip=?, "
        "sharing_email=?, sharing_phone=?, sharing_address=?, sharing_birthday=?, sharing_discord=?, "
        "updated_at=datetime('now') WHERE id=?");
    stmt.bind(1, m.discord_username);
    stmt.bind(2, m.first_name);
    stmt.bind(3, m.last_name);
    stmt.bind(4, m.display_name);
    if (m.email.empty()) {
        stmt.bind_null(5);
    } else {
        stmt.bind(5, m.email);
    }
    stmt.bind(6, m.is_paid);
    if (m.paid_until.empty()) {
        stmt.bind_null(7);
    } else {
        stmt.bind(7, m.paid_until);
    }
    stmt.bind(8, m.role);
    stmt.bind(9, m.birthday);
    stmt.bind(10, m.fol_status.empty() ? std::string("afol") : m.fol_status);
    stmt.bind(11, m.phone);
    stmt.bind(12, m.address_line1);
    stmt.bind(13, m.address_line2);
    stmt.bind(14, m.city);
    stmt.bind(15, m.state);
    stmt.bind(16, m.zip);
    auto sv = [](const std::string& s) { return s.empty() ? std::string("none") : s; };
    stmt.bind(17, sv(m.sharing_email));
    stmt.bind(18, sv(m.sharing_phone));
    stmt.bind(19, sv(m.sharing_address));
    stmt.bind(20, sv(m.sharing_birthday));
    stmt.bind(21, sv(m.sharing_discord));
    stmt.bind(22, m.id);
    stmt.step();

    // Check if any row was actually updated by querying it back
    auto existing = find_by_id(m.id);
    return existing.has_value();
}

bool MemberRepository::delete_by_id(int64_t id) {
    auto stmt = db_.prepare("DELETE FROM members WHERE id=?");
    stmt.bind(1, id);
    stmt.step();

    // Verify deletion: try to find; if not found, deletion succeeded
    auto existing = find_by_id(id);
    return !existing.has_value();
}

std::vector<Member> MemberRepository::find_search(const std::string& q) {
    std::string pattern = "%" + q + "%";
    auto stmt = db_.prepare(
        std::string(kSelectAllCols) +
        " WHERE m.display_name LIKE ? OR m.discord_username LIKE ? OR m.email LIKE ?"
        " ORDER BY m.display_name ASC");
    stmt.bind(1, pattern);
    stmt.bind(2, pattern);
    stmt.bind(3, pattern);
    std::vector<Member> result;
    while (stmt.step()) {
        result.push_back(row_to_member(stmt));
    }
    return result;
}

int MemberRepository::count_all() {
    auto stmt = db_.prepare("SELECT COUNT(*) FROM members");
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

int MemberRepository::count_search(const std::string& q) {
    if (q.empty()) return count_all();
    std::string pat = "%" + q + "%";
    auto stmt = db_.prepare(
        "SELECT COUNT(*) FROM members "
        "WHERE display_name LIKE ? OR discord_username LIKE ? OR email LIKE ?");
    stmt.bind(1, pat); stmt.bind(2, pat); stmt.bind(3, pat);
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}

std::vector<Member> MemberRepository::find_paginated(const std::string& q,
                                                      const std::string& sort_col,
                                                      const std::string& sort_dir,
                                                      int limit, int offset) {
    // Whitelist sort columns to prevent SQL injection
    static const std::set<std::string> valid_cols = {
        "display_name", "discord_username", "email", "is_paid", "role", "chapter_name"
    };
    const std::string dir = (sort_dir == "desc") ? "DESC" : "ASC";
    // Map chapter_name to the joined alias
    std::string col;
    if (sort_col == "chapter_name") col = "c.name";
    else if (valid_cols.count(sort_col)) col = "m." + sort_col;
    else col = "m.display_name";

    static const char* kJoinCols =
        "SELECT m.id, m.discord_user_id, m.discord_username, m.display_name, m.email, "
        "m.is_paid, m.paid_until, m.role, COALESCE(cm.chapter_id,0), m.created_at, m.updated_at, "
        "COALESCE(c.name,''), COALESCE(m.first_name,''), COALESCE(m.last_name,''), "
        "COALESCE(m.birthday,''), COALESCE(m.fol_status,'afol'), "
        "COALESCE(m.phone,''), COALESCE(m.address_line1,''), COALESCE(m.address_line2,''), "
        "COALESCE(m.city,''), COALESCE(m.state,''), COALESCE(m.zip,''), "
        "COALESCE(m.sharing_email,'none'), COALESCE(m.sharing_phone,'none'), "
        "COALESCE(m.sharing_address,'none'), COALESCE(m.sharing_birthday,'none'), "
        "COALESCE(m.sharing_discord,'none') FROM members m "
        "LEFT JOIN (SELECT member_id, MIN(chapter_id) AS chapter_id FROM chapter_members GROUP BY member_id) cm ON cm.member_id=m.id "
        "LEFT JOIN chapters c ON c.id=cm.chapter_id";

    auto read_row = [](Statement& stmt) -> Member {
        Member m;
        m.id               = stmt.col_int(0);
        m.discord_user_id  = stmt.col_text(1);
        m.discord_username = stmt.col_text(2);
        m.display_name     = stmt.col_text(3);
        m.email            = stmt.col_is_null(4) ? "" : stmt.col_text(4);
        m.is_paid          = stmt.col_bool(5);
        m.paid_until       = stmt.col_is_null(6) ? "" : stmt.col_text(6);
        m.role             = stmt.col_text(7);
        m.chapter_id       = stmt.col_int(8);
        m.created_at       = stmt.col_text(9);
        m.updated_at       = stmt.col_text(10);
        m.chapter_name     = stmt.col_text(11);
        m.first_name       = stmt.col_text(12);
        m.last_name        = stmt.col_text(13);
        m.birthday         = stmt.col_text(14);
        m.fol_status       = stmt.col_text(15);
        m.phone            = stmt.col_text(16);
        m.address_line1    = stmt.col_text(17);
        m.address_line2    = stmt.col_text(18);
        m.city             = stmt.col_text(19);
        m.state            = stmt.col_text(20);
        m.zip              = stmt.col_text(21);
        m.sharing_email    = stmt.col_text(22);
        m.sharing_phone    = stmt.col_text(23);
        m.sharing_address  = stmt.col_text(24);
        m.sharing_birthday = stmt.col_text(25);
        m.sharing_discord  = stmt.col_text(26);
        return m;
    };

    std::vector<Member> result;
    if (q.empty()) {
        auto stmt = db_.prepare(
            std::string(kJoinCols) + " ORDER BY " + col + " " + dir + " LIMIT ? OFFSET ?");
        stmt.bind(1, static_cast<int64_t>(limit));
        stmt.bind(2, static_cast<int64_t>(offset));
        while (stmt.step()) result.push_back(read_row(stmt));
    } else {
        std::string pat = "%" + q + "%";
        auto stmt = db_.prepare(
            std::string(kJoinCols) +
            " WHERE m.display_name LIKE ? OR m.discord_username LIKE ? OR m.email LIKE ?"
            " ORDER BY " + col + " " + dir + " LIMIT ? OFFSET ?");
        stmt.bind(1, pat); stmt.bind(2, pat); stmt.bind(3, pat);
        stmt.bind(4, static_cast<int64_t>(limit));
        stmt.bind(5, static_cast<int64_t>(offset));
        while (stmt.step()) result.push_back(read_row(stmt));
    }
    return result;
}

bool MemberRepository::set_chapter(int64_t id, int64_t chapter_id) {
    // Remove from all chapters first
    {
        auto stmt = db_.prepare("DELETE FROM chapter_members WHERE member_id=?");
        stmt.bind(1, id);
        stmt.step();
    }
    // Add to new chapter with default "member" role if specified
    if (chapter_id > 0) {
        auto stmt = db_.prepare(
            "INSERT INTO chapter_members(member_id, chapter_id, chapter_role) "
            "VALUES(?,?,'member') ON CONFLICT(member_id, chapter_id) DO NOTHING");
        stmt.bind(1, id);
        stmt.bind(2, chapter_id);
        stmt.step();
    }
    return find_by_id(id).has_value();
}

bool MemberRepository::set_paid(int64_t id, bool is_paid, const std::string& paid_until) {
    auto stmt = db_.prepare(
        "UPDATE members SET is_paid=?, paid_until=?, updated_at=datetime('now') WHERE id=?");
    stmt.bind(1, is_paid);
    if (paid_until.empty()) {
        stmt.bind_null(2);
    } else {
        stmt.bind(2, paid_until);
    }
    stmt.bind(3, id);
    stmt.step();

    auto existing = find_by_id(id);
    return existing.has_value();
}
