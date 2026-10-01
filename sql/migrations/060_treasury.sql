-- LUG money in and out other than dues (dues come from dues_payments).
CREATE TABLE IF NOT EXISTS treasury_entries (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    entry_on     TEXT    NOT NULL,                  -- YYYY-MM-DD
    kind         TEXT    NOT NULL CHECK (kind IN ('income','expense')),
    category     TEXT    NOT NULL DEFAULT '',
    amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
    description  TEXT    NOT NULL DEFAULT '',
    event_id     INTEGER REFERENCES lug_events(id) ON DELETE SET NULL,
    recorded_by  INTEGER REFERENCES members(id) ON DELETE SET NULL,
    created_at   TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_treasury_entries_on ON treasury_entries(entry_on);
CREATE INDEX IF NOT EXISTS idx_treasury_entries_event ON treasury_entries(event_id);
