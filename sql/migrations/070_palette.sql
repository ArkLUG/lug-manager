-- Each member's colour theme (static/palettes.css); '' = the LUG's default
-- (lug_settings.default_palette, itself 'classic' when unset).
ALTER TABLE members ADD COLUMN palette TEXT NOT NULL DEFAULT '';
