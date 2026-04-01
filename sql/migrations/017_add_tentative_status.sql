-- Add 'tentative' to lug_events status. SQLite requires table recreation to modify CHECK constraints.
CREATE TABLE lug_events_new (
    id                          INTEGER PRIMARY KEY AUTOINCREMENT,
    title                       TEXT    NOT NULL,
    description                 TEXT    NOT NULL DEFAULT '',
    location                    TEXT    NOT NULL DEFAULT '',
    start_time                  TEXT    NOT NULL,
    end_time                    TEXT    NOT NULL,
    status                      TEXT    NOT NULL DEFAULT 'confirmed' CHECK(status IN ('tentative','confirmed','open','closed','cancelled')),
    discord_thread_id           TEXT,
    discord_event_id            TEXT,
    google_calendar_event_id    TEXT,
    ical_uid                    TEXT    NOT NULL UNIQUE,
    signup_deadline             TEXT,
    max_attendees               INTEGER NOT NULL DEFAULT 0,
    scope                       TEXT    NOT NULL DEFAULT 'chapter',
    chapter_id                  INTEGER REFERENCES chapters(id) ON DELETE CASCADE,
    event_lead_id               INTEGER REFERENCES members(id) ON DELETE SET NULL,
    discord_chapter_message_id  TEXT,
    discord_lug_message_id      TEXT,
    discord_ping_role_ids       TEXT,
    created_at                  TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at                  TEXT    NOT NULL DEFAULT (datetime('now'))
);

INSERT INTO lug_events_new SELECT
    id, title, description, location, start_time, end_time,
    CASE WHEN status = 'announced' THEN 'confirmed' ELSE status END,
    discord_thread_id, discord_event_id, google_calendar_event_id,
    ical_uid, signup_deadline, max_attendees, scope, chapter_id, event_lead_id,
    discord_chapter_message_id, discord_lug_message_id, discord_ping_role_ids,
    created_at, updated_at
FROM lug_events;

DROP TABLE lug_events;
ALTER TABLE lug_events_new RENAME TO lug_events;
