#pragma once
#include "db/SqliteDatabase.hpp"
#include <string>

class Migrations {
public:
    explicit Migrations(SqliteDatabase& db);
    void run(const std::string& migrations_dir = "sql/migrations");

private:
    SqliteDatabase& db_;
    void ensure_migrations_table();
    int  get_current_version();
    void apply_migration(int version, const std::string& filepath);
};
