-- Per-member notification opt-outs. Everything is on by default (subject to
-- the LUG-wide settings); a row here turns one kind off for one member.
CREATE TABLE IF NOT EXISTS notification_optouts (
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    kind       TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now')),
    PRIMARY KEY (member_id, kind)
);
