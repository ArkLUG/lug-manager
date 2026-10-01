-- LEGO Fan CoLab extras: Community Ambassador history, a yearly to-do list,
-- and photos used on the public About page (whose text lives in lug_settings
-- as about_title / about_markdown).

-- Who was Community Ambassador, and when. ended_on NULL = current.
-- member_name keeps the name if the member record is later deleted.
CREATE TABLE IF NOT EXISTS community_ambassador_terms (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id   INTEGER REFERENCES members(id) ON DELETE SET NULL,
    member_name TEXT    NOT NULL DEFAULT '',
    started_on  TEXT    NOT NULL,             -- YYYY-MM-DD
    ended_on    TEXT,                         -- YYYY-MM-DD, NULL while serving
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_ambassador_terms_member ON community_ambassador_terms(member_id);

-- The ambassador already set becomes the first (open) term.
INSERT INTO community_ambassador_terms (member_id, member_name, started_on)
SELECT m.id, m.display_name, date('now')
FROM lug_settings s JOIN members m ON m.id = CAST(s.value AS INTEGER)
WHERE s.key = 'community_ambassador_id' AND s.value <> '';

-- Things to do once a year; ticked off per year.
CREATE TABLE IF NOT EXISTS fan_colab_tasks (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    title      TEXT    NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at TEXT    NOT NULL DEFAULT (datetime('now'))
);
INSERT INTO fan_colab_tasks (title, sort_order) VALUES
    ('Confirm who is Community Ambassador this year', 1),
    ('Send the year''s activity summary to LEGO Fan CoLab', 2),
    ('Check the group''s details on LEGO Fan CoLab are up to date', 3);

CREATE TABLE IF NOT EXISTS fan_colab_task_done (
    task_id INTEGER NOT NULL REFERENCES fan_colab_tasks(id) ON DELETE CASCADE,
    year    INTEGER NOT NULL,
    done_by INTEGER REFERENCES members(id) ON DELETE SET NULL,
    done_at TEXT    NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (task_id, year)
);

-- Photos placed on the public About page. Only files listed here are served
-- without a login (/about/photos/<file>); other uploads stay members-only.
CREATE TABLE IF NOT EXISTS about_photos (
    file       TEXT PRIMARY KEY,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
