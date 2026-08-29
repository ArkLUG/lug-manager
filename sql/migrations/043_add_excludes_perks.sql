-- Lets an event or meeting (e.g. a social/party) be excluded from perk-tier
-- meeting/event attendance counts while still showing up in general
-- attendance history - the two are tracked completely separately, so this
-- column only affects count_member_by_year(), nothing else reads it.

ALTER TABLE lug_events ADD COLUMN excludes_perks INTEGER NOT NULL DEFAULT 0;

ALTER TABLE meetings   ADD COLUMN excludes_perks INTEGER NOT NULL DEFAULT 0;
