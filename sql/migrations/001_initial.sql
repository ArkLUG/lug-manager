-- LUG Manager Initial Schema

CREATE TABLE IF NOT EXISTS _schema_migrations (
    version    INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS members (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_user_id   TEXT    NOT NULL UNIQUE,
    discord_username  TEXT    NOT NULL,
    display_name      TEXT    NOT NULL,
    email             TEXT,
    is_paid           INTEGER NOT NULL DEFAULT 0,
    paid_until        TEXT,
    role              TEXT    NOT NULL DEFAULT 'member' CHECK(role IN ('admin','member','readonly')),
    created_at        TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS meetings (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    title            TEXT    NOT NULL,
    description      TEXT    NOT NULL DEFAULT '',
    location         TEXT    NOT NULL DEFAULT '',
    start_time       TEXT    NOT NULL,
    end_time         TEXT    NOT NULL,
    status           TEXT    NOT NULL DEFAULT 'scheduled' CHECK(status IN ('scheduled','cancelled','completed')),
    discord_event_id TEXT,
    ical_uid         TEXT    NOT NULL UNIQUE,
    created_at       TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS lug_events (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    title              TEXT    NOT NULL,
    description        TEXT    NOT NULL DEFAULT '',
    event_type         TEXT    NOT NULL DEFAULT 'other' CHECK(event_type IN ('showcase','swap_meet','convention','other')),
    location           TEXT    NOT NULL DEFAULT '',
    start_time         TEXT    NOT NULL,
    end_time           TEXT    NOT NULL,
    status             TEXT    NOT NULL DEFAULT 'announced' CHECK(status IN ('announced','open','closed','cancelled')),
    discord_thread_id  TEXT,
    discord_event_id   TEXT,
    ical_uid           TEXT    NOT NULL UNIQUE,
    signup_deadline    TEXT,
    max_attendees      INTEGER NOT NULL DEFAULT 0,
    created_at         TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at         TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS attendance (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id     INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    entity_type   TEXT    NOT NULL CHECK(entity_type IN ('meeting','event')),
    entity_id     INTEGER NOT NULL,
    checked_in_at TEXT    NOT NULL DEFAULT (datetime('now')),
    notes         TEXT    NOT NULL DEFAULT '',
    UNIQUE(member_id, entity_type, entity_id)
);

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT    PRIMARY KEY,
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    role       TEXT    NOT NULL,
    expires_at TEXT    NOT NULL,
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_attendance_member  ON attendance(member_id);
CREATE INDEX IF NOT EXISTS idx_attendance_entity  ON attendance(entity_type, entity_id);
CREATE INDEX IF NOT EXISTS idx_meetings_start     ON meetings(start_time);
CREATE INDEX IF NOT EXISTS idx_lug_events_start   ON lug_events(start_time);
CREATE INDEX IF NOT EXISTS idx_sessions_member    ON sessions(member_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires   ON sessions(expires_at);
