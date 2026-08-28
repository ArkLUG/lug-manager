-- Adds 'moderator' as a valid members.role value (same privilege tier as
-- chapter_lead, granted manually rather than via Discord role-mapping sync).
-- SQLite can't ALTER a CHECK constraint in place, so rebuild the table exactly
-- as it stands today (per PRAGMA table_info / .schema on a fully-migrated DB).

CREATE TABLE members_new (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    discord_user_id   TEXT    UNIQUE,
    discord_username  TEXT    NOT NULL DEFAULT '',
    display_name      TEXT    NOT NULL,
    email             TEXT,
    is_paid           INTEGER NOT NULL DEFAULT 0,
    paid_until        TEXT,
    role              TEXT    NOT NULL DEFAULT 'member' CHECK(role IN ('admin','chapter_lead','moderator','member')),
    first_name        TEXT    NOT NULL DEFAULT '',
    last_name         TEXT    NOT NULL DEFAULT '',
    birthday          TEXT    NOT NULL DEFAULT '',
    fol_status        TEXT    NOT NULL DEFAULT 'afol' CHECK(fol_status IN ('kfol','tfol','afol')),
    created_at        TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at        TEXT    NOT NULL DEFAULT (datetime('now')),
    phone             TEXT    NOT NULL DEFAULT '',
    address_line1     TEXT    NOT NULL DEFAULT '',
    address_line2     TEXT    NOT NULL DEFAULT '',
    city              TEXT    NOT NULL DEFAULT '',
    state             TEXT    NOT NULL DEFAULT '',
    zip               TEXT    NOT NULL DEFAULT '',
    pii_public        INTEGER NOT NULL DEFAULT 0,
    pii_sharing       TEXT    NOT NULL DEFAULT 'none',
    sharing_email     TEXT    NOT NULL DEFAULT 'none',
    sharing_phone     TEXT    NOT NULL DEFAULT 'none',
    sharing_address   TEXT    NOT NULL DEFAULT 'none',
    sharing_birthday  TEXT    NOT NULL DEFAULT 'none',
    sharing_discord   TEXT    NOT NULL DEFAULT 'none'
);

INSERT INTO members_new SELECT * FROM members;

DROP TABLE members;
ALTER TABLE members_new RENAME TO members;

CREATE INDEX IF NOT EXISTS idx_sessions_member  ON sessions(member_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
