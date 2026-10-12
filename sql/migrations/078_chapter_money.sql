-- Chapter money (Aaron, 2026-10-08): part of each dues payment belongs to the
-- member's chapter and the rest to the LUG (the split is set on Settings >
-- Dues, default 50%), and income/expenses say which chapter they're for (or
-- the whole LUG).
--
-- Each payment keeps the chapter and the percentage it was recorded with,
-- so changing the setting or someone moving chapter never rewrites history.
-- The LUG-wide totals are unchanged: this only splits them.
ALTER TABLE dues_payments ADD COLUMN chapter_id  INTEGER REFERENCES chapters(id) ON DELETE SET NULL;
ALTER TABLE dues_payments ADD COLUMN chapter_pct INTEGER;
ALTER TABLE treasury_entries ADD COLUMN chapter_id INTEGER REFERENCES chapters(id) ON DELETE SET NULL;   -- NULL = whole LUG
CREATE INDEX IF NOT EXISTS idx_dues_payments_chapter ON dues_payments(chapter_id);
CREATE INDEX IF NOT EXISTS idx_treasury_entries_chapter ON treasury_entries(chapter_id);

-- Payments already recorded: the member's chapter today, at 50%.
UPDATE dues_payments
   SET chapter_id  = (SELECT cm.chapter_id FROM chapter_members cm WHERE cm.member_id = dues_payments.member_id ORDER BY cm.rowid LIMIT 1),
       chapter_pct = 50;

-- New payments (however they're recorded) take the member's chapter and the
-- share set on Settings > Dues at that moment.
CREATE TRIGGER IF NOT EXISTS dues_payments_chapter AFTER INSERT ON dues_payments
WHEN NEW.chapter_pct IS NULL
BEGIN
    UPDATE dues_payments
       SET chapter_id  = COALESCE(NEW.chapter_id,
                                  (SELECT cm.chapter_id FROM chapter_members cm WHERE cm.member_id = NEW.member_id ORDER BY cm.rowid LIMIT 1)),
           chapter_pct = COALESCE((SELECT CAST(value AS INTEGER) FROM lug_settings
                                    WHERE key = 'dues_chapter_share' AND value <> '' AND CAST(value AS INTEGER) BETWEEN 0 AND 100), 50)
     WHERE id = NEW.id;
END;
