-- TRAK Connect 3.2.0
-- Runtime schema is created automatically by api/config.php.
-- Keep the actual .sqlite database outside the public web root.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS devices (
    trak_id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL,
    last_seen TEXT,
    firmware_version TEXT,
    network TEXT,
    signal_percent INTEGER
);

CREATE TABLE IF NOT EXISTS positions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    trak_id TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    received_at TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    altitude REAL NOT NULL DEFAULT 0,
    speed_kmh REAL NOT NULL DEFAULT 0,
    bearing REAL NOT NULL DEFAULT 0,
    network TEXT,
    signal_percent INTEGER,
    has_fix INTEGER NOT NULL DEFAULT 1,
    firmware_version TEXT,
    motion_mode TEXT,
    motion_return_seconds INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (trak_id) REFERENCES devices(trak_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_positions_trak_received ON positions(trak_id, received_at DESC);
CREATE INDEX IF NOT EXISTS idx_positions_trak_timestamp ON positions(trak_id, timestamp DESC);

CREATE TABLE IF NOT EXISTS device_settings (
    trak_id TEXT PRIMARY KEY,
    trackserver_url TEXT,
    active_interval INTEGER NOT NULL DEFAULT 10,
    idle_interval INTEGER NOT NULL DEFAULT 60,
    motion_sensitivity INTEGER NOT NULL DEFAULT 3,
    wifi1_ssid TEXT NOT NULL DEFAULT '',
    wifi1_password TEXT NOT NULL DEFAULT '',
    wifi2_ssid TEXT NOT NULL DEFAULT '',
    wifi2_password TEXT NOT NULL DEFAULT '',
    wifi3_ssid TEXT NOT NULL DEFAULT '',
    wifi3_password TEXT NOT NULL DEFAULT '',
    config_version INTEGER NOT NULL DEFAULT 1,
    updated_at TEXT NOT NULL
);
