-- Migration: Add chapter support to LUG Manager
-- Version: 002

CREATE TABLE chapters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    discord_announcement_channel_id TEXT NOT NULL,
    created_by INTEGER,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (created_by) REFERENCES members(id) ON DELETE SET NULL
);

-- Add chapter support to members
ALTER TABLE members ADD COLUMN chapter_id INTEGER REFERENCES chapters(id) ON DELETE SET NULL;

-- Add chapter_id to meetings
ALTER TABLE meetings ADD COLUMN chapter_id INTEGER REFERENCES chapters(id) ON DELETE CASCADE;

-- Add chapter_id to lug_events
ALTER TABLE lug_events ADD COLUMN chapter_id INTEGER REFERENCES chapters(id) ON DELETE CASCADE;
