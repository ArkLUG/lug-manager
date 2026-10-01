-- Which browser/device a session belongs to, shown on the sessions list so
-- members can recognise and sign out other devices.
ALTER TABLE sessions ADD COLUMN user_agent TEXT NOT NULL DEFAULT '';
