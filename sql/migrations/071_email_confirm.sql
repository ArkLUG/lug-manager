-- An email a member types in themselves must be confirmed (a link sent to
-- it) before it can be used to sign in or receive mail. Emails already on
-- record, set by an admin, or taken from a verified Discord account count
-- as confirmed.
ALTER TABLE members ADD COLUMN email_confirmed INTEGER NOT NULL DEFAULT 1;

CREATE TABLE IF NOT EXISTS email_confirm_tokens (
    token_hash TEXT PRIMARY KEY,
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    email      TEXT NOT NULL,          -- the address it confirms (lower case)
    expires_at TEXT NOT NULL,          -- UTC "YYYY-MM-DDTHH:MM:SS"
    used_at    TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%S','now'))
);
CREATE INDEX IF NOT EXISTS idx_email_confirm_member ON email_confirm_tokens(member_id, created_at);
