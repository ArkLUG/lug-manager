#pragma once
#include "repositories/ChapterRepository.hpp"
#include "models/Chapter.hpp"
#include <vector>
#include <optional>
#include <string>
#include <stdexcept>

class ChapterService {
public:
    explicit ChapterService(ChapterRepository& repo);

    std::optional<Chapter> get(int64_t id);
    std::vector<Chapter>   list_all();

    Chapter create(const Chapter& ch);
    Chapter update(int64_t id, const Chapter& updates);
    void    delete_chapter(int64_t id);

private:
    ChapterRepository& repo_;
};
