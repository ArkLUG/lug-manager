-- Add minimum FOL status requirement to perk levels
ALTER TABLE perk_levels ADD COLUMN min_fol_status TEXT NOT NULL DEFAULT 'kfol' CHECK(min_fol_status IN ('kfol','tfol','afol'));
