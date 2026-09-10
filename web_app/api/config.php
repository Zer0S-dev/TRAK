<?php

declare(strict_types=1);

const TRAK_API_KEY = 'TRAK-PROTO-2026';
const TRACKSERVER_OSMAND_URL = 'https://surlereservoir.fr/trackserver/surledoud/eb5a96a7/?lat={0}&lon={1}&timestamp={2}&altitude={4}&speed={5}&bearing={6}';
const TRAK_STORAGE_DIR = __DIR__ . '/storage';
const TRAK_STORAGE_FILE = TRAK_STORAGE_DIR . '/positions.json';
const TRAK_SETTINGS_FILE = TRAK_STORAGE_DIR . '/settings.json';

// Prototype defaults. Environment variables can override these values on the server.
const TRAK_DEFAULT_USER = 'admin';
const TRAK_DEFAULT_PASSWORD_HASH = '$2y$12$28IUwE4iobC3xgsrTj8aG.XBJRDrydy9afa00NGA5Dop8uM5gpony';

function startTrakSession(): void
{
    if (session_status() === PHP_SESSION_ACTIVE) return;
    session_name('trak_session');
    session_set_cookie_params(['lifetime' => 0, 'path' => '/trak/', 'secure' => true, 'httponly' => true, 'samesite' => 'Strict']);
    session_start();
}

function dashboardUser(): string { return trim((string) (getenv('TRAK_CONNECT_USER') ?: TRAK_DEFAULT_USER)); }
function dashboardPasswordHash(): string { return trim((string) (getenv('TRAK_CONNECT_PASSWORD_HASH') ?: TRAK_DEFAULT_PASSWORD_HASH)); }

function requireDashboardAuth(): void
{
    startTrakSession();
    if (empty($_SESSION['trak_authenticated']) || $_SESSION['trak_authenticated'] !== true) jsonResponse(['ok' => false, 'error' => 'unauthorized'], 401);
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

function ensureTrakStorage(): void
{
    if (!is_dir(TRAK_STORAGE_DIR)) mkdir(TRAK_STORAGE_DIR, 0775, true);
    if (!is_file(TRAK_STORAGE_FILE)) file_put_contents(TRAK_STORAGE_FILE, "{}", LOCK_EX);
    if (!is_file(TRAK_SETTINGS_FILE)) file_put_contents(TRAK_SETTINGS_FILE, json_encode(['trackserver_url' => TRACKSERVER_OSMAND_URL, 'record_interval' => 15], JSON_UNESCAPED_SLASHES), LOCK_EX);
}

function jsonResponse(array $payload, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    echo json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function requireApiKey(): void
{
    $provided = $_GET['api_key'] ?? '';
    if ($provided === '') {
        $authorization = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
        if (preg_match('/^Bearer\s+(.+)$/i', $authorization, $m)) $provided = trim($m[1]);
    }
    if (!hash_equals(TRAK_API_KEY, (string) $provided)) jsonResponse(['ok' => false, 'error' => 'unauthorized'], 401);
}

function loadPositions(): array
{
    ensureTrakStorage();
    $raw = file_get_contents(TRAK_STORAGE_FILE);
    if ($raw === false || trim($raw) === '') return [];
    $data = json_decode($raw, true);
    return is_array($data) ? $data : [];
}

function savePositions(array $positions): void
{
    ensureTrakStorage();
    $json = json_encode($positions, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) jsonResponse(['ok' => false, 'error' => 'storage_encode_failed'], 500);
    $tmp = TRAK_STORAGE_FILE . '.' . bin2hex(random_bytes(8)) . '.tmp';
    if (file_put_contents($tmp, $json, LOCK_EX) === false || !rename($tmp, TRAK_STORAGE_FILE)) {
        @unlink($tmp);
        jsonResponse(['ok' => false, 'error' => 'storage_write_failed'], 500);
    }
}

function storePosition(string $trakId, array $position): array
{
    ensureTrakStorage();
    $lockPath = TRAK_STORAGE_FILE . '.lock';
    $lock = fopen($lockPath, 'c');
    if ($lock === false || !flock($lock, LOCK_EX)) {
        if (is_resource($lock)) fclose($lock);
        jsonResponse(['ok' => false, 'error' => 'storage_lock_failed'], 503);
    }

    try {
        $positions = loadPositions();
        $position['tx_count'] = (int) ($positions[$trakId]['tx_count'] ?? 0) + 1;
        $positions[$trakId] = $position;
        savePositions($positions);
        return $position;
    } finally {
        flock($lock, LOCK_UN);
        fclose($lock);
    }
}

function loadSettings(): array
{
    ensureTrakStorage();
    $raw = file_get_contents(TRAK_SETTINGS_FILE);
    if ($raw === false || trim($raw) === '') return ['trackserver_url' => TRACKSERVER_OSMAND_URL, 'record_interval' => 15];
    $data = json_decode($raw, true);
    return is_array($data) ? $data : ['trackserver_url' => TRACKSERVER_OSMAND_URL, 'record_interval' => 15];
}

function saveSettings(array $settings): void
{
    ensureTrakStorage();
    $json = json_encode($settings, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) jsonResponse(['ok' => false, 'error' => 'settings_encode_failed'], 500);
    $tmp = TRAK_SETTINGS_FILE . '.' . bin2hex(random_bytes(8)) . '.tmp';
    if (file_put_contents($tmp, $json, LOCK_EX) === false || !rename($tmp, TRAK_SETTINGS_FILE)) {
        @unlink($tmp);
        jsonResponse(['ok' => false, 'error' => 'settings_write_failed'], 500);
    }
}

function getTrackserverConfiguredUrl(): string
{
    $settings = loadSettings();
    $url = trim((string) ($settings['trackserver_url'] ?? ''));
    return $url !== '' ? $url : TRACKSERVER_OSMAND_URL;
}

function normaliseTimestamp(?string $timestamp): string
{
    if ($timestamp === null || trim($timestamp) === '') return gmdate('Y-m-d\TH:i:s\Z');
    $time = strtotime($timestamp);
    return $time === false ? gmdate('Y-m-d\TH:i:s\Z') : gmdate('Y-m-d\TH:i:s\Z', $time);
}

function trackserverUrl(array $position): string
{
    $template = getTrackserverConfiguredUrl();
    if ($template === '') return '';

    // OsmAnd/Trackserver expects {2} as Unix epoch time, not an ISO-8601 string.
    $timestamp = strtotime((string) $position['timestamp']);
    if ($timestamp === false) $timestamp = time();

    $values = [
        rawurlencode((string) $position['latitude']),
        rawurlencode((string) $position['longitude']),
        rawurlencode((string) $timestamp),
        rawurlencode((string) $position['trak_id']),
        rawurlencode((string) $position['altitude']),
        rawurlencode((string) ($position['speed_kmh'] ?? 0)),
        rawurlencode((string) ($position['bearing'] ?? 0)),
    ];

    return strtr($template, [
        '{0}' => $values[0],
        '{1}' => $values[1],
        '{2}' => $values[2],
        '{3}' => $values[3],
        '{4}' => $values[4],
        '{5}' => $values[5],
        '{6}' => $values[6],
        '{id}' => $values[3],
        '{lat}' => $values[0],
        '{lon}' => $values[1],
        '{timestamp}' => $values[2],
        '{altitude}' => $values[4],
        '{speed}' => $values[5],
        '{bearing}' => $values[6],
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
        curl_setopt_array($ch, [CURLOPT_RETURNTRANSFER => true, CURLOPT_FOLLOWLOCATION => false, CURLOPT_CONNECTTIMEOUT => 4, CURLOPT_TIMEOUT => 8, CURLOPT_HTTPGET => true, CURLOPT_USERAGENT => 'TRAK-Connect/3.0']);
        $body = (string) curl_exec($ch);
        $status = (int) curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
    } else {
        $context = stream_context_create(['http' => ['method' => 'GET', 'timeout' => 8, 'ignore_errors' => true, 'follow_location' => 0, 'header' => "User-Agent: TRAK-Connect/3.0\r\n"]]);
        $body = (string) @file_get_contents($url, false, $context);
        if (isset($http_response_header[0]) && preg_match('/\s(\d{3})\s/', $http_response_header[0], $m)) $status = (int) $m[1];
    }
    return ['configured' => true, 'ok' => $status >= 200 && $status < 300, 'status' => $status, 'url' => $url, 'response' => function_exists('mb_substr') ? mb_substr($body, 0, 500) : substr($body, 0, 500)];
}
