-- Treasurer: a member (any role) who may manage the Treasury and record dues.
ALTER TABLE members ADD COLUMN is_treasurer INTEGER NOT NULL DEFAULT 0;
-- Receipt (photo or PDF) attached to a treasury entry: file name under data/uploads/receipts.
ALTER TABLE treasury_entries ADD COLUMN receipt_file TEXT NOT NULL DEFAULT '';
