-- Migration: LUG-wide settings table
-- Version: 003

CREATE TABLE IF NOT EXISTS lug_settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL DEFAULT ''
);
