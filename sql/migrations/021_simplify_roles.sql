-- Simplify roles: remove 'readonly', add birthday/fol_status fields
-- SQLite cannot ALTER CHECK constraints, so we recreate the table.
-- Foreign keys are OFF by default in SQLite, so DROP TABLE won't cascade.

CREATE TABLE IF NOT EXISTS members_new (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_user_id   TEXT    NOT NULL UNIQUE,
    discord_username  TEXT    NOT NULL,
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
       is_paid, paid_until,
       CASE WHEN role = 'readonly' THEN 'member' ELSE role END,
       first_name, last_name,
       '', 'afol', created_at, updated_at
FROM members;

DROP TABLE members;
ALTER TABLE members_new RENAME TO members;

-- Recreate indexes
CREATE INDEX IF NOT EXISTS idx_sessions_member    ON sessions(member_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires   ON sessions(expires_at);

-- Migrate stale sessions
UPDATE sessions SET role = 'member' WHERE role = 'readonly';
