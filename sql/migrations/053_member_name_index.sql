-- Case-insensitive name lookups (manual check-in matches on first+last name).
CREATE INDEX IF NOT EXISTS idx_members_name ON members(last_name COLLATE NOCASE, first_name COLLATE NOCASE);
