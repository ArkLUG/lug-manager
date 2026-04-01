#pragma once
#include <gtest/gtest.h>
#include "db/SqliteDatabase.hpp"
#include "db/Migrations.hpp"
#include <memory>

// Base fixture: in-memory SQLite DB with all migrations applied
class DbFixture : public ::testing::Test {
protected:
    std::unique_ptr<SqliteDatabase> db;

    void SetUp() override {
        db = std::make_unique<SqliteDatabase>(":memory:");
        Migrations mig(*db);
        mig.run("sql/migrations");
    }

    void TearDown() override {
        db.reset();
    }

    // Helper: count rows in a table
    int count(const std::string& table) {
        auto stmt = db->prepare("SELECT COUNT(*) FROM " + table);
        stmt.step();
        return static_cast<int>(stmt.col_int(0));
    }
};
