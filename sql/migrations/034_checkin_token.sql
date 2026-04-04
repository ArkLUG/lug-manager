-- Public check-in token for QR code self-service attendance
ALTER TABLE meetings ADD COLUMN checkin_token TEXT NOT NULL DEFAULT '';
ALTER TABLE lug_events ADD COLUMN checkin_token TEXT NOT NULL DEFAULT '';
