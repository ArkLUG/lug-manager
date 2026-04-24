#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/EventDay.hpp"
#include <vector>
#include <optional>
#include <string>

class EventDayRepository {
public:
    explicit EventDayRepository(SqliteDatabase& db);

    std::vector<EventDay> find_by_event(int64_t event_id);
    std::optional<EventDay> find_by_event_and_date(int64_t event_id, const std::string& day_date);
    std::optional<EventDay> find_by_id(int64_t id);

    // Rebuilds event_days rows for an event from its start_time/end_time.
    // Preserves existing rows where day_date matches, removes rows outside the
    // new range, and inserts any missing rows. Existing event_day_attendance
    // rows survive when their day_date still falls in the new range; rows on
    // days dropped from the range are deleted via ON DELETE CASCADE.
    void sync_for_event(int64_t event_id,
                        const std::string& start_time,
                        const std::string& end_time);

private:
    SqliteDatabase& db_;
};
