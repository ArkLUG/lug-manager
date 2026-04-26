-- Drop the per-day `qualifies` flag from event_day_attendance.
--
-- Day-level attendance is now binary: a member either attended a given day
-- of an event or they did not. The 4+ hour distinction is removed; every
-- existing day-attendance row simply counts as attended.
ALTER TABLE event_day_attendance DROP COLUMN qualifies;
