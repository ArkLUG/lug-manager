-- Audit log for tracking all user actions
CREATE TABLE IF NOT EXISTS audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    actor_id INTEGER NOT NULL DEFAULT 0,
    actor_name TEXT NOT NULL DEFAULT '',
    action TEXT NOT NULL,
    entity_type TEXT NOT NULL DEFAULT '',
    entity_id INTEGER NOT NULL DEFAULT 0,
    entity_name TEXT NOT NULL DEFAULT '',
    details TEXT NOT NULL DEFAULT '',
    ip_address TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX idx_audit_log_created ON audit_log(created_at DESC);
CREATE INDEX idx_audit_log_actor ON audit_log(actor_id);
CREATE INDEX idx_audit_log_entity ON audit_log(entity_type, entity_id);
