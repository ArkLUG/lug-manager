#pragma once
#include <sqlite3.h>
#include <string>
#include <functional>
#include <mutex>
#include <stdexcept>

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
};

// RAII wrapper around a prepared statement
class Statement {
public:
    // `lock` is the owning SqliteDatabase's mutex, taken around prepare and
    // each step() - see SqliteDatabase::mutex_.
    Statement(sqlite3* db, std::recursive_mutex* lock, const std::string& sql);
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
    sqlite3_stmt*         stmt_ = nullptr;
    std::recursive_mutex* lock_ = nullptr;
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
    friend class Transaction;
    friend class Statement;
    sqlite3* db_ = nullptr;
    // One connection is shared by every request thread and the background
    // workers. Each prepare/step/execute holds this mutex, and a Transaction
    // holds it for its whole lifetime, so other threads' statements can't
    // interleave into (or read uncommitted state from) an open transaction.
    // Recursive so code inside a Transaction can keep using the same db.
    std::recursive_mutex mutex_;
    int tx_depth_ = 0; // nesting depth; only the outermost Transaction BEGINs/COMMITs
};

// RAII transaction: BEGIN IMMEDIATE on construction, ROLLBACK on destruction
// unless commit() was called. Nests: inner Transactions join the outer one
// (an exception escaping the inner scope still rolls back the outer).
// Keep external I/O (Discord, Google Calendar) out of the scope - it holds
// the database mutex, blocking every other request until it ends.
class Transaction {
public:
    explicit Transaction(SqliteDatabase& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    SqliteDatabase&                        db_;
    std::unique_lock<std::recursive_mutex> lock_;
    bool                                   done_ = false;
};
