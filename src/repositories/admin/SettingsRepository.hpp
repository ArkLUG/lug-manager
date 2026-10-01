#pragma once
#include "db/SqliteDatabase.hpp"
#include <string>

class SettingsRepository {
public:
    explicit SettingsRepository(SqliteDatabase& db);

    std::string get(const std::string& key, const std::string& default_val = "") const;
    void        set(const std::string& key, const std::string& value);

    SqliteDatabase& db() { return db_; }

private:
    SqliteDatabase& db_;
};
