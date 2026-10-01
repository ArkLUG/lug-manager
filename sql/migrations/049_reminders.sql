-- When the automatic Discord reminder for a meeting/event went out (NULL =
-- not yet). Set atomically by ReminderService so a reminder is sent once.
ALTER TABLE meetings   ADD COLUMN reminder_sent_at TEXT;
ALTER TABLE lug_events ADD COLUMN reminder_sent_at TEXT;
