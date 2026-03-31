-- Migration: Add Discord lead role ID to chapters
-- Version: 005

ALTER TABLE chapters ADD COLUMN discord_lead_role_id TEXT;
