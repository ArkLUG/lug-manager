-- Attendance perk levels with Discord role rewards
CREATE TABLE IF NOT EXISTS perk_levels (
    id                           INTEGER PRIMARY KEY AUTOINCREMENT,
    name                         TEXT    NOT NULL,
    discord_role_id              TEXT    NOT NULL DEFAULT '',
    meeting_attendance_required  INTEGER NOT NULL DEFAULT 0,
    event_attendance_required    INTEGER NOT NULL DEFAULT 0,
    requires_paid_dues           INTEGER NOT NULL DEFAULT 0,
    sort_order                   INTEGER NOT NULL DEFAULT 0,
    created_at                   TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at                   TEXT    NOT NULL DEFAULT (datetime('now'))
);
