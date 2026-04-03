-- Per-entity suppress flags for historical data entry
ALTER TABLE lug_events ADD COLUMN suppress_discord  INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lug_events ADD COLUMN suppress_calendar INTEGER NOT NULL DEFAULT 0;
ALTER TABLE meetings   ADD COLUMN suppress_discord  INTEGER NOT NULL DEFAULT 0;
ALTER TABLE meetings   ADD COLUMN suppress_calendar INTEGER NOT NULL DEFAULT 0;
