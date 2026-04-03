-- Allow members without Discord accounts (e.g. KFOLs).
-- discord_user_id and discord_username become nullable.
-- UNIQUE constraint kept but allows multiple NULLs in SQLite.

CREATE TABLE IF NOT EXISTS members_new (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_user_id   TEXT    UNIQUE,
    discord_username  TEXT    NOT NULL DEFAULT '',
    display_name      TEXT    NOT NULL,
    email             TEXT,
    is_paid           INTEGER NOT NULL DEFAULT 0,
    paid_until        TEXT,
    role              TEXT    NOT NULL DEFAULT 'member' CHECK(role IN ('admin','chapter_lead','member')),
    first_name        TEXT    NOT NULL DEFAULT '',
    last_name         TEXT    NOT NULL DEFAULT '',
    birthday          TEXT    NOT NULL DEFAULT '',
    fol_status        TEXT    NOT NULL DEFAULT 'afol' CHECK(fol_status IN ('kfol','tfol','afol')),
    created_at        TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT    NOT NULL DEFAULT (datetime('now'))
);

INSERT INTO members_new (id, discord_user_id, discord_username, display_name, email,
                         is_paid, paid_until, role, first_name, last_name,
                         birthday, fol_status, created_at, updated_at)
SELECT id, discord_user_id, discord_username, display_name, email,
       is_paid, paid_until, role, first_name, last_name,
       birthday, fol_status, created_at, updated_at
FROM members;

DROP TABLE members;
ALTER TABLE members_new RENAME TO members;

CREATE INDEX IF NOT EXISTS idx_sessions_member  ON sessions(member_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
