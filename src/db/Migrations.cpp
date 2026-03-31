#include "db/Migrations.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

Migrations::Migrations(SqliteDatabase& db) : db_(db) {}

void Migrations::ensure_migrations_table() {
    db_.execute(R"(
        CREATE TABLE IF NOT EXISTS _schema_migrations (
            version    INTEGER PRIMARY KEY,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");
}

int Migrations::get_current_version() {
    try {
        auto stmt = db_.prepare("SELECT COALESCE(MAX(version), 0) FROM _schema_migrations");
        if (stmt.step()) return static_cast<int>(stmt.col_int(0));
    } catch (...) {}
    return 0;
}

void Migrations::apply_migration(int version, const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) throw std::runtime_error("Cannot open migration: " + filepath);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string sql = ss.str();

    db_.execute("BEGIN");
    try {
        db_.execute(sql);
        auto stmt = db_.prepare("INSERT INTO _schema_migrations(version) VALUES(?)");
        stmt.bind(1, static_cast<int64_t>(version));
        stmt.step();
        db_.execute("COMMIT");
        std::cout << "[migrations] Applied version " << version << ": " << filepath << "\n";
    } catch (const std::exception& e) {
        db_.execute("ROLLBACK");
        throw std::runtime_error("Migration " + std::to_string(version) + " failed: " + e.what());
    }
}

void Migrations::run(const std::string& migrations_dir) {
    ensure_migrations_table();
    int current = get_current_version();

    if (!fs::exists(migrations_dir)) {
        std::cout << "[migrations] Directory not found: " << migrations_dir << "\n";
        return;
    }

    // Collect migration files sorted by version number
    std::vector<std::pair<int, std::string>> pending;
    for (const auto& entry : fs::directory_iterator(migrations_dir)) {
        if (entry.path().extension() != ".sql") continue;
        std::string stem = entry.path().stem().string();
        try {
            int ver = std::stoi(stem.substr(0, 3));
            if (ver > current) pending.emplace_back(ver, entry.path().string());
        } catch (...) {}
    }
    std::sort(pending.begin(), pending.end());

    for (auto& [ver, path] : pending) {
        apply_migration(ver, path);
    }

    if (pending.empty()) {
        std::cout << "[migrations] Database up to date (version " << current << ")\n";
    }
}
