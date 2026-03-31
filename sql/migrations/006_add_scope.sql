-- Add scope to lug_events and meetings
-- Values: 'lug_wide' | 'chapter' | 'non_lug'
-- Default 'chapter' preserves existing behaviour for all current rows.
ALTER TABLE lug_events ADD COLUMN scope TEXT NOT NULL DEFAULT 'chapter';
ALTER TABLE meetings    ADD COLUMN scope TEXT NOT NULL DEFAULT 'chapter';
