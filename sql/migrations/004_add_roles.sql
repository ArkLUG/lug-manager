-- Migration: Discord role mappings and chapter member roles
-- Version: 004

-- Maps Discord role IDs to LUG app roles
CREATE TABLE discord_role_mappings (
    discord_role_id   TEXT PRIMARY KEY,
    discord_role_name TEXT NOT NULL DEFAULT '',
    lug_role          TEXT NOT NULL  -- "admin" | "chapter_lead" | "member"
);

-- Chapter-specific member roles (who leads/manages each chapter)
CREATE TABLE chapter_members (
    member_id    INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    chapter_id   INTEGER NOT NULL REFERENCES chapters(id) ON DELETE CASCADE,
    chapter_role TEXT NOT NULL DEFAULT 'member',  -- "lead" | "event_manager" | "member"
    granted_by   INTEGER REFERENCES members(id) ON DELETE SET NULL,
    granted_at   DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (member_id, chapter_id)
);
