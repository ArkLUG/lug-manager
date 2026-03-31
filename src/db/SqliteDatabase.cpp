#include "db/SqliteDatabase.hpp"
#include <stdexcept>

// ---------- Statement ----------

Statement::Statement(sqlite3* db, const std::string& sql) {
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        throw DbError(std::string("Prepare failed: ") + sqlite3_errmsg(db) + " | SQL: " + sql);
    }
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : stmt_(other.stmt_) {
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
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    throw DbError(std::string("Step failed: rc=") + std::to_string(rc));
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
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// ---------- SqliteDatabase ----------

SqliteDatabase::SqliteDatabase(const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        throw DbError(std::string("Cannot open database: ") + sqlite3_errmsg(db_));
    }
    // Enable WAL mode for better concurrent read performance
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA synchronous=NORMAL");
}

SqliteDatabase::~SqliteDatabase() {
    if (db_) sqlite3_close(db_);
}

Statement SqliteDatabase::prepare(const std::string& sql) {
    return Statement(db_, sql);
}

void SqliteDatabase::execute(const std::string& sql) {
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
