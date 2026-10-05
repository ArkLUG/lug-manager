-- A show's day-by-day schedule: time blocks within its days, each setup,
-- public (open to visitors), teardown or other. Several per day are fine
-- (e.g. Sunday public 10-4 then teardown 4-6). Setup can be on days before
-- the public dates. An event with no blocks works as before: all-day dates.
CREATE TABLE IF NOT EXISTS event_blocks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id   INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    day_date   TEXT    NOT NULL,                       -- YYYY-MM-DD
    kind       TEXT    NOT NULL DEFAULT 'public' CHECK (kind IN ('setup','public','teardown','other')),
    starts_at  TEXT    NOT NULL,                       -- HH:MM, LUG-local
    ends_at    TEXT    NOT NULL,                       -- HH:MM
    label      TEXT    NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_event_blocks_event ON event_blocks(event_id, day_date, starts_at);
