-- Records where a member's LUG role came from, so the Discord role sync
-- (login + the 6-hourly guild sync) can tell a role it granted from one an
-- admin set by hand:
--   'discord' - follows the Discord role mapping; losing the mapped Discord
--               role demotes back to 'member'.
--   'manual'  - set by an admin (or the bootstrap admin); sync may raise it
--               to a higher mapped role but never lowers it.
-- Previously sync overwrote every guild member's role with the mapped role
-- or 'member', wiping manually granted moderator/chapter_lead/admin roles.
-- Existing elevated members are marked 'manual' so nobody is demoted by this
-- change; plain members follow Discord.

ALTER TABLE members ADD COLUMN role_source TEXT NOT NULL DEFAULT 'discord';

UPDATE members SET role_source = 'manual' WHERE role <> 'member';
