#pragma once
#include <sqlite3.h>
#include <string>
#include <functional>
#include <stdexcept>

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
};

// RAII wrapper around a prepared statement
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();

    // Non-copyable, movable
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;

    // Bind parameters (1-indexed)
    Statement& bind(int idx, int64_t val);
    Statement& bind(int idx, const std::string& val);
    Statement& bind(int idx, bool val);
    Statement& bind_null(int idx);

    // Execute one step; returns true if a row is available
    bool step();

    // Column accessors (0-indexed)
    int64_t     col_int(int idx)  const;
    std::string col_text(int idx) const;
    bool        col_bool(int idx) const;
    bool        col_is_null(int idx) const;

    void reset();

private:
    sqlite3_stmt* stmt_ = nullptr;
};

class SqliteDatabase {
public:
    explicit SqliteDatabase(const std::string& path);
    ~SqliteDatabase();

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    Statement prepare(const std::string& sql);
    void      execute(const std::string& sql);         // DDL / simple statements
    int64_t   last_insert_rowid();
    sqlite3*  raw() { return db_; }

private:
    sqlite3* db_ = nullptr;
};
