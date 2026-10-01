#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Member.hpp"
#include <vector>
#include <unordered_map>
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
    std::vector<Member>   find_without_discord_id();
    std::vector<Member>   find_search(const std::string& q);
    std::vector<Member>   find_paginated(const std::string& q,
                                         const std::string& sort_col,
                                         const std::string& sort_dir,
                                         int limit, int offset);
    int count_all();
    int count_search(const std::string& q);

    Member create(const Member& m);  // Returns member with id and timestamps set
    bool   update(const Member& m);  // Returns false if not found
    // members.role_source: "manual" | "discord" - see services/RoleSync.hpp
    std::string get_role_source(int64_t id);

    // Guardian / consent details for young members (migration 056).
    struct Guardian {
        std::string name, phone, email, consent_date;
        bool consent_on_file = false, photo_release = false;
    };
    Guardian get_guardian(int64_t id);
    void     set_guardian(int64_t id, const Guardian& g);
    // For check-in lists: minors (KFOL/TFOL) among `ids` -> {consent_on_file, photo_release}.
    std::unordered_map<int64_t, std::pair<bool, bool>> minor_flags(const std::vector<int64_t>& ids);
    // Exact first+last name match (case-insensitive, indexed - migration 053).
    std::optional<Member> find_by_full_name(const std::string& first, const std::string& last);
    // Name-only search for the public check-in page (never matches email etc.).
    std::vector<Member> search_names(const std::string& q, int limit);
    // Personal calendar feed token (migration 052): SHA-256 of the token.
    void        set_calendar_token_hash(int64_t id, const std::string& hash);
    int64_t     find_by_calendar_token_hash(const std::string& hash); // 0 if none
    void        set_role_source(int64_t id, const std::string& source);
    bool   delete_by_id(int64_t id);

    // Convenience: set paid status
    bool set_paid(int64_t id, bool is_paid, const std::string& paid_until);
    // Convenience: set chapter assignment (0 = clear)
    bool set_chapter(int64_t id, int64_t chapter_id);
    // Link an existing member (created without a Discord account) to a Discord
    // identity. Deliberately narrow — does NOT go through update() so it can't
    // accidentally overwrite unrelated fields.
    bool link_discord_id(int64_t member_id, const std::string& discord_user_id,
                          const std::string& discord_username);

    SqliteDatabase& db() { return db_; }

private:
    SqliteDatabase& db_;
    static Member row_to_member(Statement& stmt);
};
