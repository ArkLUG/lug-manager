#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/PerkLevel.hpp"
#include <vector>
#include <optional>
#include <string>

// FOL rank: kfol(0) < tfol(1) < afol(2)
inline int fol_rank(const std::string& fol) {
    if (fol == "afol") return 2;
    if (fol == "tfol") return 1;
    return 0;
}

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
