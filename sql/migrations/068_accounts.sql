-- Sign-in without Discord: email + password, TOTP two-factor with recovery
-- codes, and password reset links. Settings (lug_settings):
--   auth_password_enabled  "0" turns password sign-in off (default on)
--   auth_email_links       "0" turns emailed sign-in links off (default on, needs SMTP)
--   auth_require_2fa       "off" | "staff" (everyone above member) | "everyone"
ALTER TABLE members ADD COLUMN password_hash       TEXT    NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN password_changed_at TEXT;
ALTER TABLE members ADD COLUMN totp_secret         TEXT    NOT NULL DEFAULT '';   -- base32, set while 2FA is on
ALTER TABLE members ADD COLUMN totp_pending_secret TEXT    NOT NULL DEFAULT '';   -- during setup, until confirmed
ALTER TABLE members ADD COLUMN totp_enabled_at     TEXT;
ALTER TABLE members ADD COLUMN totp_last_step      INTEGER NOT NULL DEFAULT 0;    -- no code is accepted twice

-- One-time recovery codes (sha256), for a lost authenticator.
CREATE TABLE IF NOT EXISTS recovery_codes (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    code_hash  TEXT    NOT NULL,
    used_at    TEXT,
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_recovery_codes_member ON recovery_codes(member_id);

-- First factor passed, waiting for the 6-digit code (5 minutes, 5 tries).
CREATE TABLE IF NOT EXISTS login_challenges (
    token_hash TEXT    PRIMARY KEY,
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    method     TEXT    NOT NULL,                 -- password | discord | email_link
    next_path  TEXT    NOT NULL DEFAULT '',
    attempts   INTEGER NOT NULL DEFAULT 0,
    created_at TEXT    NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT    NOT NULL
);

-- "Set your password" links: emailed (forgot password) or made by an admin.
CREATE TABLE IF NOT EXISTS password_reset_tokens (
    token_hash TEXT    PRIMARY KEY,
    member_id  INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    created_by INTEGER REFERENCES members(id) ON DELETE SET NULL,
    created_at TEXT    NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT    NOT NULL,
    used_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_password_reset_member ON password_reset_tokens(member_id);
