<?php

declare(strict_types=1);

require_once dirname(__DIR__, 2) . '/config.php';

const TRAK_ACTIVE_INTERVAL_VALUES = [5, 10, 15, 20];
const TRAK_IDLE_INTERVAL_VALUES = [30, 60, 900, 1800, 3600];
const TRAK_SENSITIVITY_VALUES = [1, 2, 3, 4, 5];
const TRAK_DEFAULT_ACTIVE_INTERVAL = 10;
const TRAK_DEFAULT_IDLE_INTERVAL = 60;
const TRAK_DEFAULT_SENSITIVITY = 3;

function validTrakActiveInterval(int $value): int
{
    return in_array($value, TRAK_ACTIVE_INTERVAL_VALUES, true) ? $value : TRAK_DEFAULT_ACTIVE_INTERVAL;
}

function validTrakIdleInterval(int $value): int
{
    return in_array($value, TRAK_IDLE_INTERVAL_VALUES, true) ? $value : TRAK_DEFAULT_IDLE_INTERVAL;
}

function validTrakSensitivity(int $value): int
{
    return in_array($value, TRAK_SENSITIVITY_VALUES, true) ? $value : TRAK_DEFAULT_SENSITIVITY;
}

function requestHasValidApiKey(): bool
{
    $provided = (string) ($_GET['api_key'] ?? '');
    if ($provided === '') {
        $authorization = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
        if (preg_match('/^Bearer\s+(.+)$/i', $authorization, $m)) $provided = trim($m[1]);
    }
    return $provided !== '' && hash_equals(TRAK_API_KEY, $provided);
}

function intervalResponse(): void
{
    $settings = loadSettings();
    $active = validTrakActiveInterval((int) ($settings['active_interval'] ?? TRAK_DEFAULT_ACTIVE_INTERVAL));
    $idle = validTrakIdleInterval((int) ($settings['idle_interval'] ?? TRAK_DEFAULT_IDLE_INTERVAL));
    $sensitivity = validTrakSensitivity((int) ($settings['motion_sensitivity'] ?? TRAK_DEFAULT_SENSITIVITY));
    jsonResponse([
        'ok' => true,
        'active_interval_seconds' => $active,
        'idle_interval_seconds' => $idle,
        'sensitivity_level' => $sensitivity,
    ]);
}

if ($_SERVER['REQUEST_METHOD'] === 'GET' && requestHasValidApiKey()) {
    intervalResponse();
}

requireDashboardAuth();

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    intervalResponse();
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: GET, POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireCsrf();

$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$active = (int) ($data['active_interval_seconds'] ?? 0);
$idle = (int) ($data['idle_interval_seconds'] ?? 0);
$sensitivity = (int) ($data['sensitivity_level'] ?? 0);

if (!in_array($active, TRAK_ACTIVE_INTERVAL_VALUES, true)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_active_interval', 'allowed' => TRAK_ACTIVE_INTERVAL_VALUES], 422);
}
if (!in_array($idle, TRAK_IDLE_INTERVAL_VALUES, true)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_idle_interval', 'allowed' => TRAK_IDLE_INTERVAL_VALUES], 422);
}
if (!in_array($sensitivity, TRAK_SENSITIVITY_VALUES, true)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_sensitivity', 'allowed' => TRAK_SENSITIVITY_VALUES], 422);
}

$settings = loadSettings();
$settings['active_interval'] = $active;
$settings['idle_interval'] = $idle;
$settings['motion_sensitivity'] = $sensitivity;
saveSettings($settings);

intervalResponse();
