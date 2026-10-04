<?php
declare(strict_types=1);

require_once __DIR__ . '/../partials.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

function trakConfigResponse(array $payload, int $status = 200): never {
    http_response_code($status);
    echo json_encode($payload, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    trakConfigResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

$raw = file_get_contents('php://input');
$input = json_decode($raw ?: '', true);
if (!is_array($input)) {
    trakConfigResponse(['ok' => false, 'error' => 'invalid_json'], 400);
}

$auth = trim((string)($_SERVER['HTTP_AUTHORIZATION'] ?? ''));
$apiKey = '';
if (preg_match('/^Bearer\\s+(.+)$/i', $auth, $m)) {
    $apiKey = trim($m[1]);
}
if ($apiKey === '') {
    trakConfigResponse(['ok' => false, 'error' => 'missing_api_key'], 401);
}

$trakId = strtoupper(trim((string)($input['trak_id'] ?? '')));
$action = strtolower(trim((string)($input['action'] ?? 'status')));

if ($trakId === '') {
    trakConfigResponse(['ok' => false, 'error' => 'missing_trak_id'], 400);
}

$pdo = db();
$stmt = $pdo->prepare('
    SELECT tb.id, tb.trak_id, tb.api_key, tc.api_key AS pending_api_key, tc.config_pending
    FROM trak_boxes tb
    LEFT JOIN trak_configs tc ON tc.trak_box_id = tb.id
    WHERE tb.trak_id = ?
    LIMIT 1
');
$stmt->execute([$trakId]);
$trak = $stmt->fetch();

$validCurrentKey = $trak && hash_equals((string)$trak['api_key'], $apiKey);
$validPendingKey = $trak && (int)$trak['config_pending'] === 1 &&
    (string)$trak['pending_api_key'] !== '' &&
    hash_equals((string)$trak['pending_api_key'], $apiKey);
if (!$trak || (!$validCurrentKey && !$validPendingKey)) {
    trakConfigResponse(['ok' => false, 'error' => 'invalid_credentials'], 401);
}

$configStmt = $pdo->prepare('SELECT * FROM trak_configs WHERE trak_box_id = ? LIMIT 1');
$configStmt->execute([(int)$trak['id']]);
$config = $configStmt->fetch();

if (!$config) {
    trakConfigResponse(['ok' => false, 'error' => 'config_not_found'], 404);
}

if ($action === 'status') {
    trakConfigResponse([
        'ok' => true,
        'trak_id' => $trak['trak_id'],
        'config_pending' => (int)$config['config_pending'],
        'config_updated_at' => (int)$config['config_updated_at']
    ]);
}

if ($action === 'config') {
    $lastTimestamp = (int)($input['last_config_timestamp'] ?? 0);
    $serverTimestamp = (int)$config['config_updated_at'];

    if ((int)$config['config_pending'] !== 1 || $serverTimestamp <= $lastTimestamp) {
        trakConfigResponse([
            'ok' => true,
            'config_pending' => 0,
            'config_updated_at' => $serverTimestamp,
            'config_available' => false
        ]);
    }

    trakConfigResponse([
        'ok' => true,
        'config_pending' => 1,
        'config_available' => true,
        'config_updated_at' => $serverTimestamp,
        'config' => [
            'trak_id' => $trak['trak_id'],
            'trak_phone' => $config['trak_phone'],
            'user_phone' => $config['user_phone'],
            'api_key' => $config['api_key'],
            'trackserver_url' => $config['trackserver_url'],
            'dashboard_url' => $config['dashboard_url'],
            'wifi' => [
                [
                    'slot' => 1,
                    'ssid' => $config['wifi_ssid_1'],
                    'password' => $config['wifi_password_1']
                ],
                [
                    'slot' => 2,
                    'ssid' => $config['wifi_ssid_2'],
                    'password' => $config['wifi_password_2']
                ],
                [
                    'slot' => 3,
                    'ssid' => $config['wifi_ssid_3'],
                    'password' => $config['wifi_password_3']
                ]
            ]
        ]
    ]);
}

if ($action === 'ack') {
    $timestamp = (int)($input['config_updated_at'] ?? 0);
    if ($timestamp <= 0) {
        trakConfigResponse(['ok' => false, 'error' => 'missing_config_timestamp'], 400);
    }

    $stmt = $pdo->prepare('
        UPDATE trak_configs
        SET config_pending = 0,
            updated_at = CURRENT_TIMESTAMP
        WHERE trak_box_id = ?
          AND config_pending = 1
          AND config_updated_at = ?
    ');
    $stmt->execute([(int)$trak['id'], $timestamp]);

    if ($stmt->rowCount() !== 1) {
        trakConfigResponse([
            'ok' => false,
            'error' => 'config_timestamp_mismatch',
            'config_pending' => (int)$config['config_pending'],
            'config_updated_at' => (int)$config['config_updated_at']
        ], 409);
    }

    $updateKey = $pdo->prepare('
        UPDATE trak_boxes
        SET api_key = ?
        WHERE id = ?
    ');
    $updateKey->execute([(string)$config['api_key'], (int)$trak['id']);

    trakConfigResponse([
        'ok' => true,
        'trak_id' => $trak['trak_id'],
        'config_pending' => 0,
        'config_updated_at' => $timestamp
    ]);
}

trakConfigResponse(['ok' => false, 'error' => 'unknown_action'], 400);
