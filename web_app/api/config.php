<?php

declare(strict_types=1);

const TRACKSERVER_OSMAND_URL = 'https://surlereservoir.fr/trackserver/surledoud/eb5a96a7/?lat={0}&lon={1}&timestamp={2}&altitude={4}&speed={5}&bearing={6}';
const TRAK_STORAGE_DIR = dirname(__DIR__) . '/storage';
const TRAK_STORAGE_FILE = TRAK_STORAGE_DIR . '/positions.json';
const TRAK_SETTINGS_FILE = TRAK_STORAGE_DIR . '/settings.json';

const TRAK_USER_DB_FILE = TRAK_STORAGE_DIR . '/users.sqlite';

function startTrakSession(): void
{
    if (session_status() === PHP_SESSION_ACTIVE) return;
    session_name('trak_session');
    $isHttps = (!empty($_SERVER['HTTPS']) && strtolower((string) $_SERVER['HTTPS']) !== 'off')
        || ((int) ($_SERVER['SERVER_PORT'] ?? 0) === 443);
    session_set_cookie_params([
        'lifetime' => 0,
        'path' => '/trak/',
        'secure' => $isHttps,
        'httponly' => true,
        'samesite' => 'Strict',
    ]);
    session_start();
}

function userDatabase(): PDO
{
    ensureTrakStorage();

    if (!extension_loaded('pdo_sqlite')) {
        throw new RuntimeException('PDO SQLite est indisponible sur ce serveur PHP.');
    }

    if (!is_dir(TRAK_STORAGE_DIR) || !is_writable(TRAK_STORAGE_DIR)) {
        throw new RuntimeException('Le dossier web_app/storage doit être accessible en écriture par PHP.');
    }

    $pdo = new PDO('sqlite:' . TRAK_USER_DB_FILE, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
    $pdo->exec('PRAGMA foreign_keys = ON');
    $pdo->exec('CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        email TEXT NOT NULL DEFAULT "",
        password_hash TEXT NOT NULL,
        created_at TEXT NOT NULL,
        updated_at TEXT NOT NULL
    )');
    return $pdo;
}

function userCount(): int
{
    return (int) userDatabase()->query('SELECT COUNT(*) FROM users')->fetchColumn();
}

function findUserByUsername(string $username): ?array
{
    $stmt = userDatabase()->prepare('SELECT id, username, email, password_hash FROM users WHERE username = :username LIMIT 1');
    $stmt->execute(['username' => $username]);
    $user = $stmt->fetch();
    return is_array($user) ? $user : null;
}

function currentUser(): ?array
{
    startTrakSession();
    $username = trim((string) ($_SESSION['trak_user'] ?? ''));
    return $username === '' ? null : findUserByUsername($username);
}

function createFirstUser(string $username, string $email, string $password): void
{
    $username = trim($username);
    $email = trim($email);
    if ($username === '' || strlen($username) < 3) jsonResponse(['ok' => false, 'error' => 'invalid_username'], 422);
    if ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) jsonResponse(['ok' => false, 'error' => 'invalid_email'], 422);
    if (strlen($password) < 8) jsonResponse(['ok' => false, 'error' => 'password_too_short'], 422);
    $pdo = userDatabase();
    if ((int) $pdo->query('SELECT COUNT(*) FROM users')->fetchColumn() > 0) {
        jsonResponse(['ok' => false, 'error' => 'setup_already_done'], 409);
    }
    $now = gmdate('c');
    $stmt = $pdo->prepare('INSERT INTO users (username, email, password_hash, created_at, updated_at) VALUES (:username, :email, :password_hash, :created_at, :updated_at)');
    try {
        $stmt->execute([
            'username' => $username,
            'email' => $email,
            'password_hash' => password_hash($password, PASSWORD_DEFAULT),
            'created_at' => $now,
            'updated_at' => $now,
        ]);
    } catch (PDOException $e) {
        jsonResponse(['ok' => false, 'error' => 'user_create_failed'], 500);
    }
}

function dashboardUser(): string
{
    $user = currentUser();
    return $user ? (string) $user['username'] : '';
}

function dashboardPasswordHash(): string
{
    $user = currentUser();
    return $user ? (string) $user['password_hash'] : '';
}

function requireDashboardAuth(): void
{
    startTrakSession();
    if (empty($_SESSION['trak_authenticated']) || $_SESSION['trak_authenticated'] !== true) {
        jsonResponse(['ok' => false, 'error' => 'unauthorized'], 401);
    }
    if (currentUser() === null) {
        $_SESSION = [];
        session_destroy();
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

function ensureTrakStorage(): void
{
    if (!is_dir(TRAK_STORAGE_DIR)) {
        if (!@mkdir(TRAK_STORAGE_DIR, 0775, true) && !is_dir(TRAK_STORAGE_DIR)) {
            jsonResponse(['ok' => false, 'error' => 'storage_dir_unavailable'], 503);
        }
    }

    if (!is_file(TRAK_STORAGE_FILE)) {
        if (@file_put_contents(TRAK_STORAGE_FILE, "{}", LOCK_EX) === false) jsonResponse(['ok' => false, 'error' => 'storage_init_failed'], 503);
    }

    if (!is_file(TRAK_SETTINGS_FILE)) {
        $defaults = [
            'trackserver_url' => TRACKSERVER_OSMAND_URL,
            'record_interval' => 15,
            'wifi_profiles' => [
                ['ssid' => '', 'password' => ''],
                ['ssid' => '', 'password' => ''],
                ['ssid' => '', 'password' => ''],
            ],
        ];
        if (@file_put_contents(TRAK_SETTINGS_FILE, json_encode($defaults, JSON_UNESCAPED_SLASHES), LOCK_EX) === false) jsonResponse(['ok' => false, 'error' => 'settings_init_failed'], 503);
    }
}

function jsonResponse(array $payload, int $status = 200): never
{
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    echo json_encode($payload, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function loadPositions(): array
{
    ensureTrakStorage();
    $raw = @file_get_contents(TRAK_STORAGE_FILE);
    if ($raw === false || trim($raw) === '') return [];
    $data = json_decode($raw, true);
    return is_array($data) ? $data : [];
}

function savePositions(array $positions): void
{
    ensureTrakStorage();
    $json = json_encode($positions, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) jsonResponse(['ok' => false, 'error' => 'storage_encode_failed'], 500);
    if (@file_put_contents(TRAK_STORAGE_FILE, $json, LOCK_EX) === false) jsonResponse(['ok' => false, 'error' => 'storage_write_failed'], 500);
}

function storePosition(string $trakId, array $position): array
{
    ensureTrakStorage();
    $lockPath = TRAK_STORAGE_FILE . '.lock';
    $lock = @fopen($lockPath, 'c');
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
    $raw = @file_get_contents(TRAK_SETTINGS_FILE);
    if ($raw === false || trim($raw) === '') return [
        'trackserver_url' => TRACKSERVER_OSMAND_URL,
        'record_interval' => 15,
        'wifi_profiles' => [
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
        ],
    ];
    $data = json_decode($raw, true);
    return is_array($data) ? $data : [
        'trackserver_url' => TRACKSERVER_OSMAND_URL,
        'record_interval' => 15,
        'wifi_profiles' => [
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
            ['ssid' => '', 'password' => ''],
        ],
    ];
}

function saveSettings(array $settings): void
{
    ensureTrakStorage();
    $json = json_encode($settings, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    if ($json === false) jsonResponse(['ok' => false, 'error' => 'settings_encode_failed'], 500);
    if (@file_put_contents(TRAK_SETTINGS_FILE, $json, LOCK_EX) === false) jsonResponse(['ok' => false, 'error' => 'settings_write_failed'], 500);
}

function getTrackserverConfiguredUrl(): string
{
    $settings = loadSettings();
    $url = trim((string) ($settings['trackserver_url'] ?? ''));
    return $url !== '' ? $url : TRACKSERVER_OSMAND_URL;
}

function normaliseTimestamp(?string $timestamp): string
{
    if ($timestamp === null || trim($timestamp) === '') return gmdate('Y-m-d\\TH:i:s\\Z');
    $time = strtotime($timestamp);
    return $time === false ? gmdate('Y-m-d\\TH:i:s\\Z') : gmdate('Y-m-d\\TH:i:s\\Z', $time);
}

function trackserverUrl(array $position): string
{
    $template = getTrackserverConfiguredUrl();
    if ($template === '') return '';
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
