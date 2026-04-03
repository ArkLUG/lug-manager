-- Notes/report fields for events and meetings
ALTER TABLE lug_events ADD COLUMN notes                  TEXT NOT NULL DEFAULT '';
ALTER TABLE lug_events ADD COLUMN notes_discord_post_id  TEXT NOT NULL DEFAULT '';
ALTER TABLE meetings   ADD COLUMN notes                  TEXT NOT NULL DEFAULT '';
ALTER TABLE meetings   ADD COLUMN notes_discord_post_id  TEXT NOT NULL DEFAULT '';
