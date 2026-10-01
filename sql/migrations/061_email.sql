-- Email for members without Discord: unsubscribe token (no-login links in
-- every email) and one-time sign-in links.
ALTER TABLE members ADD COLUMN email_token TEXT NOT NULL DEFAULT '';

CREATE TABLE IF NOT EXISTS email_login_tokens (
    token_hash TEXT    PRIMARY KEY,               -- SHA-256 of the emailed token
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    created_at TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now')),
    expires_at TEXT    NOT NULL,
    used_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_email_login_tokens_member ON email_login_tokens(member_id, created_at);
