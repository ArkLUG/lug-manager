-- Recurring meetings: a series is a template + rule; SeriesService creates
-- the individual meetings a few weeks ahead (they're ordinary meetings and
-- can be edited/cancelled one by one).
CREATE TABLE IF NOT EXISTS meeting_series (
    id                        INTEGER PRIMARY KEY AUTOINCREMENT,
    title                     TEXT    NOT NULL,
    description               TEXT    NOT NULL DEFAULT '',
    location                  TEXT    NOT NULL DEFAULT '',
    scope                     TEXT    NOT NULL DEFAULT 'chapter',
    chapter_id                INTEGER NOT NULL DEFAULT 0,
    is_virtual                INTEGER NOT NULL DEFAULT 0,
    discord_voice_channel_id  TEXT    NOT NULL DEFAULT '',
    start_hm                  TEXT    NOT NULL,          -- "19:00"
    end_hm                    TEXT    NOT NULL,          -- "21:00"
    rule                      TEXT    NOT NULL CHECK(rule IN ('weekly','monthly')),
    weekday                   INTEGER NOT NULL,          -- 0=Sunday .. 6=Saturday
    interval_weeks            INTEGER NOT NULL DEFAULT 1,-- weekly: every N weeks
    nth                       INTEGER NOT NULL DEFAULT 1,-- monthly: 1..4, or -1 = last
    starts_on                 TEXT    NOT NULL,          -- YYYY-MM-DD
    ends_on                   TEXT    NOT NULL DEFAULT '',
    days_ahead                INTEGER NOT NULL DEFAULT 45,
    suppress_discord          INTEGER NOT NULL DEFAULT 0,
    suppress_calendar         INTEGER NOT NULL DEFAULT 0,
    is_private                INTEGER NOT NULL DEFAULT 0,
    excludes_perks            INTEGER NOT NULL DEFAULT 0,
    active                    INTEGER NOT NULL DEFAULT 1,
    created_by                INTEGER,
    created_at                TEXT    NOT NULL DEFAULT (datetime('now'))
);
ALTER TABLE meetings ADD COLUMN series_id INTEGER;
CREATE INDEX IF NOT EXISTS idx_meetings_series ON meetings(series_id, start_time);
