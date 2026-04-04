-- Add address and phone number fields to members (PII)
ALTER TABLE members ADD COLUMN phone         TEXT NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN address_line1 TEXT NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN address_line2 TEXT NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN city          TEXT NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN state         TEXT NOT NULL DEFAULT '';
ALTER TABLE members ADD COLUMN zip           TEXT NOT NULL DEFAULT '';
