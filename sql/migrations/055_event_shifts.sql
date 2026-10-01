-- Volunteer shifts for events (set-up, tear-down, booth duty...).
CREATE TABLE IF NOT EXISTS event_shifts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id   INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    title      TEXT    NOT NULL,
    starts_at  TEXT    NOT NULL,   -- LUG-local ISO "YYYY-MM-DDTHH:MM"
    ends_at    TEXT    NOT NULL,
    slots      INTEGER NOT NULL DEFAULT 1,
    notes      TEXT    NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_event_shifts_event ON event_shifts(event_id, starts_at);

CREATE TABLE IF NOT EXISTS event_shift_signups (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    shift_id    INTEGER NOT NULL REFERENCES event_shifts(id) ON DELETE CASCADE,
    member_id   INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    reminded_at TEXT,
    UNIQUE(shift_id, member_id)
);
