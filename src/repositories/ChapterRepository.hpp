#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/Chapter.hpp"
#include <vector>
#include <optional>
#include <string>

class ChapterRepository {
public:
    explicit ChapterRepository(SqliteDatabase& db);

    std::optional<Chapter> find_by_id(int64_t id);
    std::vector<Chapter>   find_all();

    Chapter create(const Chapter& ch);
    bool    update(const Chapter& ch);
    bool    delete_by_id(int64_t id);

private:
    SqliteDatabase& db_;
    static Chapter row_to_chapter(Statement& stmt);
};
