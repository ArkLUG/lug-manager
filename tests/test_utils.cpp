#include <gtest/gtest.h>
#include "db/SqliteDatabase.hpp"
#include "db/Migrations.hpp"

// ═══════════════════════════════════════════════════════════════════════════
// SQLite Database
// ═══════════════════════════════════════════════════════════════════════════

TEST(SqliteDatabase, InMemoryWorks) {
    SqliteDatabase db(":memory:");
    db.execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    db.execute("INSERT INTO test (name) VALUES ('hello')");
    auto stmt = db.prepare("SELECT name FROM test");
    EXPECT_TRUE(stmt.step());
    EXPECT_EQ(stmt.col_text(0), "hello");
    EXPECT_FALSE(stmt.step());
}

TEST(SqliteDatabase, BindAndRead) {
    SqliteDatabase db(":memory:");
    db.execute("CREATE TABLE t (i INTEGER, s TEXT, b INTEGER)");
    auto ins = db.prepare("INSERT INTO t VALUES (?,?,?)");
    ins.bind(1, static_cast<int64_t>(42));
    ins.bind(2, std::string("test"));
    ins.bind(3, true);
    ins.step();

    auto sel = db.prepare("SELECT i, s, b FROM t");
    EXPECT_TRUE(sel.step());
    EXPECT_EQ(sel.col_int(0), 42);
    EXPECT_EQ(sel.col_text(1), "test");
    EXPECT_TRUE(sel.col_bool(2));
}

TEST(SqliteDatabase, BindNull) {
    SqliteDatabase db(":memory:");
    db.execute("CREATE TABLE t (v TEXT)");
    auto ins = db.prepare("INSERT INTO t VALUES (?)");
    ins.bind_null(1);
    ins.step();

    auto sel = db.prepare("SELECT v FROM t");
    EXPECT_TRUE(sel.step());
    EXPECT_TRUE(sel.col_is_null(0));
    EXPECT_EQ(sel.col_text(0), "");
}

TEST(SqliteDatabase, LastInsertRowid) {
    SqliteDatabase db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)");
    auto ins = db.prepare("INSERT INTO t (name) VALUES (?)");
    ins.bind(1, std::string("first"));
    ins.step();
    EXPECT_EQ(db.last_insert_rowid(), 1);
}

TEST(SqliteDatabase, InvalidSqlThrows) {
    SqliteDatabase db(":memory:");
    EXPECT_THROW(db.prepare("SELECT FROM nonexistent garbage"), DbError);
}

TEST(SqliteDatabase, ConstraintViolationThrows) {
    SqliteDatabase db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE)");
    auto ins = db.prepare("INSERT INTO t (id, name) VALUES (?,?)");
    ins.bind(1, static_cast<int64_t>(1));
    ins.bind(2, std::string("unique_name"));
    ins.step();

    auto dup = db.prepare("INSERT INTO t (id, name) VALUES (?,?)");
    dup.bind(1, static_cast<int64_t>(2));
    dup.bind(2, std::string("unique_name"));
    EXPECT_THROW(dup.step(), DbError);
}

// ═══════════════════════════════════════════════════════════════════════════
// Migrations
// ═══════════════════════════════════════════════════════════════════════════

TEST(Migrations, RunAllMigrations) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    EXPECT_NO_THROW(mig.run("sql/migrations"));

    // Verify key tables exist
    auto stmt = db.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN "
        "('members','meetings','lug_events','attendance','chapters','chapter_members',"
        "'lug_settings','role_mappings')");
    stmt.step();
    EXPECT_GE(stmt.col_int(0), 7); // at least 7 core tables
}

TEST(Migrations, Idempotent) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    mig.run("sql/migrations");
    // Running again should not throw
    EXPECT_NO_THROW(mig.run("sql/migrations"));
}

TEST(Migrations, EventStatusConstraint) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    mig.run("sql/migrations");

    // Valid statuses should work
    for (const char* status : {"tentative", "confirmed", "open", "closed", "cancelled"}) {
        auto ins = db.prepare(
            "INSERT INTO lug_events (title, start_time, end_time, status, ical_uid) "
            "VALUES (?, '2026-01-01', '2026-01-02', ?, ?)");
        ins.bind(1, std::string("test_") + status);
        ins.bind(2, std::string(status));
        ins.bind(3, std::string("uid_") + status);
        EXPECT_NO_THROW(ins.step()) << "Status '" << status << "' should be valid";
    }

    // Invalid status should fail
    auto bad = db.prepare(
        "INSERT INTO lug_events (title, start_time, end_time, status, ical_uid) "
        "VALUES ('bad', '2026-01-01', '2026-01-02', 'invalid', 'uid_bad')");
    EXPECT_THROW(bad.step(), DbError);
}

TEST(Migrations, AttendanceVirtualColumn) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    mig.run("sql/migrations");

    // Verify is_virtual column exists with default 0
    db.execute("INSERT INTO members (discord_user_id, discord_username, display_name, role) "
               "VALUES ('test1', 'test', 'Test', 'member')");
    db.execute("INSERT INTO attendance (member_id, entity_type, entity_id) "
               "VALUES (1, 'meeting', 1)");
    auto sel = db.prepare("SELECT is_virtual FROM attendance WHERE id=1");
    EXPECT_TRUE(sel.step());
    EXPECT_EQ(sel.col_int(0), 0);
}

TEST(Migrations, ChapterShorthandColumn) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    mig.run("sql/migrations");

    db.execute("INSERT INTO chapters (name, shorthand, discord_announcement_channel_id) VALUES ('Test', 'TST', '')");
    auto sel = db.prepare("SELECT shorthand FROM chapters WHERE name='Test'");
    EXPECT_TRUE(sel.step());
    EXPECT_EQ(sel.col_text(0), "TST");
}

TEST(Migrations, GoogleCalendarEventIdColumns) {
    SqliteDatabase db(":memory:");
    Migrations mig(db);
    mig.run("sql/migrations");

    // Verify column exists on meetings
    db.execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, google_calendar_event_id) "
               "VALUES ('test', '2026-01-01T10:00:00', '2026-01-01T12:00:00', 'uid1', 'gcal1')");
    auto sel = db.prepare("SELECT google_calendar_event_id FROM meetings WHERE ical_uid='uid1'");
    EXPECT_TRUE(sel.step());
    EXPECT_EQ(sel.col_text(0), "gcal1");

    // Verify column exists on lug_events
    db.execute("INSERT INTO lug_events (title, start_time, end_time, status, ical_uid, google_calendar_event_id) "
               "VALUES ('test', '2026-01-01', '2026-01-02', 'confirmed', 'uid2', 'gcal2')");
    auto sel2 = db.prepare("SELECT google_calendar_event_id FROM lug_events WHERE ical_uid='uid2'");
    EXPECT_TRUE(sel2.step());
    EXPECT_EQ(sel2.col_text(0), "gcal2");
}
