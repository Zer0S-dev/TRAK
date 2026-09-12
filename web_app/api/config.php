<?php

declare(strict_types=1);

const TRACKSERVER_OSMAND_URL = 'https://surlereservoir.fr/trackserver/surledoud/eb5a96a7/?lat={0}&lon={1}&timestamp={2}&altitude={4}&speed={5}&bearing={6}';

/*
 * SQLite is deliberately stored outside the web application directory.
 * TRAK_CONNECT_DB may be set by the hosting environment to an absolute path.
 * Default: ../private/trak.sqlite relative to web_app/api/.
 */
const TRAK_CONNECT_DB = __DIR__ . '/../../private/trak.sqlite';
const TRAK_LEGACY_STORAGE_DIR = __DIR__ . '/storage';
const TRAK_LEGACY_POSITIONS_FILE = TRAK_LEGACY_STORAGE_DIR . '/positions.json';
const TRAK_LEGACY_SETTINGS_FILE = TRAK_LEGACY_STORAGE_DIR . '/settings.json';

const TRAK_DEFAULT_USER = 'admin';
const TRAK_DEFAULT_PASSWORD_HASH = '$2y$12$28IUwE4iobC3xgsrTj8aG.XBJRDrydy9afa00NGA5Dop8uM5gpony';

function startTrakSession(): void
{
    if (session_status() === PHP_SESSION_ACTIVE) return;
    session_name('trak_session');
    session_set_cookie_params([
        'lifetime' => 0,
        'path' => '/trak/',
        'secure' => true,
        'httponly' => true,
        'samesite' => 'Strict',
    ]);
    session_start();
}

function dashboardUser(): string { return trim((string) (getenv('TRAK_CONNECT_USER') ?: TRAK_DEFAULT_USER)); }
function dashboardPasswordHash(): string { return trim((string) (getenv('TRAK_CONNECT_PASSWORD_HASH') ?: TRAK_DEFAULT_PASSWORD_HASH)); }

function requireDashboardAuth(): void
{
    startTrakSession();
    if (empty($_SESSION['trak_authenticated']) || $_SESSION['trak_authenticated'] !== true) {
        jsonResponse(['ok' => false, 'error' => 'unauthorized'], 401);
    }
}

function csrfToken(): string
{
    startTrakSession();
    if (empty($_SESSION['trak_csrf'])) $_SESSION['trak_csrf'] = bin2hex(random_bytes(32));
    return (string) $_SESSION['trak_csrf'];
}

function requireCsrf(): void
{
    $provided = (string) ($_SERVER['HTTP_X_CSRF_TOKEN'] ?? '');
    if ($provided === '' || !hash_equals(csrfToken(), $provided)) jsonResponse(['ok' => false, 'error' => 'csrf_failed'], 403);
}

function jsonResponse(array $payload, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    echo json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function defaultSettings(): array
{
    return [
        'trackserver_url' => TRACKSERVER_OSMAND_URL,
        'active_interval' => 10,
        'idle_interval' => 60,
        'motion_sensitivity' => 3,
        'wifi_profiles' => [
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
        ],
    ];
}

function sqlite(): PDO
{
    static $pdo = null;
    if ($pdo instanceof PDO) return $pdo;

    if (!class_exists('PDO')) jsonResponse(['ok' => false, 'error' => 'pdo_unavailable'], 503);
    if (!in_array('sqlite', PDO::getAvailableDrivers(), true)) jsonResponse(['ok' => false, 'error' => 'pdo_sqlite_unavailable'], 503);

    $dbPath = trim((string) (getenv('TRAK_CONNECT_DB') ?: TRAK_CONNECT_DB));
    $dbDir = dirname($dbPath);
    if (!is_dir($dbDir) && !@mkdir($dbDir, 0700, true) && !is_dir($dbDir)) {
        jsonResponse(['ok' => false, 'error' => 'database_dir_unavailable'], 503);
    }

    try {
        $pdo = new PDO('sqlite:' . $dbPath, null, null, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]);
        $pdo->exec('PRAGMA foreign_keys = ON');
        $pdo->exec('PRAGMA busy_timeout = 5000');
        $pdo->exec('PRAGMA journal_mode = WAL');
        initialiseSqlite($pdo);
        migrateLegacyJson($pdo);
        return $pdo;
    } catch (Throwable $e) {
        jsonResponse(['ok' => false, 'error' => 'database_unavailable'], 503);
    }
}

function initialiseSqlite(PDO $pdo): void
{
    $pdo->exec(<<<'SQL'
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
SQL);
}

function migrateLegacyJson(PDO $pdo): void
{
    static $done = false;
    if ($done) return;
    $done = true;

    $count = (int) $pdo->query('SELECT COUNT(*) FROM positions')->fetchColumn();
    $settingsCount = (int) $pdo->query('SELECT COUNT(*) FROM device_settings')->fetchColumn();

    if ($count === 0 && is_file(TRAK_LEGACY_POSITIONS_FILE)) {
        $raw = @file_get_contents(TRAK_LEGACY_POSITIONS_FILE);
        $legacy = json_decode($raw ?: '', true);
        if (is_array($legacy)) {
            $pdo->beginTransaction();
            try {
                $insertDevice = $pdo->prepare('INSERT OR IGNORE INTO devices (trak_id, created_at) VALUES (:id, :created_at)');
                $insertPosition = $pdo->prepare(<<<'SQL'
INSERT INTO positions (
    trak_id, timestamp, received_at, latitude, longitude, altitude, speed_kmh, bearing,
    network, signal_percent, has_fix, firmware_version, motion_mode, motion_return_seconds
) VALUES (
    :trak_id, :timestamp, :received_at, :latitude, :longitude, :altitude, :speed_kmh, :bearing,
    :network, :signal_percent, :has_fix, :firmware_version, :motion_mode, :motion_return_seconds
)
SQL);
                foreach ($legacy as $trakId => $position) {
                    if (!is_array($position) || !is_string($trakId) || $trakId === '') continue;
                    $insertDevice->execute([':id' => $trakId, ':created_at' => gmdate('Y-m-d\\TH:i:s\\Z')]);
                    $insertPosition->execute([
                        ':trak_id' => $trakId,
                        ':timestamp' => (string) ($position['timestamp'] ?? gmdate('Y-m-d\\TH:i:s\\Z')),
                        ':received_at' => (string) ($position['received_at'] ?? gmdate('Y-m-d\\TH:i:s\\Z')),
                        ':latitude' => (float) ($position['latitude'] ?? 0),
                        ':longitude' => (float) ($position['longitude'] ?? 0),
                        ':altitude' => (float) ($position['altitude'] ?? 0),
                        ':speed_kmh' => (float) ($position['speed_kmh'] ?? 0),
                        ':bearing' => (float) ($position['bearing'] ?? 0),
                        ':network' => (string) ($position['network'] ?? ''),
                        ':signal_percent' => isset($position['signal_percent']) ? (int) $position['signal_percent'] : null,
                        ':has_fix' => !empty($position['hasFix']) ? 1 : 0,
                        ':firmware_version' => $position['firmware_version'] ?? null,
                        ':motion_mode' => $position['motionMode'] ?? 'IMMOBILE',
                        ':motion_return_seconds' => (int) ($position['motionReturnSeconds'] ?? 0),
                    ]);
                }
                $pdo->commit();
                @unlink(TRAK_LEGACY_POSITIONS_FILE);
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) $pdo->rollBack();
            }
        }
    }

    if ($settingsCount === 0 && is_file(TRAK_LEGACY_SETTINGS_FILE)) {
        $raw = @file_get_contents(TRAK_LEGACY_SETTINGS_FILE);
        $legacy = json_decode($raw ?: '', true);
        if (is_array($legacy)) {
            $pdo->beginTransaction();
            try {
                $defaults = defaultSettings();
                $settings = array_replace($defaults, $legacy);
                $profiles = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : $defaults['wifi_profiles'];
                for ($i = 0; $i < 3; ++$i) if (!is_array($profiles[$i] ?? null)) $profiles[$i] = $defaults['wifi_profiles'][$i];

                $stmt = $pdo->prepare(<<<'SQL'
INSERT INTO device_settings (
    trak_id, trackserver_url, active_interval, idle_interval, motion_sensitivity,
    wifi1_ssid, wifi1_password, wifi2_ssid, wifi2_password, wifi3_ssid, wifi3_password,
    config_version, updated_at
) VALUES ('__default__', :trackserver_url, :active_interval, :idle_interval, :motion_sensitivity,
          :wifi1_ssid, :wifi1_password, :wifi2_ssid, :wifi2_password, :wifi3_ssid, :wifi3_password,
          1, :updated_at)
SQL);
                $stmt->execute([
                    ':trackserver_url' => (string) ($settings['trackserver_url'] ?? TRACKSERVER_OSMAND_URL),
                    ':active_interval' => (int) ($settings['active_interval'] ?? 10),
                    ':idle_interval' => (int) ($settings['idle_interval'] ?? 60),
                    ':motion_sensitivity' => (int) ($settings['motion_sensitivity'] ?? 3),
                    ':wifi1_ssid' => (string) ($profiles[0]['ssid'] ?? ''), ':wifi1_password' => (string) ($profiles[0]['password'] ?? ''),
                    ':wifi2_ssid' => (string) ($profiles[1]['ssid'] ?? ''), ':wifi2_password' => (string) ($profiles[1]['password'] ?? ''),
                    ':wifi3_ssid' => (string) ($profiles[2]['ssid'] ?? ''), ':wifi3_password' => (string) ($profiles[2]['password'] ?? ''),
                    ':updated_at' => gmdate('Y-m-d\\TH:i:s\\Z'),
                ]);
                $pdo->commit();
                @unlink(TRAK_LEGACY_SETTINGS_FILE);
            } catch (Throwable $e) {
                if ($pdo->inTransaction()) $pdo->rollBack();
            }
        }
    }
}

function normaliseTimestamp(?string $timestamp): string
{
    if ($timestamp === null || trim($timestamp) === '') return gmdate('Y-m-d\\TH:i:s\\Z');
    $time = strtotime($timestamp);
    return $time === false ? gmdate('Y-m-d\\TH:i:s\\Z') : gmdate('Y-m-d\\TH:i:s\\Z', $time);
}

function ensureDevice(string $trakId, ?string $firmwareVersion = null, ?string $network = null, ?int $signalPercent = null): void
{
    $pdo = sqlite();
    $stmt = $pdo->prepare(<<<'SQL'
INSERT INTO devices (trak_id, created_at, last_seen, firmware_version, network, signal_percent)
VALUES (:id, :created_at, :last_seen, :firmware, :network, :signal)
ON CONFLICT(trak_id) DO UPDATE SET
    last_seen = excluded.last_seen,
    firmware_version = COALESCE(excluded.firmware_version, devices.firmware_version),
    network = COALESCE(excluded.network, devices.network),
    signal_percent = COALESCE(excluded.signal_percent, devices.signal_percent)
SQL);
    $now = gmdate('Y-m-d\\TH:i:s\\Z');
    $stmt->execute([
        ':id' => $trakId, ':created_at' => $now, ':last_seen' => $now,
        ':firmware' => $firmwareVersion, ':network' => $network, ':signal' => $signalPercent,
    ]);
}

function storePosition(string $trakId, array $position): array
{
    ensureDevice($trakId, $position['firmware_version'] ?? null, $position['network'] ?? null, $position['signal_percent'] ?? null);
    $pdo = sqlite();
    $stmt = $pdo->prepare(<<<'SQL'
INSERT INTO positions (
    trak_id, timestamp, received_at, latitude, longitude, altitude, speed_kmh, bearing,
    network, signal_percent, has_fix, firmware_version, motion_mode, motion_return_seconds
) VALUES (
    :trak_id, :timestamp, :received_at, :latitude, :longitude, :altitude, :speed_kmh, :bearing,
    :network, :signal_percent, :has_fix, :firmware_version, :motion_mode, :motion_return_seconds
)
SQL);
    $stmt->execute([
        ':trak_id' => $trakId,
        ':timestamp' => (string) $position['timestamp'],
        ':received_at' => (string) $position['received_at'],
        ':latitude' => (float) $position['latitude'],
        ':longitude' => (float) $position['longitude'],
        ':altitude' => (float) $position['altitude'],
        ':speed_kmh' => (float) ($position['speed_kmh'] ?? 0),
        ':bearing' => (float) ($position['bearing'] ?? 0),
        ':network' => $position['network'] ?? null,
        ':signal_percent' => $position['signal_percent'] ?? null,
        ':has_fix' => !empty($position['hasFix']) ? 1 : 0,
        ':firmware_version' => $position['firmware_version'] ?? null,
        ':motion_mode' => $position['motionMode'] ?? 'IMMOBILE',
        ':motion_return_seconds' => (int) ($position['motionReturnSeconds'] ?? 0),
    ]);
    $position['tx_count'] = (int) $pdo->query('SELECT COUNT(*) FROM positions WHERE trak_id = ' . $pdo->quote($trakId))->fetchColumn();
    return $position;
}

function rowToPosition(array $row): array
{
    return [
        'trak_id' => (string) $row['trak_id'],
        'latitude' => (float) $row['latitude'],
        'longitude' => (float) $row['longitude'],
        'altitude' => (float) $row['altitude'],
        'speed_kmh' => (float) $row['speed_kmh'],
        'bearing' => (float) $row['bearing'],
        'timestamp' => $row['timestamp'],
        'received_at' => $row['received_at'],
        'network' => $row['network'],
        'signal_percent' => $row['signal_percent'] === null ? null : (int) $row['signal_percent'],
        'hasFix' => (bool) $row['has_fix'],
        'firmware_version' => $row['firmware_version'],
        'motionMode' => $row['motion_mode'] ?: 'IMMOBILE',
        'motionReturnSeconds' => (int) $row['motion_return_seconds'],
    ];
}

function loadPositions(): array
{
    $pdo = sqlite();
    $stmt = $pdo->query(<<<'SQL'
SELECT p.*
FROM positions p
JOIN (
    SELECT trak_id, MAX(id) AS max_id
    FROM positions
    GROUP BY trak_id
) latest ON latest.max_id = p.id
ORDER BY p.received_at DESC
SQL);
    $out = [];
    foreach ($stmt->fetchAll() as $row) {
        $position = rowToPosition($row);
        $countStmt = $pdo->prepare('SELECT COUNT(*) FROM positions WHERE trak_id = :id');
        $countStmt->execute([':id' => $position['trak_id']]);
        $position['tx_count'] = (int) $countStmt->fetchColumn();
        $out[$position['trak_id']] = $position;
    }
    return $out;
}

function loadSettings(?string $trakId = null): array
{
    $pdo = sqlite();
    $id = $trakId !== null && trim($trakId) !== '' ? trim($trakId) : '__default__';
    $stmt = $pdo->prepare('SELECT * FROM device_settings WHERE trak_id = :id LIMIT 1');
    $stmt->execute([':id' => $id]);
    $row = $stmt->fetch();
    if (!$row) return defaultSettings();

    return [
        'trackserver_url' => (string) ($row['trackserver_url'] ?? TRACKSERVER_OSMAND_URL),
        'active_interval' => (int) ($row['active_interval'] ?? 10),
        'idle_interval' => (int) ($row['idle_interval'] ?? 60),
        'motion_sensitivity' => (int) ($row['motion_sensitivity'] ?? 3),
        'wifi_profiles' => [
            ['ssid' => (string) $row['wifi1_ssid'], 'password' => (string) $row['wifi1_password']],
            ['ssid' => (string) $row['wifi2_ssid'], 'password' => (string) $row['wifi2_password']],
            ['ssid' => (string) $row['wifi3_ssid'], 'password' => (string) $row['wifi3_password']],
        ],
        'config_version' => (int) ($row['config_version'] ?? 1),
    ];
}

function saveSettings(array $settings, ?string $trakId = null): void
{
    $pdo = sqlite();
    $id = $trakId !== null && trim($trakId) !== '' ? trim($trakId) : '__default__';
    $profiles = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : defaultSettings()['wifi_profiles'];
    for ($i = 0; $i < 3; ++$i) if (!is_array($profiles[$i] ?? null)) $profiles[$i] = ['ssid' => '', 'password' => ''];

    $stmt = $pdo->prepare(<<<'SQL'
INSERT INTO device_settings (
    trak_id, trackserver_url, active_interval, idle_interval, motion_sensitivity,
    wifi1_ssid, wifi1_password, wifi2_ssid, wifi2_password, wifi3_ssid, wifi3_password,
    config_version, updated_at
) VALUES (:id, :trackserver_url, :active_interval, :idle_interval, :motion_sensitivity,
          :wifi1_ssid, :wifi1_password, :wifi2_ssid, :wifi2_password, :wifi3_ssid, :wifi3_password,
          :config_version, :updated_at)
ON CONFLICT(trak_id) DO UPDATE SET
    trackserver_url = excluded.trackserver_url,
    active_interval = excluded.active_interval,
    idle_interval = excluded.idle_interval,
    motion_sensitivity = excluded.motion_sensitivity,
    wifi1_ssid = excluded.wifi1_ssid,
    wifi1_password = excluded.wifi1_password,
    wifi2_ssid = excluded.wifi2_ssid,
    wifi2_password = excluded.wifi2_password,
    wifi3_ssid = excluded.wifi3_ssid,
    wifi3_password = excluded.wifi3_password,
    config_version = excluded.config_version,
    updated_at = excluded.updated_at
SQL);
    $current = loadSettings($id);
    $version = max(1, (int) ($current['config_version'] ?? 1) + 1);
    $stmt->execute([
        ':id' => $id,
        ':trackserver_url' => (string) ($settings['trackserver_url'] ?? TRACKSERVER_OSMAND_URL),
        ':active_interval' => (int) ($settings['active_interval'] ?? 10),
        ':idle_interval' => (int) ($settings['idle_interval'] ?? 60),
        ':motion_sensitivity' => (int) ($settings['motion_sensitivity'] ?? 3),
        ':wifi1_ssid' => (string) ($profiles[0]['ssid'] ?? ''), ':wifi1_password' => (string) ($profiles[0]['password'] ?? ''),
        ':wifi2_ssid' => (string) ($profiles[1]['ssid'] ?? ''), ':wifi2_password' => (string) ($profiles[1]['password'] ?? ''),
        ':wifi3_ssid' => (string) ($profiles[2]['ssid'] ?? ''), ':wifi3_password' => (string) ($profiles[2]['password'] ?? ''),
        ':config_version' => $version,
        ':updated_at' => gmdate('Y-m-d\\TH:i:s\\Z'),
    ]);
}

function getTrackserverConfiguredUrl(): string
{
    $settings = loadSettings();
    $url = trim((string) ($settings['trackserver_url'] ?? ''));
    return $url !== '' ? $url : TRACKSERVER_OSMAND_URL;
}

function trackserverUrl(array $position): string
{
    $template = getTrackserverConfiguredUrl();
    if ($template === '') return '';
    $timestamp = strtotime((string) $position['timestamp']);
    if ($timestamp === false) $timestamp = time();
    $values = [
        rawurlencode((string) $position['latitude']), rawurlencode((string) $position['longitude']),
        rawurlencode((string) $timestamp), rawurlencode((string) $position['trak_id']),
        rawurlencode((string) $position['altitude']), rawurlencode((string) ($position['speed_kmh'] ?? 0)),
        rawurlencode((string) ($position['bearing'] ?? 0)),
    ];
    return strtr($template, [
        '{0}' => $values[0], '{1}' => $values[1], '{2}' => $values[2], '{3}' => $values[3],
        '{4}' => $values[4], '{5}' => $values[5], '{6}' => $values[6], '{id}' => $values[3],
        '{lat}' => $values[0], '{lon}' => $values[1], '{timestamp}' => $values[2],
        '{altitude}' => $values[4], '{speed}' => $values[5], '{bearing}' => $values[6],
    ]);
}

function sendToTrackserver(array $position): array
{
    $url = trackserverUrl($position);
    if ($url === '') return ['configured' => false, 'ok' => false, 'status' => 0, 'url' => ''];
    $status = 0;
    $body = '';
    if (function_exists('curl_init')) {
        $ch = curl_init($url);
        curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_FOLLOWLOCATION => false, CURLOPT_CONNECTTIMEOUT => 4, CURLOPT_TIMEOUT => 8, CURLOPT_HTTPGET => true, CURLOPT_USERAGENT => 'TRAK-Connect/3.2']);
        $body = (string) curl_exec($ch);
        $status = (int) curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
    } else {
        $context = stream_context_create(['http' => ['method' => 'GET', 'timeout' => 8, 'ignore_errors' => true, 'follow_location' => 0, 'header' => "User-Agent: TRAK-Connect/3.2\r\n"]]);
        $body = (string) @file_get_contents($url, false, $context);
        if (isset($http_response_header[0]) && preg_match('/\s(\d{3})\s/', $http_response_header[0], $m)) $status = (int) $m[1];
    }
    return ['configured' => true, 'ok' => $status >= 200 && $status < 300, 'status' => $status, 'url' => $url, 'response' => function_exists('mb_substr') ? mb_substr($body, 0, 500) : substr($body, 0, 500)];
}
