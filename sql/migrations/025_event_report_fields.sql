-- Additional fields for event reports
ALTER TABLE lug_events ADD COLUMN entrance_fee     TEXT NOT NULL DEFAULT '';
ALTER TABLE lug_events ADD COLUMN public_kids      INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lug_events ADD COLUMN public_teens     INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lug_events ADD COLUMN public_adults    INTEGER NOT NULL DEFAULT 0;
ALTER TABLE lug_events ADD COLUMN social_media_links TEXT NOT NULL DEFAULT '';
ALTER TABLE lug_events ADD COLUMN event_feedback   TEXT NOT NULL DEFAULT '';
