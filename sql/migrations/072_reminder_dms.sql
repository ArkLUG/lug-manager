-- Reminder DMs sent with buttons (Remind me later, Don't remind me, Can't make
-- it...). The buttons carry this row's id; a click is checked against the
-- member it was sent to. snooze_until (unix time) = send it again then.
CREATE TABLE IF NOT EXISTS reminder_dms (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id     INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    kind          TEXT    NOT NULL,              -- NotificationPrefs kind
    ref           TEXT    NOT NULL DEFAULT '',   -- event id, shift signup id, ...
    template_key  TEXT    NOT NULL,
    values_json   TEXT    NOT NULL DEFAULT '{}',
    sent_at       TEXT    NOT NULL DEFAULT (datetime('now')),
    snooze_until  INTEGER,
    resent        INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_reminder_dms_snooze ON reminder_dms(snooze_until) WHERE snooze_until IS NOT NULL AND resent = 0;
