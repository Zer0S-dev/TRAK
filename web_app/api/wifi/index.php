<?php

declare(strict_types=1);

require_once dirname(__DIR__, 2) . '/config.php';

const TRAK_WIFI_MAX_PROFILES = 3;

function validWifiSlot(int $slot): bool { return $slot >= 0 && $slot < TRAK_WIFI_MAX_PROFILES; }
function cleanWifiString($value, int $max): string {
    $value = trim((string) $value);
    return mb_substr($value, 0, $max);
}
function loadWifiProfiles(bool $includePasswords): array {
    $settings = loadSettings();
    $stored = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : [];
    $out = [];
    for ($i = 0; $i < TRAK_WIFI_MAX_PROFILES; ++$i) {
        $p = is_array($stored[$i] ?? null) ? $stored[$i] : [];
        $item = ['slot' => $i, 'ssid' => (string) ($p['ssid'] ?? '')];
        if ($includePasswords) $item['password'] = (string) ($p['password'] ?? '');
        else $item['configured'] = $item['ssid'] !== '';
        $out[] = $item;
    }
    return $out;
}

function wifiResponse(bool $includePasswords): void {
    jsonResponse(['ok' => true, 'profiles' => loadWifiProfiles($includePasswords)]);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET' && isset($_GET['api_key'])) {
    requireApiKey();
    wifiResponse(true);
}

requireDashboardAuth();
if ($_SERVER['REQUEST_METHOD'] === 'GET') wifiResponse(false);
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: GET, POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}
requireCsrf();

$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$slot = (int) ($data['slot'] ?? -1);
if (!validWifiSlot($slot)) jsonResponse(['ok' => false, 'error' => 'invalid_slot'], 422);
$ssid = cleanWifiString($data['ssid'] ?? '', 64);
$password = cleanWifiString($data['password'] ?? '', 128);
if ($ssid === '') jsonResponse(['ok' => false, 'error' => 'ssid_required'], 422);
if (strlen($password) > 0 && strlen($password) < 8) jsonResponse(['ok' => false, 'error' => 'password_too_short'], 422);

$settings = loadSettings();
$profiles = is_array($settings['wifi_profiles'] ?? null) ? $settings['wifi_profiles'] : [];
for ($i = 0; $i < TRAK_WIFI_MAX_PROFILES; ++$i) if (!isset($profiles[$i]) || !is_array($profiles[$i])) $profiles[$i] = ['ssid' => '', 'password' => ''];
$profiles[$slot] = ['ssid' => $ssid, 'password' => $password];
$settings['wifi_profiles'] = array_values($profiles);
saveSettings($settings);
wifiResponse(false);
