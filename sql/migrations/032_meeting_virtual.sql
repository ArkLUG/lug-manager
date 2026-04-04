-- Support wholly-virtual meetings with optional Discord voice channel
ALTER TABLE meetings ADD COLUMN is_virtual INTEGER NOT NULL DEFAULT 0;
ALTER TABLE meetings ADD COLUMN discord_voice_channel_id TEXT NOT NULL DEFAULT '';
