#pragma once

#include "models/Chapter.hpp"
#include <vector>
#include <optional>
#include <memory>

class SqliteDatabase;

class ChapterRepository {
public:
    explicit ChapterRepository(SqliteDatabase& db);

    Chapter create(const Chapter& ch);
    std::optional<Chapter> get(int64_t id);
    std::vector<Chapter> list_all();
    Chapter update(int64_t id, const Chapter& updates);
    void delete_chapter(int64_t id);

private:
    SqliteDatabase& db_;
};
