#include <gtest/gtest.h>
#include "db/SqliteDatabase.hpp"
#include "db/Migrations.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

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

// ═══════════════════════════════════════════════════════════════════════════
// Migration 021 — verify dependent data survives member table recreation
// ═══════════════════════════════════════════════════════════════════════════

// Helper: apply migrations up to (but not including) a given version
static void apply_migrations_up_to(SqliteDatabase& db, int max_version) {
    namespace fs = std::filesystem;
    db.execute(R"(
        CREATE TABLE IF NOT EXISTS _schema_migrations (
            version INTEGER PRIMARY KEY,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
    )");

    std::vector<std::pair<int, std::string>> pending;
    for (const auto& entry : fs::directory_iterator("sql/migrations")) {
        if (entry.path().extension() != ".sql") continue;
        std::string stem = entry.path().stem().string();
        try {
            int ver = std::stoi(stem.substr(0, 3));
            if (ver < max_version) pending.emplace_back(ver, entry.path().string());
        } catch (...) {}
    }
    std::sort(pending.begin(), pending.end());
    for (auto& [ver, path] : pending) {
        std::ifstream f(path);
        std::stringstream ss; ss << f.rdbuf();
        db.execute("PRAGMA foreign_keys=OFF");
        db.execute("BEGIN");
        db.execute(ss.str());
        auto stmt = db.prepare("INSERT INTO _schema_migrations(version) VALUES(?)");
        stmt.bind(1, static_cast<int64_t>(ver));
        stmt.step();
        db.execute("COMMIT");
        db.execute("PRAGMA foreign_keys=ON");
    }
}

static void apply_single_migration(SqliteDatabase& db, int version) {
    namespace fs = std::filesystem;
    for (const auto& entry : fs::directory_iterator("sql/migrations")) {
        if (entry.path().extension() != ".sql") continue;
        std::string stem = entry.path().stem().string();
        try {
            int ver = std::stoi(stem.substr(0, 3));
            if (ver == version) {
                std::ifstream f(entry.path());
                std::stringstream ss; ss << f.rdbuf();
                // Mirror the real migration runner: disable FK before, re-enable after
                db.execute("PRAGMA foreign_keys=OFF");
                db.execute("BEGIN");
                db.execute(ss.str());
                auto stmt = db.prepare("INSERT INTO _schema_migrations(version) VALUES(?)");
                stmt.bind(1, static_cast<int64_t>(ver));
                stmt.step();
                db.execute("COMMIT");
                db.execute("PRAGMA foreign_keys=ON");
                return;
            }
        } catch (...) {}
    }
}

TEST(Migrations, Migration021PreservesAttendance) {
    SqliteDatabase db(":memory:");

    // Apply migrations 001-020 (everything before our changes)
    apply_migrations_up_to(db, 21);

    // Insert test data: a member, a meeting, and an attendance record
    db.execute("INSERT INTO members (discord_user_id, discord_username, display_name, role, first_name, last_name) "
               "VALUES ('user123', 'testuser', 'Test U.', 'member', 'Test', 'User')");
    db.execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope) "
               "VALUES ('Pre-migration Meeting', '2026-03-01T19:00:00', '2026-03-01T21:00:00', 'pre-mig-uid', 'chapter')");
    db.execute("INSERT INTO attendance (member_id, entity_type, entity_id, notes) "
               "VALUES (1, 'meeting', 1, 'was here')");
    db.execute("INSERT INTO sessions (token, member_id, role, expires_at) "
               "VALUES ('tok123', 1, 'member', '2099-01-01')");
    db.execute("INSERT INTO chapters (name, shorthand, discord_announcement_channel_id) "
               "VALUES ('Test Chapter', 'TC', '')");
    db.execute("INSERT INTO chapter_members (member_id, chapter_id, chapter_role) "
               "VALUES (1, 1, 'lead')");

    // Verify data exists before migration
    { auto s = db.prepare("SELECT COUNT(*) FROM attendance"); s.step();
      EXPECT_EQ(s.col_int(0), 1); }
    { auto s = db.prepare("SELECT COUNT(*) FROM sessions"); s.step();
      EXPECT_EQ(s.col_int(0), 1); }
    { auto s = db.prepare("SELECT COUNT(*) FROM chapter_members"); s.step();
      EXPECT_EQ(s.col_int(0), 1); }

    // Apply migration 021 (recreates members table)
    apply_single_migration(db, 21);

    // Verify member was migrated
    { auto s = db.prepare("SELECT display_name, role FROM members WHERE id=1");
      ASSERT_TRUE(s.step());
      EXPECT_EQ(s.col_text(0), "Test U.");
      EXPECT_EQ(s.col_text(1), "member"); }

    // Verify attendance survived
    { auto s = db.prepare("SELECT COUNT(*) FROM attendance"); s.step();
      EXPECT_EQ(s.col_int(0), 1) << "Attendance records should survive migration 021"; }
    { auto s = db.prepare("SELECT notes FROM attendance WHERE member_id=1 AND entity_type='meeting'");
      ASSERT_TRUE(s.step());
      EXPECT_EQ(s.col_text(0), "was here"); }

    // Verify sessions survived
    { auto s = db.prepare("SELECT COUNT(*) FROM sessions"); s.step();
      EXPECT_EQ(s.col_int(0), 1) << "Sessions should survive migration 021"; }
    { auto s = db.prepare("SELECT token FROM sessions WHERE member_id=1");
      ASSERT_TRUE(s.step());
      EXPECT_EQ(s.col_text(0), "tok123"); }

    // Verify chapter_members survived
    { auto s = db.prepare("SELECT COUNT(*) FROM chapter_members"); s.step();
      EXPECT_EQ(s.col_int(0), 1) << "Chapter members should survive migration 021"; }
    { auto s = db.prepare("SELECT chapter_role FROM chapter_members WHERE member_id=1");
      ASSERT_TRUE(s.step());
      EXPECT_EQ(s.col_text(0), "lead"); }

    // Verify new columns exist
    { auto s = db.prepare("SELECT birthday, fol_status FROM members WHERE id=1");
      ASSERT_TRUE(s.step());
      EXPECT_EQ(s.col_text(0), "");
      EXPECT_EQ(s.col_text(1), "afol"); }
}

// ═══════════════════════════════════════════════════════════════════════════
// Data persistence across database close/reopen (simulates app restart)
// ═══════════════════════════════════════════════════════════════════════════

TEST(Persistence, AllDataSurvivesRestart) {
    namespace fs = std::filesystem;
    // Use a temp file, not :memory:, so we can close and reopen
    std::string db_path = "/tmp/lug_test_persistence_" +
        std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".db";

    // Phase 1: create database, run migrations, insert data
    {
        SqliteDatabase db(db_path);
        Migrations mig(db);
        mig.run("sql/migrations");

        db.execute("INSERT INTO members (discord_user_id, discord_username, display_name, role, first_name, last_name, birthday, fol_status) "
                   "VALUES ('persist_user', 'puser', 'Persist U.', 'member', 'Persist', 'User', '2000-01-15', 'afol')");
        db.execute("INSERT INTO chapters (name, shorthand, discord_announcement_channel_id) "
                   "VALUES ('Persist Chapter', 'PC', '')");
        db.execute("INSERT INTO chapter_members (member_id, chapter_id, chapter_role) "
                   "VALUES (1, 1, 'lead')");
        db.execute("INSERT INTO meetings (title, start_time, end_time, ical_uid, scope, notes) "
                   "VALUES ('Persist Meeting', '2026-05-01T19:00:00', '2026-05-01T21:00:00', 'persist-uid-1', 'chapter', '# Notes')");
        db.execute("INSERT INTO lug_events (title, start_time, end_time, status, ical_uid, scope, suppress_discord, notes) "
                   "VALUES ('Persist Event', '2026-06-01', '2026-06-02', 'confirmed', 'persist-uid-2', 'lug_wide', 1, '## Report')");
        db.execute("INSERT INTO attendance (member_id, entity_type, entity_id, notes, is_virtual) "
                   "VALUES (1, 'meeting', 1, 'attended', 0)");
        db.execute("INSERT INTO attendance (member_id, entity_type, entity_id, notes, is_virtual) "
                   "VALUES (1, 'event', 1, 'was there', 1)");
        db.execute("INSERT INTO sessions (token, member_id, role, expires_at) "
                   "VALUES ('persist_token_abc', 1, 'member', '2099-12-31')");
        db.execute("INSERT INTO perk_levels (name, discord_role_id, meeting_attendance_required, event_attendance_required, requires_paid_dues, sort_order) "
                   "VALUES ('Gold', 'role999', 5, 2, 1, 1)");
        db.execute("INSERT INTO lug_settings (key, value) VALUES ('test_setting', 'test_value')");
        db.execute("INSERT INTO discord_role_mappings (discord_role_id, discord_role_name, lug_role) "
                   "VALUES ('drole1', 'Test Role', 'member')");
    }
    // db is closed here (destructor)

    // Phase 2: reopen database, run migrations again (should be no-op), verify all data
    {
        SqliteDatabase db(db_path);
        Migrations mig(db);
        mig.run("sql/migrations"); // should say "up to date"

        // Members
        { auto s = db.prepare("SELECT display_name, birthday, fol_status FROM members WHERE discord_user_id='persist_user'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "Persist U.");
          EXPECT_EQ(s.col_text(1), "2000-01-15");
          EXPECT_EQ(s.col_text(2), "afol"); }

        // Chapters
        { auto s = db.prepare("SELECT name, shorthand FROM chapters WHERE name='Persist Chapter'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(1), "PC"); }

        // Chapter members
        { auto s = db.prepare("SELECT chapter_role FROM chapter_members WHERE member_id=1 AND chapter_id=1");
          ASSERT_TRUE(s.step()) << "Chapter membership should persist across restart";
          EXPECT_EQ(s.col_text(0), "lead"); }

        // Meetings (with notes)
        { auto s = db.prepare("SELECT title, notes FROM meetings WHERE ical_uid='persist-uid-1'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "Persist Meeting");
          EXPECT_EQ(s.col_text(1), "# Notes"); }

        // Events (with suppress + notes)
        { auto s = db.prepare("SELECT title, suppress_discord, notes FROM lug_events WHERE ical_uid='persist-uid-2'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "Persist Event");
          EXPECT_EQ(s.col_int(1), 1);
          EXPECT_EQ(s.col_text(2), "## Report"); }

        // Attendance (both records)
        { auto s = db.prepare("SELECT COUNT(*) FROM attendance WHERE member_id=1");
          s.step();
          EXPECT_EQ(s.col_int(0), 2) << "Both attendance records should persist"; }
        { auto s = db.prepare("SELECT notes, is_virtual FROM attendance WHERE entity_type='meeting'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "attended");
          EXPECT_EQ(s.col_int(1), 0); }
        { auto s = db.prepare("SELECT notes, is_virtual FROM attendance WHERE entity_type='event'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "was there");
          EXPECT_EQ(s.col_int(1), 1); }

        // Sessions
        { auto s = db.prepare("SELECT token, role FROM sessions WHERE member_id=1");
          ASSERT_TRUE(s.step()) << "Session should persist across restart";
          EXPECT_EQ(s.col_text(0), "persist_token_abc");
          EXPECT_EQ(s.col_text(1), "member"); }

        // Perk levels
        { auto s = db.prepare("SELECT name, discord_role_id, meeting_attendance_required, event_attendance_required, requires_paid_dues FROM perk_levels");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "Gold");
          EXPECT_EQ(s.col_text(1), "role999");
          EXPECT_EQ(s.col_int(2), 5);
          EXPECT_EQ(s.col_int(3), 2);
          EXPECT_EQ(s.col_int(4), 1); }

        // Settings
        { auto s = db.prepare("SELECT value FROM lug_settings WHERE key='test_setting'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "test_value"); }

        // Role mappings
        { auto s = db.prepare("SELECT discord_role_name, lug_role FROM discord_role_mappings WHERE discord_role_id='drole1'");
          ASSERT_TRUE(s.step());
          EXPECT_EQ(s.col_text(0), "Test Role");
          EXPECT_EQ(s.col_text(1), "member"); }
    }

    // Cleanup
    fs::remove(db_path);
    fs::remove(db_path + "-wal");
    fs::remove(db_path + "-shm");
}

TEST(Migrations, Migration021MigratesReadonlyToMember) {
    SqliteDatabase db(":memory:");
    apply_migrations_up_to(db, 21);

    // Insert a readonly member
    db.execute("INSERT INTO members (discord_user_id, discord_username, display_name, role, first_name, last_name) "
               "VALUES ('ro_user', 'readonly_user', 'RO U.', 'readonly', 'RO', 'User')");

    apply_single_migration(db, 21);

    // Should now be 'member', not 'readonly'
    auto s = db.prepare("SELECT role FROM members WHERE discord_user_id='ro_user'");
    ASSERT_TRUE(s.step());
    EXPECT_EQ(s.col_text(0), "member");
}
