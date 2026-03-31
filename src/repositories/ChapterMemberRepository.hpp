#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/ChapterMember.hpp"
#include <vector>
#include <optional>
#include <string>
#include <cstdint>
#include <map>

struct ChapterStats {
    int member_count = 0;
    int paid_count   = 0;
    std::vector<std::string> lead_names;
};

class ChapterMemberRepository {
public:
    explicit ChapterMemberRepository(SqliteDatabase& db);

    // Returns the chapter_role for (member_id, chapter_id), or nullopt if not a member
    std::optional<std::string> get_chapter_role(int64_t member_id, int64_t chapter_id) const;

    // All members of a chapter (joined with member display info)
    std::vector<ChapterMember> find_by_chapter(int64_t chapter_id) const;

    // All chapter memberships for a member (joined with chapter name)
    std::vector<ChapterMember> find_by_member(int64_t member_id) const;

    // Per-chapter stats (member count, paid count, lead names) for all chapters
    std::map<int64_t, ChapterStats> get_all_chapter_stats() const;

    // Upsert: create or update a member's role in a chapter
    void upsert(int64_t member_id, int64_t chapter_id,
                const std::string& chapter_role, int64_t granted_by);

    // Remove a member from a chapter
    void remove(int64_t member_id, int64_t chapter_id);

private:
    SqliteDatabase& db_;
};
