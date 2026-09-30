-- Session tokens are now stored as SHA-256(token) rather than the raw
-- bearer value, so a leaked database (backup, copied volume) can't be used
-- to hijack live sessions. Rows written before this change still hold the
-- raw token; SessionStore hashes them in place at startup and flips this
-- flag (hashing can't be done in SQL), so nobody is logged out.
ALTER TABLE sessions ADD COLUMN token_is_hash INTEGER NOT NULL DEFAULT 0;
