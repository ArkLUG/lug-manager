#include "auth/SessionStore.hpp"
#include <openssl/rand.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string now_plus_hours(int h) {
    auto tp = std::chrono::system_clock::now() + std::chrono::hours(h);
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

static std::string now_iso() {
    auto tp = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

// Returns true if expires_at (ISO 8601 UTC, no Z) is in the past relative to now.
static bool is_expired(const std::string& expires_at) {
    if (expires_at.empty()) return true;
    return expires_at <= now_iso();
}

// ---------------------------------------------------------------------------
// SessionStore
// ---------------------------------------------------------------------------

SessionStore::SessionStore(SqliteDatabase& db) : db_(db) {
    // Ensure the sessions table exists
    db_.execute(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            token       TEXT PRIMARY KEY,
            member_id   INTEGER NOT NULL,
            role        TEXT    NOT NULL DEFAULT 'member',
            expires_at  TEXT    NOT NULL,
            created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
        )
    )");
}

std::string SessionStore::generate_token() {
    unsigned char buf[32];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        throw std::runtime_error("RAND_bytes failed: cannot generate session token");
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str(); // 64-char lowercase hex string
}

std::string SessionStore::create(int64_t member_id, const std::string& role,
                                  const std::string& display_name, int hours) {
    std::string token      = generate_token();
    std::string expires_at = now_plus_hours(hours);
    std::string created_at = now_iso();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto stmt = db_.prepare(
            "INSERT INTO sessions (token, member_id, role, expires_at, created_at) "
            "VALUES (?, ?, ?, ?, ?)");
        stmt.bind(1, token)
            .bind(2, member_id)
            .bind(3, role)
            .bind(4, expires_at)
            .bind(5, created_at);
        stmt.step();

        Session s;
        s.token        = token;
        s.member_id    = member_id;
        s.role         = role;
        s.display_name = display_name;
        s.expires_at   = expires_at;
        s.created_at   = created_at;
        cache_[token]  = s;
    }
    return token;
}

std::optional<Session> SessionStore::find(const std::string& token) {
    if (token.empty()) return std::nullopt;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check in-memory cache first
    auto it = cache_.find(token);
    if (it != cache_.end()) {
        if (is_expired(it->second.expires_at)) {
            // Expired: remove from cache and DB
            cache_.erase(it);
            auto del = db_.prepare("DELETE FROM sessions WHERE token = ?");
            del.bind(1, token);
            del.step();
            return std::nullopt;
        }
        return it->second;
    }

    // Fall back to database
    auto stmt = db_.prepare(
        "SELECT token, member_id, role, expires_at, created_at "
        "FROM sessions WHERE token = ?");
    stmt.bind(1, token);

    if (!stmt.step()) return std::nullopt;

    Session s;
    s.token      = stmt.col_text(0);
    s.member_id  = stmt.col_int(1);
    s.role       = stmt.col_text(2);
    s.expires_at = stmt.col_text(3);
    s.created_at = stmt.col_text(4);
    // Look up display name from members table
    {
        auto m_stmt = db_.prepare("SELECT display_name FROM members WHERE id=?");
        m_stmt.bind(1, s.member_id);
        if (m_stmt.step()) s.display_name = m_stmt.col_text(0);
    }

    if (is_expired(s.expires_at)) {
        // Remove expired session
        auto del = db_.prepare("DELETE FROM sessions WHERE token = ?");
        del.bind(1, token);
        del.step();
        return std::nullopt;
    }

    cache_[s.token] = s;
    return s;
}

void SessionStore::remove(const std::string& token) {
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(token);
    auto stmt = db_.prepare("DELETE FROM sessions WHERE token = ?");
    stmt.bind(1, token);
    stmt.step();
}

void SessionStore::purge_expired() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove expired sessions from DB
    auto stmt = db_.prepare("DELETE FROM sessions WHERE expires_at < datetime('now')");
    stmt.step();

    // Clean up expired entries from in-memory cache
    std::string now = now_iso();
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        if (it->second.expires_at <= now) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}
