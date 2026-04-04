#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/AuditLog.hpp"
#include <vector>
#include <string>

class AuditLogRepository {
public:
    explicit AuditLogRepository(SqliteDatabase& db);

    // Write a log entry
    void log(int64_t actor_id, const std::string& actor_name,
             const std::string& action,
             const std::string& entity_type, int64_t entity_id,
             const std::string& entity_name,
             const std::string& details,
             const std::string& ip_address = "");

    // Query logs (newest first) with optional filters
    std::vector<AuditLog> find_paginated(const std::string& search,
                                          const std::string& action_filter,
                                          int limit, int offset);
    int count_filtered(const std::string& search, const std::string& action_filter);

private:
    SqliteDatabase& db_;
    static AuditLog row_to_log(Statement& stmt);
};
