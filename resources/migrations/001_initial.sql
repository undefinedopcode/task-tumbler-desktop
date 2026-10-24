-- Task Tumbler Desktop: initial schema.
--
-- Conventions
--   * Single user: no users table, no user_id columns.
--   * Instants (created_at, scheduled_start, ...) are INTEGER milliseconds since
--     the Unix epoch, UTC.
--   * Wall-clock intents (due dates, recurring times, category windows, all-day
--     events) are local TEXT: 'YYYY-MM-DD' and 'HH:MM'.
--   * Booleans are INTEGER 0/1. JSON is TEXT, checked with json_valid().
--   * Secrets (API keys, OAuth tokens) live in the OS keychain, never here.

CREATE TABLE settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL CHECK (json_valid(value))
);

CREATE TABLE categories (
    id               TEXT PRIMARY KEY,
    name             TEXT NOT NULL UNIQUE,
    schedule         TEXT NOT NULL CHECK (json_valid(schedule)),
    color            TEXT NOT NULL DEFAULT '#6366f1',
    is_default       INTEGER NOT NULL DEFAULT 0,
    show_on_schedule INTEGER NOT NULL DEFAULT 0,
    created_at       INTEGER NOT NULL,
    updated_at       INTEGER NOT NULL
);
-- At most one default category.
CREATE UNIQUE INDEX idx_categories_one_default ON categories(is_default) WHERE is_default = 1;

CREATE TABLE tasks (
    id                    TEXT PRIMARY KEY,
    title                 TEXT NOT NULL,
    description           TEXT,
    status                TEXT NOT NULL DEFAULT 'pending'
                          CHECK (status IN ('pending', 'in_progress', 'completed', 'cancelled')),
    priority              TEXT NOT NULL DEFAULT 'medium'
                          CHECK (priority IN ('urgent', 'high', 'medium', 'low')),
    due_date              TEXT,
    time_estimate_minutes INTEGER NOT NULL DEFAULT 30,
    complexity            INTEGER NOT NULL DEFAULT 3 CHECK (complexity BETWEEN 1 AND 5),
    energy_required       TEXT NOT NULL DEFAULT 'medium'
                          CHECK (energy_required IN ('low', 'medium', 'high', 'peak')),
    recurrence            TEXT CHECK (recurrence IS NULL OR json_valid(recurrence)), -- NULL: not recurring
    scheduled_time        TEXT,                -- HH:MM for recurring / fixed-time tasks
    is_fixed_time         INTEGER NOT NULL DEFAULT 0,
    parent_task_id        TEXT REFERENCES tasks(id) ON DELETE SET NULL,
    category_id           TEXT REFERENCES categories(id) ON DELETE SET NULL,
    tags                  TEXT NOT NULL DEFAULT '[]' CHECK (json_valid(tags)),
    color                 TEXT,
    sort_order            INTEGER,
    created_at            INTEGER NOT NULL,
    updated_at            INTEGER NOT NULL
);
CREATE INDEX idx_tasks_status   ON tasks(status);
CREATE INDEX idx_tasks_due      ON tasks(due_date);
CREATE INDEX idx_tasks_parent   ON tasks(parent_task_id);
CREATE INDEX idx_tasks_category ON tasks(category_id);

CREATE TABLE task_dependencies (
    task_id            TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    depends_on_task_id TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    created_at         INTEGER NOT NULL,
    PRIMARY KEY (task_id, depends_on_task_id),
    CHECK (task_id <> depends_on_task_id)
);
CREATE INDEX idx_task_dependencies_depends_on ON task_dependencies(depends_on_task_id);

CREATE TABLE task_occurrences (
    id              TEXT PRIMARY KEY,
    task_id         TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    occurrence_date TEXT NOT NULL,             -- YYYY-MM-DD
    status          TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'completed', 'skipped')),
    completed_at    INTEGER,
    scheduled_time  TEXT,                      -- HH:MM override; NULL uses the task's
    created_at      INTEGER NOT NULL,
    UNIQUE (task_id, occurrence_date)
);
CREATE INDEX idx_occurrences_date ON task_occurrences(occurrence_date);

CREATE TABLE work_logs (
    id               TEXT PRIMARY KEY,
    task_id          TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    started_at       INTEGER NOT NULL,
    ended_at         INTEGER,
    duration_minutes INTEGER,
    notes            TEXT,
    created_at       INTEGER NOT NULL
);
CREATE INDEX idx_work_logs_task    ON work_logs(task_id);
CREATE INDEX idx_work_logs_started ON work_logs(started_at);
CREATE INDEX idx_work_logs_active  ON work_logs(ended_at) WHERE ended_at IS NULL;

CREATE TABLE schedules (
    id              TEXT PRIMARY KEY,
    task_id         TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    scheduled_start INTEGER NOT NULL,
    scheduled_end   INTEGER NOT NULL,
    actual_start    INTEGER,
    actual_end      INTEGER,
    completed       INTEGER NOT NULL DEFAULT 0,
    bumped_count    INTEGER NOT NULL DEFAULT 0,
    notes           TEXT,
    work_log_id     TEXT REFERENCES work_logs(id) ON DELETE SET NULL,
    created_at      INTEGER NOT NULL,
    CHECK (scheduled_end >= scheduled_start)
);
CREATE INDEX idx_schedules_start ON schedules(scheduled_start);
CREATE INDEX idx_schedules_task  ON schedules(task_id);

CREATE TABLE events (
    id              TEXT PRIMARY KEY,
    title           TEXT NOT NULL,
    description     TEXT,
    is_all_day      INTEGER NOT NULL DEFAULT 0,
    start_date      TEXT,                      -- all-day: local YYYY-MM-DD
    end_date        TEXT,
    start_at        INTEGER,                   -- timed: UTC ms
    end_at          INTEGER,
    timezone        TEXT,                      -- zone the event was entered in
    blocking_mode   TEXT NOT NULL DEFAULT 'none'
                    CHECK (blocking_mode IN ('none', 'block_all', 'recurring_only', 'tags_only')),
    allowed_tags    TEXT NOT NULL DEFAULT '[]' CHECK (json_valid(allowed_tags)),
    color           TEXT NOT NULL DEFAULT '#6366f1',
    location        TEXT,
    latitude        REAL,
    longitude       REAL,
    is_travel_event INTEGER NOT NULL DEFAULT 0,
    start_location  TEXT,
    end_location    TEXT,
    start_latitude  REAL,
    start_longitude REAL,
    end_latitude    REAL,
    end_longitude   REAL,
    transport_type  TEXT,
    flight_number   TEXT,
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL,
    CHECK ((is_all_day = 1 AND start_date IS NOT NULL AND end_date IS NOT NULL)
        OR (is_all_day = 0 AND start_at IS NOT NULL AND end_at IS NOT NULL))
);
CREATE INDEX idx_events_start_at   ON events(start_at);
CREATE INDEX idx_events_start_date ON events(start_date);

CREATE TABLE energy_logs (
    id           TEXT PRIMARY KEY,
    logged_at    INTEGER NOT NULL,
    energy_level INTEGER NOT NULL CHECK (energy_level BETWEEN 1 AND 10),
    notes        TEXT
);
CREATE INDEX idx_energy_logs_at ON energy_logs(logged_at);

CREATE TABLE health_logs (
    id              TEXT PRIMARY KEY,
    log_date        TEXT NOT NULL UNIQUE,      -- YYYY-MM-DD
    sleep_score     INTEGER CHECK (sleep_score BETWEEN 0 AND 100),
    readiness_score INTEGER CHECK (readiness_score BETWEEN 0 AND 100),
    hours_slept     REAL,
    notes           TEXT,
    source          TEXT NOT NULL DEFAULT 'manual',
    created_at      INTEGER NOT NULL,
    updated_at      INTEGER NOT NULL
);

CREATE TABLE health_summaries (
    id           TEXT PRIMARY KEY,
    summary_date TEXT NOT NULL UNIQUE,
    summary_text TEXT NOT NULL,
    data_context TEXT CHECK (data_context IS NULL OR json_valid(data_context)),
    created_at   INTEGER NOT NULL
);

CREATE TABLE task_history (
    id      TEXT PRIMARY KEY,
    task_id TEXT REFERENCES tasks(id) ON DELETE SET NULL,
    action  TEXT NOT NULL,                     -- created, started, completed, bumped, cancelled
    at      INTEGER NOT NULL,
    details TEXT CHECK (details IS NULL OR json_valid(details))
);
CREATE INDEX idx_task_history_task ON task_history(task_id);
CREATE INDEX idx_task_history_at   ON task_history(at);

CREATE TABLE ai_logs (
    id              TEXT PRIMARY KEY,
    action_type     TEXT NOT NULL,
    provider        TEXT,                      -- anthropic / ollama / ...; NULL for imported rows
    model           TEXT,
    request_context TEXT CHECK (request_context IS NULL OR json_valid(request_context)),
    response        TEXT CHECK (response IS NULL OR json_valid(response)),
    user_feedback   TEXT,                      -- accepted / modified / rejected
    created_at      INTEGER NOT NULL
);

CREATE TABLE notes (
    id         TEXT PRIMARY KEY,
    title      TEXT NOT NULL,
    content    TEXT NOT NULL DEFAULT '',
    task_id    TEXT REFERENCES tasks(id) ON DELETE SET NULL,
    tags       TEXT NOT NULL DEFAULT '[]' CHECK (json_valid(tags)),
    color      TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);
CREATE INDEX idx_notes_task ON notes(task_id);

CREATE VIRTUAL TABLE notes_fts USING fts5(title, content, content = 'notes', content_rowid = 'rowid');
CREATE TRIGGER notes_ai AFTER INSERT ON notes BEGIN
    INSERT INTO notes_fts(rowid, title, content) VALUES (new.rowid, new.title, new.content);
END;
CREATE TRIGGER notes_ad AFTER DELETE ON notes BEGIN
    INSERT INTO notes_fts(notes_fts, rowid, title, content) VALUES ('delete', old.rowid, old.title, old.content);
END;
CREATE TRIGGER notes_au AFTER UPDATE ON notes BEGIN
    INSERT INTO notes_fts(notes_fts, rowid, title, content) VALUES ('delete', old.rowid, old.title, old.content);
    INSERT INTO notes_fts(rowid, title, content) VALUES (new.rowid, new.title, new.content);
END;

CREATE TABLE integrations (
    id           TEXT PRIMARY KEY,
    provider     TEXT NOT NULL UNIQUE,         -- trello, google-tasks, google-calendar, ...
    config       TEXT NOT NULL DEFAULT '{}' CHECK (json_valid(config)),
    last_sync_at INTEGER,
    sync_status  TEXT NOT NULL DEFAULT 'idle', -- idle, syncing, error, needs_auth
    sync_error   TEXT,
    created_at   INTEGER NOT NULL,
    updated_at   INTEGER NOT NULL
);

CREATE TABLE synced_items (
    id                  TEXT PRIMARY KEY,
    integration_id      TEXT NOT NULL REFERENCES integrations(id) ON DELETE CASCADE,
    external_id         TEXT NOT NULL,
    external_updated_at INTEGER,
    task_id             TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    data_hash           TEXT,
    last_synced_at      INTEGER NOT NULL,
    sync_status         TEXT NOT NULL DEFAULT 'synced',
    created_at          INTEGER NOT NULL,
    UNIQUE (integration_id, external_id)
);

CREATE TABLE synced_events (
    id                   TEXT PRIMARY KEY,
    integration_id       TEXT NOT NULL REFERENCES integrations(id) ON DELETE CASCADE,
    external_calendar_id TEXT NOT NULL,
    external_event_id    TEXT NOT NULL,
    external_updated_at  INTEGER,
    event_id             TEXT NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    data_hash            TEXT,
    last_synced_at       INTEGER NOT NULL,
    sync_status          TEXT NOT NULL DEFAULT 'synced' CHECK (sync_status IN ('synced', 'missing_external')),
    created_at           INTEGER NOT NULL,
    UNIQUE (integration_id, external_event_id)
);

-- Things to open when a task starts: the doc, the project, a playlist.
CREATE TABLE task_actions (
    id         TEXT PRIMARY KEY,
    task_id    TEXT NOT NULL REFERENCES tasks(id) ON DELETE CASCADE,
    kind       TEXT NOT NULL CHECK (kind IN ('url', 'file', 'app', 'command', 'dbus')),
    target     TEXT NOT NULL,                  -- URL / path / desktop-file id / argv JSON
    run_on     TEXT NOT NULL DEFAULT 'start' CHECK (run_on IN ('start', 'manual')),
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
);
CREATE INDEX idx_task_actions_task ON task_actions(task_id);
