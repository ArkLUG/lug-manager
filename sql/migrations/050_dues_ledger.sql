-- Dues payment history. members.is_paid / paid_until remain the "current
-- status" fields everything else reads; each recorded payment extends
-- paid_until to at least its covers_until.
CREATE TABLE IF NOT EXISTS dues_payments (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id     INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    paid_on       TEXT    NOT NULL,                 -- YYYY-MM-DD
    amount_cents  INTEGER NOT NULL DEFAULT 0,
    method        TEXT    NOT NULL DEFAULT '',      -- cash, card, paypal, ...
    covers_until  TEXT    NOT NULL,                 -- YYYY-MM-DD
    note          TEXT    NOT NULL DEFAULT '',
    recorded_by   INTEGER,                          -- members.id of who entered it
    created_at    TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_dues_payments_member ON dues_payments(member_id, paid_on);

-- paid_until value the member was last reminded about (so each expiry gets
-- at most one reminder DM).
ALTER TABLE members ADD COLUMN dues_reminded_for TEXT NOT NULL DEFAULT '';
