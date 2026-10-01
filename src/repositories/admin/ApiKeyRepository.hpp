#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/ApiKey.hpp"
#include <optional>
#include <string>
#include <vector>

class ApiKeyRepository {
public:
    explicit ApiKeyRepository(SqliteDatabase& db);

    std::vector<ApiKey>   find_all() const;               // newest first, joined with members.display_name
    std::optional<ApiKey> find_by_id(int64_t id) const;
    std::optional<ApiKey> find_by_hash(const std::string& key_hash) const; // active only (revoked_at = '')

    // Returns the created row (id/created_at populated). Caller supplies key_hash (already hashed).
    ApiKey create(const std::string& key_hash, const std::string& label,
                  const std::string& scope, int64_t created_by);

    void revoke(int64_t id);          // sets revoked_at = now
    void touch_last_used(int64_t id); // sets last_used_at = now (best-effort)

private:
    SqliteDatabase& db_;
    static ApiKey row_to_key(Statement& stmt);
};
