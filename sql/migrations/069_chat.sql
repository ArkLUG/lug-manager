-- Chat integrations (Discord today; Slack, Matrix etc. can be added).
--
-- Settings (lug_settings), per provider, "1"/"0", default on:
--   chat.<provider>.event_thread, .event_announce, .event_chapter_announce,
--   .event_scheduled, .meeting_announce, .meeting_scheduled, .dms, .challenges
-- (Discord keeps its existing discord_suppress_pings / discord_suppress_updates
-- and reminder settings), and chat.quiet = "1" for edits only, nothing new.
-- Message wording: tmpl.<key> / tmpl.<key>.subject (see chat/Templates.hpp).

-- Per meeting/event: parts not to post, comma-separated
-- (announce, chapter_announce, thread, scheduled, update_note).
CREATE TABLE IF NOT EXISTS chat_item_options (
    entity_type TEXT    NOT NULL,          -- event | meeting
    entity_id   INTEGER NOT NULL,
    skip        TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (entity_type, entity_id)
);

-- Where posts went, for providers other than Discord (Discord keeps using
-- the discord_* columns on lug_events / meetings).
CREATE TABLE IF NOT EXISTS chat_posts (
    provider    TEXT    NOT NULL,
    entity_type TEXT    NOT NULL,
    entity_id   INTEGER NOT NULL,
    purpose     TEXT    NOT NULL,          -- announce | chapter_announce | thread | scheduled | thread_owned
    ref         TEXT    NOT NULL DEFAULT '',
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (provider, entity_type, entity_id, purpose)
);

-- What was sent, edited or deleted, and what failed (newest kept).
CREATE TABLE IF NOT EXISTS chat_activity (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    provider    TEXT    NOT NULL,
    action      TEXT    NOT NULL,          -- post | edit | delete | thread | event | dm | skip
    what        TEXT    NOT NULL DEFAULT '',  -- template key or short description
    entity_type TEXT    NOT NULL DEFAULT '',
    entity_id   INTEGER NOT NULL DEFAULT 0,
    ok          INTEGER NOT NULL DEFAULT 1,
    error       TEXT    NOT NULL DEFAULT '',
    channel     TEXT    NOT NULL DEFAULT '',
    payload     TEXT    NOT NULL DEFAULT '',  -- failed standalone posts: the text, so they can be retried
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_chat_activity_created ON chat_activity(created_at);
