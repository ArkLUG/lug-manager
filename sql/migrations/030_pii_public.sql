-- Allow members to opt-in to making their PII visible to all authenticated users
ALTER TABLE members ADD COLUMN pii_public INTEGER NOT NULL DEFAULT 0;
