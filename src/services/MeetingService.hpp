#pragma once
#include "repositories/MeetingRepository.hpp"
#include "repositories/ChapterRepository.hpp"
#include "integrations/DiscordClient.hpp"
#include "integrations/CalendarGenerator.hpp"
#include "models/Meeting.hpp"
#include <vector>
#include <optional>
#include <string>
#include <openssl/rand.h>

class MeetingService {
public:
    MeetingService(MeetingRepository& repo, DiscordClient& discord, CalendarGenerator& cal, ChapterRepository* chapter_repo = nullptr);

    std::vector<Meeting>   list_upcoming();
    std::vector<Meeting>   list_all();
    std::vector<Meeting>   list_by_chapter(int64_t chapter_id);
    std::vector<Meeting>   list_paginated(const std::string& search, int limit, int offset);
    int                    count_filtered(const std::string& search);
    int                    count_all();
    std::optional<Meeting> get(int64_t id);

    Meeting create(const Meeting& m);     // Generates ical_uid, posts to Discord
    Meeting update(int64_t id, const Meeting& updates); // Propagates to Discord + calendar
    void    cancel(int64_t id);           // Sets status=cancelled, updates Discord
    void    complete(int64_t id);

private:
    MeetingRepository& repo_;
    DiscordClient&     discord_;
    CalendarGenerator& cal_;
    ChapterRepository* chapter_repo_;

    static std::string generate_uuid();
};
