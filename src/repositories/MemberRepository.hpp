#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Member.hpp"
#include <vector>
#include <optional>
#include <string>

class MemberRepository {
public:
    explicit MemberRepository(SqliteDatabase& db);

    std::optional<Member> find_by_id(int64_t id);
    std::optional<Member> find_by_discord_id(const std::string& discord_user_id);
    std::vector<Member>   find_all();
    std::vector<Member>   find_paid();
    std::vector<Member>   find_by_role(const std::string& role);
    std::vector<Member>   find_by_chapter(int64_t chapter_id);
    std::vector<Member>   find_search(const std::string& q);
    std::vector<Member>   find_paginated(const std::string& q,
                                         const std::string& sort_col,
                                         const std::string& sort_dir,
                                         int limit, int offset);
    int count_all();
    int count_search(const std::string& q);

    Member create(const Member& m);  // Returns member with id and timestamps set
    bool   update(const Member& m);  // Returns false if not found
    bool   delete_by_id(int64_t id);

    // Convenience: set paid status
    bool set_paid(int64_t id, bool is_paid, const std::string& paid_until);
    // Convenience: set chapter assignment (0 = clear)
    bool set_chapter(int64_t id, int64_t chapter_id);

private:
    SqliteDatabase& db_;
    static Member row_to_member(Statement& stmt);
};
