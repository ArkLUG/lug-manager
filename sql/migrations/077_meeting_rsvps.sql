-- Who's coming to a meeting: "I'm going" / "Can't make it" on the meeting
-- page and on its Discord announcement (Aaron, 2026-10-06). Unlike events
-- there's no capacity or waitlist; it's a headcount, and people who are
-- going get the meeting reminder DM.
CREATE TABLE IF NOT EXISTS meeting_rsvps (
    meeting_id INTEGER NOT NULL REFERENCES meetings(id) ON DELETE CASCADE,
    member_id  INTEGER NOT NULL REFERENCES members(id)  ON DELETE CASCADE,
    going      INTEGER NOT NULL CHECK (going IN (0, 1)),
    updated_at TEXT    NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (meeting_id, member_id)
);
CREATE INDEX IF NOT EXISTS idx_meeting_rsvps_member ON meeting_rsvps(member_id);
