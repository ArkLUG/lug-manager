#include "db/SqliteDatabase.hpp"
#include <stdexcept>

// ---------- Statement ----------

Statement::Statement(sqlite3* db, std::recursive_mutex* lock, const std::string& sql)
    : lock_(lock) {
    std::lock_guard<std::recursive_mutex> guard(*lock_);
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        throw DbError(std::string("Prepare failed: ") + sqlite3_errmsg(db) + " | SQL: " + sql);
    }
}

Statement::~Statement() {
    if (!stmt_) return;
    std::lock_guard<std::recursive_mutex> guard(*lock_);
    sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_), lock_(other.lock_) {
    other.stmt_ = nullptr;
}

Statement& Statement::bind(int idx, int64_t val) {
    sqlite3_bind_int64(stmt_, idx, val);
    return *this;
}

Statement& Statement::bind(int idx, const std::string& val) {
    sqlite3_bind_text(stmt_, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    return *this;
}

Statement& Statement::bind(int idx, bool val) {
    sqlite3_bind_int(stmt_, idx, val ? 1 : 0);
    return *this;
}

Statement& Statement::bind_null(int idx) {
    sqlite3_bind_null(stmt_, idx);
    return *this;
}

bool Statement::step() {
    std::lock_guard<std::recursive_mutex> guard(*lock_);
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    // Include SQLite error message for better debugging
    sqlite3* db = sqlite3_db_handle(stmt_);
    std::string msg = "Step failed: rc=" + std::to_string(rc);
    if (db) msg += " — " + std::string(sqlite3_errmsg(db));
    throw DbError(msg);
}

int64_t Statement::col_int(int idx) const {
    return sqlite3_column_int64(stmt_, idx);
}

std::string Statement::col_text(int idx) const {
    const unsigned char* val = sqlite3_column_text(stmt_, idx);
    return val ? reinterpret_cast<const char*>(val) : "";
}

bool Statement::col_bool(int idx) const {
    return sqlite3_column_int(stmt_, idx) != 0;
}

bool Statement::col_is_null(int idx) const {
    return sqlite3_column_type(stmt_, idx) == SQLITE_NULL;
}

void Statement::reset() {
    std::lock_guard<std::recursive_mutex> guard(*lock_);
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---------- SqliteDatabase ----------

SqliteDatabase::SqliteDatabase(const std::string& path) : path_(path) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw DbError(std::string("Cannot open database: ") + sqlite3_errmsg(db_));
    }
    // Enable WAL mode for better concurrent read performance
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA synchronous=NORMAL");
    // Bigger page cache (16 MB), memory-mapped reads (64 MB) and in-memory
    // temp tables: cheap wins for the overview/report queries.
    execute("PRAGMA cache_size=-16000");
    execute("PRAGMA mmap_size=67108864");
    execute("PRAGMA temp_store=MEMORY");
}

SqliteDatabase::~SqliteDatabase() {
    if (db_) sqlite3_close(db_);
}

Statement SqliteDatabase::prepare(const std::string& sql) {
    return Statement(db_, &mutex_, sql);
}

void SqliteDatabase::execute(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "Unknown error";
        sqlite3_free(errmsg);
        throw DbError("Execute failed: " + msg);
    }
}

int64_t SqliteDatabase::last_insert_rowid() {
    return sqlite3_last_insert_rowid(db_);
}

// ---------- Transaction ----------

Transaction::Transaction(SqliteDatabase& db) : db_(db), lock_(db.mutex_) {
    if (db_.tx_depth_ == 0) db_.execute("BEGIN IMMEDIATE");
    ++db_.tx_depth_;
}

void Transaction::commit() {
    if (done_) return;
    done_ = true;
    // An inner scope already rolled everything back (its exception was
    // caught somewhere inside this one) - committing now would be a lie.
    if (db_.tx_depth_ <= 0) throw DbError("commit after transaction was rolled back");
    if (--db_.tx_depth_ == 0) {
        try {
            db_.execute("COMMIT");
        } catch (...) {
            // Never leave the shared connection inside an open transaction:
            // every later BEGIN would fail ("transaction within a transaction").
            try { db_.execute("ROLLBACK"); } catch (...) {}
            throw;
        }
    }
}

Transaction::~Transaction() {
    if (done_) return;
    // Not committed (exception or early return): undo the whole outermost
    // transaction. For an inner scope, ROLLBACK here ends the outer one too;
    // the outer destructor/commit then sees depth 0 and does nothing more.
    if (db_.tx_depth_ > 0) {
        db_.tx_depth_ = 0;
        try { db_.execute("ROLLBACK"); } catch (...) {}
    }
}
