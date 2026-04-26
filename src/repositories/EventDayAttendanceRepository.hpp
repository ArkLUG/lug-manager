#pragma once
#include "db/SqliteDatabase.hpp"
#include "models/EventDay.hpp"
#include <vector>
#include <optional>
#include <string>

class EventDayAttendanceRepository {
public:
    explicit EventDayAttendanceRepository(SqliteDatabase& db);

    // Check a member in to a specific event day. Returns true if a new row was
    // inserted, false if they were already checked in.
    bool check_in(int64_t event_day_id, int64_t member_id,
                  const std::string& notes = "");

    bool is_checked_in(int64_t event_day_id, int64_t member_id);

    // All day-attendance rows for an event (across all days), joined with
    // member info and the day's date/number. Ordered by day_number, then
    // checked_in_at.
    std::vector<EventDayAttendance> find_by_event(int64_t event_id);

    // All day-attendance rows for a single day.
    std::vector<EventDayAttendance> find_by_day(int64_t event_day_id);

    // Distinct members with any attendance for the event.
    int count_distinct_members_by_event(int64_t event_id);

    // Has the member attended at least one day of this event?
    bool member_attended_event(int64_t member_id, int64_t event_id);

    bool remove_by_id(int64_t attendance_id);
    std::optional<EventDayAttendance> find_by_id(int64_t attendance_id);

    // Year-scoped helper — a member gets 1 event credit per event they
    // attended any day of, within the year.
    int count_events_credited_for_member(int64_t member_id, int year);

private:
    SqliteDatabase& db_;
};
