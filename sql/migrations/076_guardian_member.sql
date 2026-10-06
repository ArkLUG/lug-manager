-- A young member's guardian is another member of the club (Aaron, 2026-10-06),
-- not free text. The old guardian_name/phone/email columns stay for guardians
-- typed in before this; they are linked here when exactly one other adult
-- member has that name, and cleared once a member is picked.
ALTER TABLE members ADD COLUMN guardian_member_id INTEGER REFERENCES members(id) ON DELETE SET NULL;

UPDATE members SET guardian_member_id = (
    SELECT g.id FROM members g
     WHERE g.id <> members.id AND COALESCE(g.fol_status,'afol') NOT IN ('kfol','tfol')
       AND (lower(g.display_name) = lower(trim(members.guardian_name))
            OR lower(trim(COALESCE(g.first_name,'') || ' ' || COALESCE(g.last_name,''))) = lower(trim(members.guardian_name))))
 WHERE trim(guardian_name) <> ''
   AND (SELECT COUNT(*) FROM members g
         WHERE g.id <> members.id AND COALESCE(g.fol_status,'afol') NOT IN ('kfol','tfol')
           AND (lower(g.display_name) = lower(trim(members.guardian_name))
                OR lower(trim(COALESCE(g.first_name,'') || ' ' || COALESCE(g.last_name,''))) = lower(trim(members.guardian_name)))) = 1;

CREATE INDEX IF NOT EXISTS idx_members_guardian ON members(guardian_member_id);
