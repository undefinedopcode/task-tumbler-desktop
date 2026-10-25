-- Sync links survive deleting the task here: task_id goes NULL instead of
-- the link disappearing, so the next pull knows not to bring it back.
-- done_pushed: we told the service it's done; don't tell it again.
CREATE TABLE synced_items_new (
    id                  TEXT PRIMARY KEY,
    integration_id      TEXT NOT NULL REFERENCES integrations(id) ON DELETE CASCADE,
    external_id         TEXT NOT NULL,
    external_updated_at INTEGER,
    task_id             TEXT REFERENCES tasks(id) ON DELETE SET NULL,
    data_hash           TEXT,
    last_synced_at      INTEGER NOT NULL,
    sync_status         TEXT NOT NULL DEFAULT 'synced'
                        CHECK (sync_status IN ('synced', 'missing_external', 'done_pushed')),
    created_at          INTEGER NOT NULL,
    UNIQUE (integration_id, external_id)
);
INSERT INTO synced_items_new
SELECT id, integration_id, external_id, external_updated_at, task_id, data_hash, last_synced_at,
       CASE WHEN sync_status IN ('synced', 'missing_external', 'done_pushed') THEN sync_status ELSE 'synced' END,
       created_at
FROM synced_items;
DROP TABLE synced_items;
ALTER TABLE synced_items_new RENAME TO synced_items;
CREATE INDEX idx_synced_items_task ON synced_items(task_id);
CREATE INDEX idx_synced_events_event ON synced_events(event_id);
