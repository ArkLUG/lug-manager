-- Add year to perk_levels so tiers can vary by year.
-- Existing rows default to current year (2026). Historical tiers are frozen.
ALTER TABLE perk_levels ADD COLUMN year INTEGER NOT NULL DEFAULT 2026;
