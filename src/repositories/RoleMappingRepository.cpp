#include "repositories/RoleMappingRepository.hpp"

RoleMappingRepository::RoleMappingRepository(SqliteDatabase& db) : db_(db) {}

// static
int RoleMappingRepository::role_rank(const std::string& lug_role) {
    if (lug_role == "admin")        return 3;
    if (lug_role == "chapter_lead") return 2;
    if (lug_role == "member")       return 1;
    return 0;
}

std::vector<RoleMapping> RoleMappingRepository::find_all() const {
    auto stmt = db_.prepare(
        "SELECT discord_role_id, discord_role_name, lug_role "
        "FROM discord_role_mappings ORDER BY discord_role_name ASC");
    std::vector<RoleMapping> result;
    while (stmt.step()) {
        RoleMapping m;
        m.discord_role_id   = stmt.col_text(0);
        m.discord_role_name = stmt.col_text(1);
        m.lug_role          = stmt.col_text(2);
        result.push_back(std::move(m));
    }
    return result;
}

std::optional<std::string> RoleMappingRepository::resolve_lug_role(
        const std::vector<std::string>& discord_role_ids) const {
    if (discord_role_ids.empty()) return std::nullopt;

    std::string best_role;
    int best_rank = 0;

    for (const auto& did : discord_role_ids) {
        auto stmt = db_.prepare(
            "SELECT lug_role FROM discord_role_mappings WHERE discord_role_id=?");
        stmt.bind(1, did);
        if (stmt.step()) {
            std::string lug_role = stmt.col_text(0);
            int rank = role_rank(lug_role);
            if (rank > best_rank) {
                best_rank = rank;
                best_role = lug_role;
            }
        }
    }

    if (best_rank == 0) return std::nullopt;
    return best_role;
}

void RoleMappingRepository::upsert(const std::string& discord_role_id,
                                    const std::string& discord_role_name,
                                    const std::string& lug_role) {
    auto stmt = db_.prepare(
        "INSERT INTO discord_role_mappings(discord_role_id, discord_role_name, lug_role) "
        "VALUES(?,?,?) ON CONFLICT(discord_role_id) DO UPDATE SET "
        "discord_role_name=excluded.discord_role_name, lug_role=excluded.lug_role");
    stmt.bind(1, discord_role_id);
    stmt.bind(2, discord_role_name);
    stmt.bind(3, lug_role);
    stmt.step();
}

void RoleMappingRepository::remove(const std::string& discord_role_id) {
    auto stmt = db_.prepare("DELETE FROM discord_role_mappings WHERE discord_role_id=?");
    stmt.bind(1, discord_role_id);
    stmt.step();
}
