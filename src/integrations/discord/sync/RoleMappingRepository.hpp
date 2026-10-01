#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/RoleMapping.hpp"
#include <vector>
#include <optional>
#include <string>

class RoleMappingRepository {
public:
    explicit RoleMappingRepository(SqliteDatabase& db);

    std::vector<RoleMapping> find_all() const;

    // Returns the highest-privilege LUG role matching any of the given Discord role IDs.
    // Priority: admin > chapter_lead > member. Returns nullopt if none match.
    std::optional<std::string> resolve_lug_role(const std::vector<std::string>& discord_role_ids) const;

    void upsert(const std::string& discord_role_id,
                const std::string& discord_role_name,
                const std::string& lug_role);

    void remove(const std::string& discord_role_id);

private:
    SqliteDatabase& db_;

    static int role_rank(const std::string& lug_role);
};
