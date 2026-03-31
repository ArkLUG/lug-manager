#include "repositories/SettingsRepository.hpp"

SettingsRepository::SettingsRepository(SqliteDatabase& db) : db_(db) {}

std::string SettingsRepository::get(const std::string& key, const std::string& default_val) const {
    auto stmt = db_.prepare("SELECT value FROM lug_settings WHERE key=?");
    stmt.bind(1, key);
    if (stmt.step()) {
        return stmt.col_text(0);
    }
    return default_val;
}

void SettingsRepository::set(const std::string& key, const std::string& value) {
    auto stmt = db_.prepare(
        "INSERT INTO lug_settings(key, value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    stmt.bind(1, key);
    stmt.bind(2, value);
    stmt.step();
}
