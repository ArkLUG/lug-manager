#include "repositories/AuditLogRepository.hpp"

static const char* kSelectAllCols =
    "SELECT id, actor_id, actor_name, action, entity_type, entity_id, "
    "entity_name, details, ip_address, created_at "
    "FROM audit_log";

AuditLogRepository::AuditLogRepository(SqliteDatabase& db) : db_(db) {}

AuditLog AuditLogRepository::row_to_log(Statement& stmt) {
    AuditLog a;
    a.id          = stmt.col_int(0);
    a.actor_id    = stmt.col_int(1);
    a.actor_name  = stmt.col_text(2);
    a.action      = stmt.col_text(3);
    a.entity_type = stmt.col_text(4);
    a.entity_id   = stmt.col_int(5);
    a.entity_name = stmt.col_text(6);
    a.details     = stmt.col_text(7);
    a.ip_address  = stmt.col_text(8);
    a.created_at  = stmt.col_text(9);
    return a;
}

void AuditLogRepository::log(int64_t actor_id, const std::string& actor_name,
                              const std::string& action,
                              const std::string& entity_type, int64_t entity_id,
                              const std::string& entity_name,
                              const std::string& details,
                              const std::string& ip_address) {
    auto stmt = db_.prepare(
        "INSERT INTO audit_log (actor_id, actor_name, action, entity_type, entity_id, "
        "entity_name, details, ip_address) VALUES (?,?,?,?,?,?,?,?)");
    stmt.bind(1, actor_id);
    stmt.bind(2, actor_name);
    stmt.bind(3, action);
    stmt.bind(4, entity_type);
    stmt.bind(5, entity_id);
    stmt.bind(6, entity_name);
    stmt.bind(7, details);
    stmt.bind(8, ip_address);
    stmt.step();
}

std::vector<AuditLog> AuditLogRepository::find_paginated(const std::string& search,
                                                           const std::string& action_filter,
                                                           int limit, int offset) {
    std::string where;
    int idx = 1;
    if (!search.empty()) {
        where += " WHERE (actor_name LIKE ? OR entity_name LIKE ? OR details LIKE ? OR action LIKE ?)";
    }
    if (!action_filter.empty()) {
        where += where.empty() ? " WHERE " : " AND ";
        where += "action LIKE ?";
    }

    auto stmt = db_.prepare(std::string(kSelectAllCols) + where + " ORDER BY created_at DESC LIMIT ? OFFSET ?");
    if (!search.empty()) {
        std::string pat = "%" + search + "%";
        stmt.bind(idx++, pat); stmt.bind(idx++, pat);
        stmt.bind(idx++, pat); stmt.bind(idx++, pat);
    }
    if (!action_filter.empty()) {
        stmt.bind(idx++, action_filter + "%");
    }
    stmt.bind(idx++, static_cast<int64_t>(limit));
    stmt.bind(idx, static_cast<int64_t>(offset));

    std::vector<AuditLog> result;
    while (stmt.step()) result.push_back(row_to_log(stmt));
    return result;
}

int AuditLogRepository::count_filtered(const std::string& search, const std::string& action_filter) {
    std::string sql = "SELECT COUNT(*) FROM audit_log";
    int idx = 1;
    if (!search.empty()) {
        sql += " WHERE (actor_name LIKE ? OR entity_name LIKE ? OR details LIKE ? OR action LIKE ?)";
    }
    if (!action_filter.empty()) {
        sql += search.empty() ? " WHERE " : " AND ";
        sql += "action LIKE ?";
    }

    auto stmt = db_.prepare(sql);
    if (!search.empty()) {
        std::string pat = "%" + search + "%";
        stmt.bind(idx++, pat); stmt.bind(idx++, pat);
        stmt.bind(idx++, pat); stmt.bind(idx++, pat);
    }
    if (!action_filter.empty()) {
        stmt.bind(idx++, action_filter + "%");
    }
    if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    return 0;
}
