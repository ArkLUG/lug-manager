-- "Private" events/meetings still publish to Google Calendar (unlike
-- suppress_calendar, which skips the calendar entirely) but with
-- title/description/location redacted to a generic placeholder, so the
-- shared calendar shows the LUG is busy without exposing details to anyone
-- who can see the calendar but isn't a member.

ALTER TABLE lug_events ADD COLUMN is_private INTEGER NOT NULL DEFAULT 0;

ALTER TABLE meetings   ADD COLUMN is_private INTEGER NOT NULL DEFAULT 0;
