-- Per-day event attendance
--
-- Events can span multiple days. For each event day, a member's attendance is
-- tracked with a `qualifies` flag (defaults to true) indicating whether they
-- attended long enough for the day to count toward perk credit (4+ hours).
-- A member earns 1 event credit per event if any of their day-attendance rows
-- qualifies.

CREATE TABLE IF NOT EXISTS event_days (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    day_date    TEXT    NOT NULL,  -- YYYY-MM-DD
    day_number  INTEGER NOT NULL,  -- 1-indexed
    UNIQUE(event_id, day_date),
    UNIQUE(event_id, day_number)
);
CREATE INDEX IF NOT EXISTS idx_event_days_event ON event_days(event_id);
CREATE INDEX IF NOT EXISTS idx_event_days_date  ON event_days(day_date);

CREATE TABLE IF NOT EXISTS event_day_attendance (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    event_day_id   INTEGER NOT NULL REFERENCES event_days(id) ON DELETE CASCADE,
    member_id      INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    checked_in_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    notes          TEXT    NOT NULL DEFAULT '',
    qualifies      INTEGER NOT NULL DEFAULT 1,  -- 1 = 4+ hours (counts for perk), 0 = partial
    UNIQUE(event_day_id, member_id)
);
CREATE INDEX IF NOT EXISTS idx_event_day_att_day    ON event_day_attendance(event_day_id);
CREATE INDEX IF NOT EXISTS idx_event_day_att_member ON event_day_attendance(member_id);

-- Populate event_days from existing events. For each event, create one row
-- per calendar day from start_time's date through end_time's date.
-- SQLite's recursive CTE handles the date range.
INSERT INTO event_days (event_id, day_date, day_number)
WITH RECURSIVE days(event_id, day_date, day_number, end_date) AS (
    SELECT id,
           substr(start_time, 1, 10),
           1,
           substr(end_time, 1, 10)
    FROM lug_events
    WHERE length(start_time) >= 10 AND length(end_time) >= 10
    UNION ALL
    SELECT event_id,
           date(day_date, '+1 day'),
           day_number + 1,
           end_date
    FROM days
    WHERE day_date < end_date
)
SELECT event_id, day_date, day_number FROM days;

-- Migrate existing event attendance to event_day_attendance.
-- Existing rows are mapped to day 1 of the event (per user decision) with
-- qualifies=true, matching the new default.
INSERT INTO event_day_attendance (event_day_id, member_id, checked_in_at, notes, qualifies)
SELECT ed.id, a.member_id, a.checked_in_at, a.notes, 1
FROM attendance a
JOIN event_days ed ON ed.event_id = a.entity_id AND ed.day_number = 1
WHERE a.entity_type = 'event';

-- Remove the migrated event rows from the old attendance table.
-- Meetings continue to use the attendance table unchanged.
DELETE FROM attendance WHERE entity_type = 'event';
