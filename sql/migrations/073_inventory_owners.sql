-- Who owns an inventory item or a storage location: the LUG (NULL, as before)
-- or a member who lends it (e.g. their trailer, their tables kept in the LUG's
-- unit). owner_name keeps the name if the member is deleted, so the item shows
-- "owner left" instead of silently becoming the LUG's.
ALTER TABLE inventory_items   ADD COLUMN owner_member_id INTEGER REFERENCES members(id) ON DELETE SET NULL;
ALTER TABLE inventory_items   ADD COLUMN owner_name      TEXT NOT NULL DEFAULT '';
ALTER TABLE storage_locations ADD COLUMN owner_member_id INTEGER REFERENCES members(id) ON DELETE SET NULL;
ALTER TABLE storage_locations ADD COLUMN owner_name      TEXT NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_inventory_items_owner ON inventory_items(owner_member_id);
