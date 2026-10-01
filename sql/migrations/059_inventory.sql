-- LUG-owned stuff (tables, baseplates, display cases, banners...) and who has it.
CREATE TABLE IF NOT EXISTS inventory_items (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,
    category    TEXT    NOT NULL DEFAULT '',
    quantity    INTEGER NOT NULL DEFAULT 1,
    location    TEXT    NOT NULL DEFAULT '',   -- where it lives when not checked out
    notes       TEXT    NOT NULL DEFAULT '',
    archived    INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS inventory_loans (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    item_id        INTEGER NOT NULL REFERENCES inventory_items(id) ON DELETE CASCADE,
    member_id      INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    quantity       INTEGER NOT NULL DEFAULT 1,
    due_on         TEXT    NOT NULL DEFAULT '',  -- YYYY-MM-DD or ''
    notes          TEXT    NOT NULL DEFAULT '',
    checked_out_at TEXT    NOT NULL DEFAULT (datetime('now')),
    checked_out_by INTEGER REFERENCES members(id) ON DELETE SET NULL,
    returned_at    TEXT
);
CREATE INDEX IF NOT EXISTS idx_inventory_loans_open ON inventory_loans(item_id) WHERE returned_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_inventory_loans_member ON inventory_loans(member_id);
