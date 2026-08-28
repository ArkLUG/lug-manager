-- Review queue for Discord guild members that plausibly match an existing
-- member with no discord_user_id, instead of silently auto-creating a duplicate.
CREATE TABLE IF NOT EXISTS pending_discord_matches (
    id                     INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_user_id        TEXT    NOT NULL,
    discord_username       TEXT    NOT NULL DEFAULT '',
    discord_display_name   TEXT    NOT NULL DEFAULT '',
    discord_role_ids       TEXT    NOT NULL DEFAULT '', -- comma-separated
    suggested_member_id    INTEGER,                     -- FK members.id, nullable
    created_at             TEXT    NOT NULL DEFAULT (datetime('now')),
    resolved_at            TEXT,
    resolved_action        TEXT,   -- 'linked' | 'created_new' | NULL if unresolved
    resolved_member_id     INTEGER
);
CREATE INDEX idx_pending_discord_matches_discord_id ON pending_discord_matches(discord_user_id);
CREATE INDEX idx_pending_discord_matches_unresolved ON pending_discord_matches(resolved_at);
