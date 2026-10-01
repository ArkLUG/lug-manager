-- Guardian contact + consent tracking for young members (KFOL/TFOL), shown
-- to organisers at check-in so they know who may be photographed.
ALTER TABLE members ADD COLUMN guardian_name   TEXT    NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN guardian_phone  TEXT    NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN guardian_email  TEXT    NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN consent_on_file INTEGER NOT NULL DEFAULT 0;
ALTER TABLE members ADD COLUMN consent_date    TEXT    NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN photo_release   INTEGER NOT NULL DEFAULT 0;
