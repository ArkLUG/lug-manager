-- Display (MOC) space requests for public shows. Members register what they
-- want to bring and its footprint; the event lead approves and assigns a
-- table. Only events with accepts_displays=1 take new requests.
ALTER TABLE lug_events ADD COLUMN accepts_displays INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS event_display_requests (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id         INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    member_id        INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    title            TEXT    NOT NULL,
    description      TEXT    NOT NULL DEFAULT '',
    width_in         INTEGER NOT NULL DEFAULT 0,  -- footprint, inches
    depth_in         INTEGER NOT NULL DEFAULT 0,
    needs_power      INTEGER NOT NULL DEFAULT 0,
    notes            TEXT    NOT NULL DEFAULT '',
    status           TEXT    NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','approved','declined')),
    table_assignment TEXT    NOT NULL DEFAULT '',
    created_at       TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at       TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_display_requests_event ON event_display_requests(event_id);
