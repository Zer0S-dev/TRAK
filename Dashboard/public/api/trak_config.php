<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'method_not_allowed']);
    exit;
}

$raw = file_get_contents('php://input');
$input = json_decode($raw ?: '', true);

if (!is_array($input)) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'invalid_json']);
    exit;
}

$auth = trim((string)($_SERVER['HTTP_AUTHORIZATION'] ?? ''));
$apiKey = '';

if (preg_match('/^Bearer\\s+(.+)$/i', $auth, $matches)) {
    $apiKey = trim($matches[1]);
}

if ($apiKey === '') {
    http_response_code(401);
    echo json_encode(['ok' => false, 'error' => 'missing_api_key']);
    exit;
}

$trakId = strtoupper(trim((string)($input['trak_id'] ?? '')));
$action = strtolower(trim((string)($input['action'] ?? 'status')));

if ($trakId === '') {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'missing_trak_id']);
    exit;
}

try {
    require_once dirname(__DIR__, 2) . '/app/db.php';
    $pdo = db();

    $stmt = $pdo->prepare(
        'SELECT tb.id, tb.trak_id, tb.api_key,
                tc.api_key AS pending_api_key,
                tc.config_pending
         FROM trak_boxes tb
         LEFT JOIN trak_configs tc ON tc.trak_box_id = tb.id
         WHERE tb.trak_id = ?
         LIMIT 1'
    );
    $stmt->execute([$trakId]);
    $trak = $stmt->fetch();

    if (!$trak) {
        http_response_code(401);
        echo json_encode(['ok' => false, 'error' => 'invalid_credentials']);
        exit;
    }

    $validCurrentKey = hash_equals((string)$trak['api_key'], $apiKey);
    $validPendingKey =
        (int)($trak['config_pending'] ?? 0) === 1 &&
        (string)($trak['pending_api_key'] ?? '') !== '' &&
        hash_equals((string)$trak['pending_api_key'], $apiKey);

    if (!$validCurrentKey && !$validPendingKey) {
        http_response_code(401);
        echo json_encode(['ok' => false, 'error' => 'invalid_credentials']);
        exit;
    }

    $configStmt = $pdo->prepare(
        'SELECT * FROM trak_configs WHERE trak_box_id = ? LIMIT 1'
    );
    $configStmt->execute([(int)$trak['id']]);
    $config = $configStmt->fetch();

    if (!$config) {
        http_response_code(404);
        echo json_encode(['ok' => false, 'error' => 'config_not_found']);
        exit;
    }

    if ($action === 'status') {
        echo json_encode([
            'ok' => true,
            'trak_id' => $trak['trak_id'],
            'config_pending' => (int)$config['config_pending'],
            'config_updated_at' => (int)$config['config_updated_at']
        ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if ($action === 'config') {
        $lastTimestamp = (int)($input['last_config_timestamp'] ?? 0);
        $serverTimestamp = (int)$config['config_updated_at'];

        if ($serverTimestamp <= $lastTimestamp) {
            echo json_encode([
                'ok' => true,
                'config_pending' => 0,
                'config_updated_at' => $serverTimestamp,
                'config_available' => false
            ]);
            exit;
        }

        echo json_encode([
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
                    ['slot' => 1, 'ssid' => $config['wifi_ssid_1'], 'password' => $config['wifi_password_1']],
                    ['slot' => 2, 'ssid' => $config['wifi_ssid_2'], 'password' => $config['wifi_password_2']],
                    ['slot' => 3, 'ssid' => $config['wifi_ssid_3'], 'password' => $config['wifi_password_3']]
                ]
            ]
        ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        exit;
    }

    if ($action === 'ack') {
        $timestamp = (int)($input['config_updated_at'] ?? 0);

        if ($timestamp <= 0) {
            http_response_code(400);
            echo json_encode(['ok' => false, 'error' => 'missing_config_timestamp']);
            exit;
        }

        $pdo->beginTransaction();

        try {
            $update = $pdo->prepare(
                'UPDATE trak_configs
                 SET config_pending = 0, updated_at = CURRENT_TIMESTAMP
                 WHERE trak_box_id = ?
                   AND config_pending = 1
                   AND config_updated_at = ?'
            );
            $update->execute([(int)$trak['id'], $timestamp]);

            if ($update->rowCount() !== 1) {
                $pdo->rollBack();
                http_response_code(409);
                echo json_encode([
                    'ok' => false,
                    'error' => 'config_timestamp_mismatch',
                    'config_pending' => (int)$config['config_pending'],
                    'config_updated_at' => (int)$config['config_updated_at']
                ]);
                exit;
            }

            $updateKey = $pdo->prepare(
                'UPDATE trak_boxes SET api_key = ? WHERE id = ?'
            );
            $updateKey->execute([
                (string)$config['api_key'],
                (int)$trak['id']
            ]);

            $pdo->commit();
        } catch (Throwable $e) {
            if ($pdo->inTransaction()) {
                $pdo->rollBack();
            }
            throw $e;
        }

        echo json_encode([
            'ok' => true,
            'trak_id' => $trak['trak_id'],
            'config_pending' => 0,
            'config_updated_at' => $timestamp
        ]);
        exit;
    }

    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'unknown_action']);
    exit;

} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode([
        'ok' => false,
        'error' => 'server_exception',
        'message' => $e->getMessage(),
        'file' => basename($e->getFile()),
        'line' => $e->getLine()
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}
