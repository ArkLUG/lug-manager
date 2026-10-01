#pragma once
#include "db/SqliteDatabase.hpp"
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Consistent SQLite snapshots (VACUUM INTO) under <data_dir>/backups, named
// lug-YYYYMMDD-HHMMSS.db, keeping the newest `keep` files.
class BackupService {
public:
    BackupService(SqliteDatabase& db, std::string data_dir) : db_(db), dir_(std::move(data_dir) + "/backups") {}

    struct Entry { std::string name; std::uintmax_t bytes = 0; std::string when; };

    std::string create();                       // returns file name
    std::vector<Entry> list() const;            // newest first
    void prune(int keep);
    // Creates a backup if the newest is older than `hours` (or none exist).
    bool create_if_due(int hours, int keep);
    // Absolute path of a listed backup, or "" if `name` isn't one (path-safe).
    std::string path_of(const std::string& name) const;
    static bool valid_name(const std::string& name);

private:
    SqliteDatabase& db_;
    std::string     dir_;
};
