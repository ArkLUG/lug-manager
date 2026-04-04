-- Add description field to perk levels
ALTER TABLE perk_levels ADD COLUMN description TEXT NOT NULL DEFAULT '';
