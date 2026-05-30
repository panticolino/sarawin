-- SARA Libre — Schema v1
-- Ejecutado automáticamente en el primer arranque

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ═══════════════════════════════════════════════════════
-- Programaciones principales (bloques musicales)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS schedules (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now', 'localtime')),
    updated_at  TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS schedule_elements (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    schedule_id   TEXT NOT NULL REFERENCES schedules(id) ON DELETE CASCADE,
    position      INTEGER NOT NULL,
    type          TEXT NOT NULL CHECK(type IN ('folder','file','time_announce','stream')),
    path          TEXT,
    display_name  TEXT,
    UNIQUE(schedule_id, position)
);

CREATE TABLE IF NOT EXISTS schedule_slots (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    schedule_id   TEXT NOT NULL REFERENCES schedules(id) ON DELETE CASCADE,
    day           TEXT NOT NULL CHECK(day IN (
                    'Lunes','Martes','Miércoles','Jueves',
                    'Viernes','Sábado','Domingo')),
    start_time    TEXT NOT NULL,
    end_time      TEXT NOT NULL,
    UNIQUE(day, start_time)
);

-- ═══════════════════════════════════════════════════════
-- Publicidad / Eventos
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS events (
    id            TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    persistent    INTEGER NOT NULL DEFAULT 1,
    immediate     INTEGER NOT NULL DEFAULT 0,
    priority      INTEGER NOT NULL DEFAULT 5 CHECK(priority BETWEEN 1 AND 10),
    use_ad_announce INTEGER NOT NULL DEFAULT 0,
    max_wait_minutes INTEGER NOT NULL DEFAULT 0,
    valid_from    TEXT,
    valid_until   TEXT,
    created_at    TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS event_elements (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    TEXT NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    type        TEXT NOT NULL CHECK(type IN ('folder','file','time_announce','stream')),
    path        TEXT,
    display_name TEXT,
    valid_from   TEXT,
    valid_until  TEXT,
    duration_ms  INTEGER DEFAULT 0,
    UNIQUE(event_id, position)
);

CREATE TABLE IF NOT EXISTS event_slots (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id      TEXT NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    day           TEXT NOT NULL CHECK(day IN (
                    'Lunes','Martes','Miércoles','Jueves',
                    'Viernes','Sábado','Domingo')),
    trigger_time  TEXT NOT NULL,
    enabled       INTEGER NOT NULL DEFAULT 1
);

-- ═══════════════════════════════════════════════════════
-- Instant Play (Asistente en Vivo)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS instant_play_presets (
    id      TEXT PRIMARY KEY,
    name    TEXT NOT NULL,
    owner   TEXT NOT NULL DEFAULT 'default'
);

CREATE TABLE IF NOT EXISTS instant_play_slots (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    preset_id    TEXT NOT NULL REFERENCES instant_play_presets(id) ON DELETE CASCADE,
    slot_number  INTEGER NOT NULL CHECK(slot_number BETWEEN 0 AND 11),
    file_path    TEXT,
    display_name TEXT,
    UNIQUE(preset_id, slot_number)
);

-- ═══════════════════════════════════════════════════════
-- Listas guardadas (uso manual)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS saved_lists (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS saved_list_tracks (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    list_id      TEXT NOT NULL REFERENCES saved_lists(id) ON DELETE CASCADE,
    position     INTEGER NOT NULL,
    file_path    TEXT NOT NULL,
    display_name TEXT,
    UNIQUE(list_id, position)
);

-- ═══════════════════════════════════════════════════════
-- Historial de reproducción (anti-repetición + auditoría)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS play_history (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path   TEXT NOT NULL,
    display_name TEXT,
    played_at   TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    finished_at TEXT,
    source      TEXT,
    duration_ms INTEGER,
    completed   INTEGER NOT NULL DEFAULT 1,
    pipeline    TEXT DEFAULT 'main'
);

CREATE INDEX IF NOT EXISTS idx_play_history_file
    ON play_history(file_path, played_at);
CREATE INDEX IF NOT EXISTS idx_play_history_time
    ON play_history(played_at);

-- ═══════════════════════════════════════════════════════
-- Eventos persistentes pendientes
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS pending_events (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id       TEXT NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    scheduled_for  TEXT NOT NULL,
    reason         TEXT,
    created_at     TEXT DEFAULT (datetime('now', 'localtime'))
);

-- Eventos que no se reprodujeron por vencimiento de tiempo
CREATE TABLE IF NOT EXISTS expired_events (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id       TEXT NOT NULL,
    event_name     TEXT NOT NULL,
    scheduled_time TEXT NOT NULL,
    expired_at     TEXT DEFAULT (datetime('now', 'localtime')),
    reason         TEXT DEFAULT 'timeout'
);

-- Presets de streaming (compartidos entre Explorador y Eventos)
CREATE TABLE IF NOT EXISTS stream_presets (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT NOT NULL,
    url        TEXT NOT NULL UNIQUE,
    created_at TEXT DEFAULT (datetime('now', 'localtime'))
);

-- ═══════════════════════════════════════════════════════
-- Configuración clave-valor (respaldo, lo principal va en TOML)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS config (
    key   TEXT PRIMARY KEY,
    value TEXT
);

-- ═══════════════════════════════════════════════════════
-- Monitores de auditoría (Música Nacional, etc.)
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS audit_monitors (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    created_at  TEXT DEFAULT (datetime('now', 'localtime'))
);

CREATE TABLE IF NOT EXISTS audit_monitor_folders (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    monitor_id  TEXT NOT NULL REFERENCES audit_monitors(id) ON DELETE CASCADE,
    folder_path TEXT NOT NULL,
    UNIQUE(monitor_id, folder_path)
);

-- ═══════════════════════════════════════════════════════
-- Pisadores individuales por pista
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS pisador_assignments (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path   TEXT NOT NULL UNIQUE,
    pisador_type TEXT NOT NULL DEFAULT 'specific',  -- 'specific', 'random', 'time'
    pisador_path TEXT,  -- ruta del pisador (NULL si random o time)
    created_at  TEXT DEFAULT (datetime('now', 'localtime'))
);

-- ═══════════════════════════════════════════════════════
-- Metadatos del esquema
-- ═══════════════════════════════════════════════════════

CREATE TABLE IF NOT EXISTS schema_version (
    version     INTEGER PRIMARY KEY,
    applied_at  TEXT DEFAULT (datetime('now', 'localtime'))
);

INSERT OR IGNORE INTO schema_version (version) VALUES (10);

-- Registro de eventos emitidos (para recuperación tras reinicio)
CREATE TABLE IF NOT EXISTS played_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    TEXT NOT NULL,
    slot_time   TEXT NOT NULL,
    play_date   TEXT NOT NULL DEFAULT (date('now', 'localtime')),
    played_at   TEXT DEFAULT (datetime('now', 'localtime')),
    UNIQUE(event_id, slot_time, play_date)
);
