#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/PendingDiscordMatch.hpp"
#include <vector>
#include <optional>
#include <string>

class PendingDiscordMatchRepository {
public:
    explicit PendingDiscordMatchRepository(SqliteDatabase& db);

    std::vector<PendingDiscordMatch>   find_all_unresolved() const;
    std::optional<PendingDiscordMatch> find_by_id(int64_t id) const;
    // Finds an unresolved row for this discord_user_id, if any — used to avoid
    // creating duplicate pending rows across repeated sync runs.
    std::optional<PendingDiscordMatch> find_unresolved_by_discord_id(const std::string& discord_user_id) const;

    PendingDiscordMatch create(const PendingDiscordMatch& p); // returns row with id/created_at set
    bool mark_resolved(int64_t id, const std::string& action, int64_t resolved_member_id);

private:
    SqliteDatabase& db_;
    static PendingDiscordMatch row_to_match(Statement& stmt);
};
