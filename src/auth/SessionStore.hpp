#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Session.hpp"
#include <optional>
#include <unordered_map>
#include <mutex>
#include <string>

class SessionStore {
public:
    explicit SessionStore(SqliteDatabase& db);

    // Create new session, returns token
    std::string create(int64_t member_id, const std::string& role, int hours = 24);

    // Find session by token (checks in-memory cache first, then DB)
    std::optional<Session> find(const std::string& token);

    // Remove session
    void remove(const std::string& token);

    // Remove all expired sessions (call periodically)
    void purge_expired();

private:
    SqliteDatabase&                          db_;
    std::unordered_map<std::string, Session> cache_;
    mutable std::mutex                       mutex_;

    static std::string generate_token(); // 32 random bytes as 64-char hex
};
