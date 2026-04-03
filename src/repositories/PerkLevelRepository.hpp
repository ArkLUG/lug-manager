#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/PerkLevel.hpp"
#include <vector>
#include <optional>
#include <string>

class PerkLevelRepository {
public:
    explicit PerkLevelRepository(SqliteDatabase& db);

    std::vector<PerkLevel>   find_all();           // ordered by sort_order
    std::optional<PerkLevel> find_by_id(int64_t id);

    PerkLevel create(const PerkLevel& p);
    bool      update(const PerkLevel& p);
    bool      remove(int64_t id);

private:
    SqliteDatabase& db_;
    static PerkLevel row_to_perk(Statement& stmt);
};
