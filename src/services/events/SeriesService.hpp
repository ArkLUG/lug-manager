#pragma once
#include "db/SqliteDatabase.hpp"
#include "services/events/MeetingService.hpp"
#include <optional>
#include <string>
#include <vector>

struct MeetingSeries {
    int64_t     id = 0;
    std::string title, description, location;
    std::string scope = "chapter";
    int64_t     chapter_id = 0;
    bool        is_virtual = false;
    std::string discord_voice_channel_id;
    std::string start_hm = "19:00", end_hm = "21:00";
    std::string rule = "monthly";   // weekly | monthly
    int         weekday = 2;        // 0=Sun..6=Sat
    int         interval_weeks = 1;
    int         nth = 1;            // 1..4, -1 = last
    std::string starts_on, ends_on; // YYYY-MM-DD ('' = open-ended)
    int         days_ahead = 45;
    bool        suppress_discord = false, suppress_calendar = false, is_private = false, excludes_perks = false;
    bool        active = true;
    int64_t     created_by = 0;
};

// Recurring meetings (migration 054). materialize() creates the meetings of
// every active series that fall within its days_ahead window and don't exist
// yet - idempotent, run periodically and right after a series is saved.
class SeriesService {
public:
    SeriesService(SqliteDatabase& db, MeetingService& meetings) : db_(db), meetings_(meetings) {}

    int64_t create(const MeetingSeries& s);
    std::optional<MeetingSeries> get(int64_t id);
    std::vector<MeetingSeries> list(bool active_only = false);
    // Stops a series and deletes its meetings that haven't started yet.
    int stop(int64_t id, const std::string& now_local_iso);

    // Occurrence dates (YYYY-MM-DD) of `s` within [from, to].
    static std::vector<std::string> occurrences(const MeetingSeries& s, const std::string& from,
                                                const std::string& to);
    // Returns how many meetings were created.
    int materialize(const std::string& today);
    // Human summary, e.g. "2nd Tuesday of every month, 7:00 PM".
    static std::string describe(const MeetingSeries& s);

private:
    SqliteDatabase& db_;
    MeetingService& meetings_;
    MeetingSeries row(Statement& st);
};
