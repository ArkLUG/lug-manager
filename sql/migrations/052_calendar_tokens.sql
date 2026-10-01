-- Personal calendar feed tokens (SHA-256 stored, like sessions/API keys).
-- /calendar/me/<token>.ics shows private items with full details, so the
-- URL is a secret; members can regenerate it from the dashboard.
ALTER TABLE members ADD COLUMN calendar_token_hash TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_members_calendar_token ON members(calendar_token_hash);
