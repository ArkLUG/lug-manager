-- Where LUG inventory lives: storage units, trailers, members' garages...
-- Each location can have a member who looks after it. Stock is per item and
-- location (an item can be split across several); loans remember where they
-- came from so returns go back there.
CREATE TABLE IF NOT EXISTS storage_locations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT    NOT NULL,
    kind        TEXT    NOT NULL DEFAULT 'storage',   -- storage | trailer | home | venue | other
    address     TEXT    NOT NULL DEFAULT '',
    notes       TEXT    NOT NULL DEFAULT '',
    keeper_id   INTEGER REFERENCES members(id) ON DELETE SET NULL,
    archived    INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS inventory_stock (
    item_id     INTEGER NOT NULL REFERENCES inventory_items(id) ON DELETE CASCADE,
    location_id INTEGER NOT NULL REFERENCES storage_locations(id) ON DELETE CASCADE,
    quantity    INTEGER NOT NULL CHECK (quantity > 0),
    PRIMARY KEY (item_id, location_id)
);

ALTER TABLE inventory_loans ADD COLUMN from_location_id INTEGER REFERENCES storage_locations(id) ON DELETE SET NULL;

-- The old free-text "kept at" becomes real locations, holding what isn't out on loan.
INSERT INTO storage_locations (name, kind)
    SELECT DISTINCT TRIM(location), 'storage' FROM inventory_items WHERE TRIM(location) <> '';
INSERT INTO inventory_stock (item_id, location_id, quantity)
    SELECT i.id, s.id,
           i.quantity - COALESCE((SELECT SUM(l.quantity) FROM inventory_loans l WHERE l.item_id = i.id AND l.returned_at IS NULL), 0)
    FROM inventory_items i JOIN storage_locations s ON s.name = TRIM(i.location)
    WHERE i.quantity - COALESCE((SELECT SUM(l.quantity) FROM inventory_loans l WHERE l.item_id = i.id AND l.returned_at IS NULL), 0) > 0;
-- Open loans remember where the item was kept, so returns go back there.
UPDATE inventory_loans SET from_location_id = (
    SELECT s.id FROM storage_locations s JOIN inventory_items i ON s.name = TRIM(i.location)
    WHERE i.id = inventory_loans.item_id)
WHERE returned_at IS NULL;
