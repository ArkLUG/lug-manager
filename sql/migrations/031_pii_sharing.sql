-- Convert pii_public boolean to pii_sharing text: "none"|"verified"|"all"
ALTER TABLE members ADD COLUMN pii_sharing TEXT NOT NULL DEFAULT 'none';
UPDATE members SET pii_sharing = CASE WHEN pii_public = 1 THEN 'all' ELSE 'none' END;
