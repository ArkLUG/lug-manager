#include "repositories/ApiKeyRepository.hpp"

ApiKeyRepository::ApiKeyRepository(SqliteDatabase& db) : db_(db) {}

// static
ApiKey ApiKeyRepository::row_to_key(Statement& stmt) {
    ApiKey k;
    k.id              = stmt.col_int(0);
    k.key_hash        = stmt.col_text(1);
    k.label           = stmt.col_text(2);
    k.scope           = stmt.col_text(3);
    k.created_by      = stmt.col_int(4);
    k.created_at      = stmt.col_text(5);
    k.last_used_at    = stmt.col_text(6);
    k.revoked_at      = stmt.col_text(7);
    k.created_by_name = stmt.col_is_null(8) ? "" : stmt.col_text(8);
    return k;
}

std::vector<ApiKey> ApiKeyRepository::find_all() const {
    auto stmt = db_.prepare(
        "SELECT k.id, k.key_hash, k.label, k.scope, k.created_by, k.created_at, "
        "       k.last_used_at, k.revoked_at, m.display_name "
        "FROM api_keys k LEFT JOIN members m ON m.id = k.created_by "
        "ORDER BY k.created_at DESC");
    std::vector<ApiKey> result;
    while (stmt.step()) {
        result.push_back(row_to_key(stmt));
    }
    return result;
}

std::optional<ApiKey> ApiKeyRepository::find_by_id(int64_t id) const {
    auto stmt = db_.prepare(
        "SELECT k.id, k.key_hash, k.label, k.scope, k.created_by, k.created_at, "
        "       k.last_used_at, k.revoked_at, m.display_name "
        "FROM api_keys k LEFT JOIN members m ON m.id = k.created_by "
        "WHERE k.id = ?");
    stmt.bind(1, id);
    if (stmt.step()) return row_to_key(stmt);
    return std::nullopt;
}

std::optional<ApiKey> ApiKeyRepository::find_by_hash(const std::string& key_hash) const {
    auto stmt = db_.prepare(
        "SELECT k.id, k.key_hash, k.label, k.scope, k.created_by, k.created_at, "
        "       k.last_used_at, k.revoked_at, m.display_name "
        "FROM api_keys k LEFT JOIN members m ON m.id = k.created_by "
        "WHERE k.key_hash = ? AND k.revoked_at = ''");
    stmt.bind(1, key_hash);
    if (stmt.step()) return row_to_key(stmt);
    return std::nullopt;
}

ApiKey ApiKeyRepository::create(const std::string& key_hash, const std::string& label,
                                 const std::string& scope, int64_t created_by) {
    auto stmt = db_.prepare(
        "INSERT INTO api_keys(key_hash, label, scope, created_by) VALUES(?,?,?,?)");
    stmt.bind(1, key_hash);
    stmt.bind(2, label);
    stmt.bind(3, scope);
    stmt.bind(4, created_by);
    stmt.step();

    int64_t id = db_.last_insert_rowid();
    auto created = find_by_id(id);
    return created ? *created : ApiKey{};
}

void ApiKeyRepository::revoke(int64_t id) {
    auto stmt = db_.prepare("UPDATE api_keys SET revoked_at = datetime('now') WHERE id = ?");
    stmt.bind(1, id);
    stmt.step();
}

void ApiKeyRepository::touch_last_used(int64_t id) {
    auto stmt = db_.prepare("UPDATE api_keys SET last_used_at = datetime('now') WHERE id = ?");
    stmt.bind(1, id);
    stmt.step();
}
