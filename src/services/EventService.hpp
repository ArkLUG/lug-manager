#pragma once
#include "repositories/EventRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "models/LugEvent.hpp"
#include <vector>
#include <optional>
#include <string>

class EventService {
public:
    EventService(EventRepository& repo, DiscordClient& discord, CalendarGenerator& cal,
                 ChapterRepository* chapter_repo = nullptr);

    std::vector<LugEvent>   list_upcoming();
    std::vector<LugEvent>   list_all();
    std::vector<LugEvent>   list_by_chapter(int64_t chapter_id);
    std::vector<LugEvent>   list_paginated(const std::string& search, int limit, int offset, bool upcoming_only = true);
    int                     count_filtered(const std::string& search, bool upcoming_only = true);
    int                     count_all();
    std::optional<LugEvent> get(int64_t id);

    LugEvent create(const LugEvent& e);
    LugEvent update(int64_t id, const LugEvent& updates);
    void     cancel(int64_t id);
    void     update_status(int64_t id, const std::string& status);

private:
    EventRepository&   repo_;
    DiscordClient&     discord_;
    CalendarGenerator& cal_;
    ChapterRepository* chapter_repo_;

    static std::string generate_uuid();
};
