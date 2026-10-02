#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Session.hpp"
#include <optional>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

class SessionStore {
public:
    explicit SessionStore(SqliteDatabase& db);

    // Create new session, returns token
    // Sessions last kSessionHours from their last use: each day a session is
    // used, it's extended (see find()), so people who use the site stay signed
    // in and an unused session ends after 30 days.
    static constexpr int kSessionHours = 30 * 24;
    std::string create(int64_t member_id, const std::string& role,
                       const std::string& display_name = "", int hours = kSessionHours,
                       const std::string& user_agent = "");

    struct Info {
        std::string id;          // first 12 hex chars of the token hash (not usable as a token)
        std::string created_at;
        std::string expires_at;
        std::string user_agent;
        bool        current = false;
    };
    // Active sessions of a member; `current_token` (raw) is flagged current.
    std::vector<Info> list_for_member(int64_t member_id, const std::string& current_token = "");
    // Signs a member out everywhere, optionally keeping one (raw) token. Returns count removed.
    int remove_all_for_member(int64_t member_id, const std::string& keep_token = "");

    // Find session by token (checks in-memory cache first, then DB)
    std::optional<Session> find(const std::string& token);

    // Remove session
    void remove(const std::string& token);

    // Remove all expired sessions (call periodically)
    void purge_expired();

    static std::string generate_token(); // 32 random bytes as 64-char hex

private:
    SqliteDatabase&                          db_;
    std::unordered_map<std::string, Session> cache_;
    mutable std::mutex                       mutex_;

};
