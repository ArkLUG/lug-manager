-- What each role may do beyond being a member (Settings > Roles and
-- permissions). Admin can always do everything and isn't listed. A permission
-- with no row is denied. The rows below keep what moderators and chapter
-- leads could already do; everything else starts admin-only.
CREATE TABLE IF NOT EXISTS role_permissions (
    role       TEXT NOT NULL CHECK (role IN ('moderator','chapter_lead','member')),
    permission TEXT NOT NULL,
    PRIMARY KEY (role, permission)
);
INSERT OR IGNORE INTO role_permissions(role, permission) VALUES
    ('moderator',    'members.view_private'),
    ('moderator',    'members.edit'),
    ('moderator',    'members.export'),
    ('moderator',    'discord.matches'),
    ('moderator',    'dues.record'),
    ('moderator',    'inventory.manage'),
    ('chapter_lead', 'members.view_private'),
    ('chapter_lead', 'members.edit'),
    ('chapter_lead', 'members.export'),
    ('chapter_lead', 'discord.matches'),
    ('chapter_lead', 'dues.record'),
    ('chapter_lead', 'inventory.manage');
