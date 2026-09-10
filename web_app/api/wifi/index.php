<?php

declare(strict_types=1);

// Never let a PHP fatal error become an empty Apache 500 response.
register_shutdown_function(static function (): void {
    $error = error_get_last();
    if ($error === null) return;
    $fatalTypes = [E_ERROR, E_PARSE, E_CORE_ERROR, E_COMPILE_ERROR, E_USER_ERROR];
    if (!in_array($error['type'], $fatalTypes, true)) return;
    if (!headers_sent()) {
        http_response_code(500);
        header('Content-Type: application/json; charset=utf-8');
        header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    }
    echo json_encode([
        'ok' => false,
        'error' => 'php_fatal',
        'message' => $error['message'],
        'file' => basename($error['file']),
        'line' => $error['line'],
    ], JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
});

require_once dirname(__DIR__, 2) . '/config.php';

const TRAK_WIFI_MAX_PROFILES = 3;

function validWifiSlot(int $slot): bool
{
    return $slot >= 0 && $slot < TRAK_WIFI_MAX_PROFILES;
}

function cleanWifiString($value, int $max): string
{
    $value = trim((string) $value);
    return function_exists('mb_substr') ? mb_substr($value, 0, $max) : substr($value, 0, $max);
}

function loadWifiProfiles(bool $includePasswords): array
{
    $settings = loadSettings();
    $stored = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : [];
    $out = [];

    for ($i = 0; $i < TRAK_WIFI_MAX_PROFILES; ++$i) {
        $p = is_array($stored[$i] ?? null) ? $stored[$i] : [];
        $item = [
            'slot' => $i,
            'ssid' => (string) ($p['ssid'] ?? ''),
        ];
        if ($includePasswords) {
            $item['password'] = (string) ($p['password'] ?? '');
        } else {
            $item['configured'] = $item['ssid'] !== '';
        }
        $out[] = $item;
    }

    return $out;
}

function wifiResponse(bool $includePasswords): void
{
    jsonResponse(['ok' => true, 'profiles' => loadWifiProfiles($includePasswords)]);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET' && isset($_GET['api_key'])) {
    requireApiKey();
    wifiResponse(true);
}

requireDashboardAuth();

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    wifiResponse(false);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: GET, POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireCsrf();

$raw = file_get_contents('php://input');
$data = json_decode($raw !== false ? $raw : '', true);
if (!is_array($data)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);
}

$slot = (int) ($data['slot'] ?? -1);
if (!validWifiSlot($slot)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_slot'], 422);
}

$ssid = cleanWifiString($data['ssid'] ?? '', 64);
$passwordProvided = array_key_exists('password', $data);
$password = cleanWifiString($data['password'] ?? '', 128);

if ($ssid === '') {
    jsonResponse(['ok' => false, 'error' => 'ssid_required'], 422);
}
if ($passwordProvided && $password !== '' && strlen($password) < 8) {
    jsonResponse(['ok' => false, 'error' => 'password_too_short'], 422);
}

$settings = loadSettings();
$profiles = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : [];
for ($i = 0; $i < TRAK_WIFI_MAX_PROFILES; ++$i) {
    if (!isset($profiles[$i]) || !is_array($profiles[$i])) {
        $profiles[$i] = ['ssid' => '', 'password' => ''];
    }
}

$oldPassword = (string) ($profiles[$slot]['password'] ?? '');
$profiles[$slot] = [
    'ssid' => $ssid,
    'password' => ($passwordProvided && $password !== '') ? $password : $oldPassword,
];
$settings['wifi_profiles'] = array_values($profiles);

saveSettings($settings);
wifiResponse(false);
