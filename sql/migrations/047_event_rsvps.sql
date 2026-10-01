-- RSVPs for events, separate from day-of check-in (attendance). An RSVP is
-- 'going' while the event has room (lug_events.max_attendees, 0 = unlimited)
-- and 'waitlist' otherwise; cancelling a 'going' RSVP promotes the oldest
-- waitlisted one.
CREATE TABLE IF NOT EXISTS event_rsvps (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    member_id   INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    status      TEXT    NOT NULL CHECK(status IN ('going','waitlist')),
    created_at  TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%f','now')),
    UNIQUE(event_id, member_id)
);
CREATE INDEX IF NOT EXISTS idx_event_rsvps_event ON event_rsvps(event_id, status, created_at);
