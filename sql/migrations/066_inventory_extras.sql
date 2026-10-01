-- Inventory extras: a photo and condition per item, and a pack list per event.
ALTER TABLE inventory_items ADD COLUMN photo_file TEXT NOT NULL DEFAULT '';
ALTER TABLE inventory_items ADD COLUMN condition TEXT NOT NULL DEFAULT 'good';   -- good | worn | needs_repair | broken
ALTER TABLE inventory_items ADD COLUMN condition_note TEXT NOT NULL DEFAULT '';

CREATE TABLE IF NOT EXISTS inventory_pack (
    event_id  INTEGER NOT NULL REFERENCES lug_events(id) ON DELETE CASCADE,
    item_id   INTEGER NOT NULL REFERENCES inventory_items(id) ON DELETE CASCADE,
    quantity  INTEGER NOT NULL CHECK (quantity > 0),
    packed    INTEGER NOT NULL DEFAULT 0,
    note      TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (event_id, item_id)
);
