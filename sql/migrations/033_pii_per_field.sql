-- Per-field PII sharing replaces single pii_sharing column
ALTER TABLE members ADD COLUMN sharing_email TEXT NOT NULL DEFAULT 'none';
ALTER TABLE members ADD COLUMN sharing_phone TEXT NOT NULL DEFAULT 'none';
ALTER TABLE members ADD COLUMN sharing_address TEXT NOT NULL DEFAULT 'none';
ALTER TABLE members ADD COLUMN sharing_birthday TEXT NOT NULL DEFAULT 'none';
ALTER TABLE members ADD COLUMN sharing_discord TEXT NOT NULL DEFAULT 'none';

-- Migrate existing pii_sharing value to all per-field columns
UPDATE members SET
  sharing_email    = COALESCE(pii_sharing, 'none'),
  sharing_phone    = COALESCE(pii_sharing, 'none'),
  sharing_address  = COALESCE(pii_sharing, 'none'),
  sharing_birthday = COALESCE(pii_sharing, 'none'),
  sharing_discord  = COALESCE(pii_sharing, 'none');
