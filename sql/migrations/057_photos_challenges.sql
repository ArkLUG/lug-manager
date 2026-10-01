-- Event photo galleries and monthly build challenges. Image files live in
-- <data_dir>/uploads under random names; these tables hold the metadata.
CREATE TABLE IF NOT EXISTS event_photos (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    member_id   INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    file        TEXT    NOT NULL,
    caption     TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_event_photos_event ON event_photos(event_id, created_at);

CREATE TABLE IF NOT EXISTS challenges (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    title        TEXT    NOT NULL,
    description  TEXT    NOT NULL DEFAULT '',
    starts_on    TEXT    NOT NULL,            -- submissions open (YYYY-MM-DD)
    ends_on      TEXT    NOT NULL,            -- submissions close; voting runs 7 more days
    announced    INTEGER NOT NULL DEFAULT 0,
    created_by   INTEGER,
    created_at   TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS challenge_entries (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    challenge_id  INTEGER NOT NULL REFERENCES challenges(id) ON DELETE CASCADE,
    member_id     INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    title         TEXT    NOT NULL,
    file          TEXT    NOT NULL,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now')),
    UNIQUE(challenge_id, member_id)
);
CREATE TABLE IF NOT EXISTS challenge_votes (
    challenge_id  INTEGER NOT NULL REFERENCES challenges(id) ON DELETE CASCADE,
    entry_id      INTEGER NOT NULL REFERENCES challenge_entries(id) ON DELETE CASCADE,
    member_id     INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    PRIMARY KEY (challenge_id, member_id)      -- one vote per member per challenge
);
