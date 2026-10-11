<?php
declare(strict_types=1);

/*
 * TRAK remote configuration API.
 * Always returns a JSON body, including errors.
 */

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
header('Pragma: no-cache');

function respond(array $payload, int $status = 200): never
{
    http_response_code($status);

    $json = json_encode(
        $payload,
        JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE
    );

    if ($json === false) {
        $json = '{"ok":false,"error":"json_encode_failed"}';
        http_response_code(500);
    }

    // Avoid an output buffer or PHP compression layer swallowing/changing
    // the response body seen by the A7670 HTTP client.
    while (ob_get_level() > 0) {
        ob_end_clean();
    }

    header('Content-Length: ' . strlen($json));
    echo $json;
    exit;
}

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
    respond(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

try {
    $raw = file_get_contents('php://input');
    $input = json_decode($raw ?: '', true);

    if (!is_array($input)) {
        respond(['ok' => false, 'error' => 'invalid_json'], 400);
    }

    $auth = trim((string)($_SERVER['HTTP_AUTHORIZATION'] ?? ''));
    $apiKey = '';

    if (preg_match('/^Bearer\s+(.+)$/i', $auth, $matches)) {
        $apiKey = trim($matches[1]);
    }

    if ($apiKey === '') {
        respond(['ok' => false, 'error' => 'missing_api_key'], 401);
    }

    $trakId = strtoupper(trim((string)($input['trak_id'] ?? '')));
    $action = strtolower(trim((string)($input['action'] ?? 'status')));

    if ($trakId === '') {
        respond(['ok' => false, 'error' => 'missing_trak_id'], 400);
    }

    require_once dirname(__DIR__, 2) . '/app/db.php';
    $pdo = db();

    $stmt = $pdo->prepare(
        'SELECT tb.id, tb.trak_id, tb.api_key,
                tc.api_key AS pending_api_key,
                tc.config_pending,
                tc.config_updated_at
         FROM trak_boxes tb
         LEFT JOIN trak_configs tc ON tc.trak_box_id = tb.id
         WHERE tb.trak_id = ?
         LIMIT 1'
    );
    $stmt->execute([$trakId]);
    $trak = $stmt->fetch();

    if (!$trak) {
        respond(['ok' => false, 'error' => 'invalid_credentials'], 401);
    }

    $validCurrentKey = hash_equals((string)$trak['api_key'], $apiKey);
    $validPendingKey =
        (int)($trak['config_pending'] ?? 0) === 1 &&
        (string)($trak['pending_api_key'] ?? '') !== '' &&
        hash_equals((string)$trak['pending_api_key'], $apiKey);

    if (!$validCurrentKey && !$validPendingKey) {
        respond(['ok' => false, 'error' => 'invalid_credentials'], 401);
    }

    $configStmt = $pdo->prepare(
        'SELECT * FROM trak_configs WHERE trak_box_id = ? LIMIT 1'
    );
    $configStmt->execute([(int)$trak['id']]);
    $config = $configStmt->fetch();

    // Diagnostic périodique envoyé avec l'action STATUS du check de configuration.
    // Les anciens firmwares qui n'envoient pas ce bloc restent compatibles.
    $diagnostic = $input['diagnostic'] ?? null;
    if (is_array($diagnostic)) {
        $number = static function (array $data, string $key, float $min, float $max): ?float {
            if (!array_key_exists($key, $data) || $data[$key] === null || $data[$key] === '' || !is_numeric($data[$key])) {
                return null;
            }
            $value = (float)$data[$key];
            return is_finite($value) && $value >= $min && $value <= $max ? $value : null;
        };
        $integer = static function (array $data, string $key, int $min, int $max): ?int {
            $value = $number($data, $key, $min, $max);
            return $value === null ? null : (int)$value;
        };

        $gnssFix = !empty($diagnostic['gnss_fix']) ? 1 : 0;
        $fixMode = $integer($diagnostic, 'fix_mode', 0, 3);
        $satTotal = $integer($diagnostic, 'satellites_total', 0, 100);
        $satUsed = $integer($diagnostic, 'satellites_used', 0, 100);
        $gps = $integer($diagnostic, 'gps_satellites', 0, 100);
        $glonass = $integer($diagnostic, 'glonass_satellites', 0, 100);
        $galileo = $integer($diagnostic, 'galileo_satellites', 0, 100);
        $beidou = $integer($diagnostic, 'beidou_satellites', 0, 100);
        $hdop = $number($diagnostic, 'hdop', 0, 999);
        $pdop = $number($diagnostic, 'pdop', 0, 999);
        $vdop = $number($diagnostic, 'vdop', 0, 999);
        $volt = $number($diagnostic, 'volt', 0, 1000);
        $amp = $number($diagnostic, 'amp', -1000, 1000);
        $gnssTimestamp = trim((string)($diagnostic['gnss_timestamp'] ?? ''));
        if ($gnssTimestamp === '' || strlen($gnssTimestamp) > 40) $gnssTimestamp = null;

        $diagnosticValues = [
            (int)$trak['id'], $gnssFix, $fixMode, $satTotal, $satUsed,
            $gps, $glonass, $galileo, $beidou, $hdop, $pdop, $vdop,
            $gnssTimestamp, $volt, $amp
        ];
        $columns = 'trak_box_id, gnss_fix, fix_mode, satellites_total, satellites_used,
            gps_satellites, glonass_satellites, galileo_satellites, beidou_satellites,
            hdop, pdop, vdop, gnss_timestamp, volt, amp';

        $historyStmt = $pdo->prepare(
            "INSERT INTO trak_diagnostics_history ($columns) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );
        $historyStmt->execute($diagnosticValues);

        $currentStmt = $pdo->prepare(
            "INSERT INTO trak_diagnostics ($columns) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
             ON CONFLICT(trak_box_id) DO UPDATE SET
                gnss_fix = excluded.gnss_fix,
                fix_mode = excluded.fix_mode,
                satellites_total = excluded.satellites_total,
                satellites_used = excluded.satellites_used,
                gps_satellites = excluded.gps_satellites,
                glonass_satellites = excluded.glonass_satellites,
                galileo_satellites = excluded.galileo_satellites,
                beidou_satellites = excluded.beidou_satellites,
                hdop = excluded.hdop,
                pdop = excluded.pdop,
                vdop = excluded.vdop,
                gnss_timestamp = excluded.gnss_timestamp,
                volt = excluded.volt,
                amp = excluded.amp,
                received_at = CURRENT_TIMESTAMP"
        );
        $currentStmt->execute($diagnosticValues);

        // Alimenter les graphiques électriques existants uniquement si U et I sont valides.
        if ($volt !== null && $amp !== null) {
            $healthTimestamp = $gnssFix === 1 ? $gnssTimestamp : null;
            $healthStmt = $pdo->prepare(
                'INSERT INTO trak_health (trak_box_id, volt, amp, health_timestamp, received_at)
                 VALUES (?, ?, ?, ?, CURRENT_TIMESTAMP)
                 ON CONFLICT(trak_box_id) DO UPDATE SET
                    volt = excluded.volt,
                    amp = excluded.amp,
                    health_timestamp = excluded.health_timestamp,
                    received_at = CURRENT_TIMESTAMP'
            );
            $healthStmt->execute([(int)$trak['id'], $volt, $amp, $healthTimestamp]);

            $historyHealthStmt = $pdo->prepare(
                'INSERT INTO health (trak_box_id, volt, amp, received_at) VALUES (?, ?, ?, CURRENT_TIMESTAMP)'
            );
            $historyHealthStmt->execute([(int)$trak['id'], $volt, $amp]);
        }
    }


    /*
     * STATUS must always be available once the TRAK itself is known.
     * This is important for the A7670 client: HTTP 200 with an empty body
     * is not a usable status response.
     */
    if ($action === 'status') {
        if (!$config) {
            respond([
                'ok' => true,
                'trak_id' => $trak['trak_id'],
                'config_pending' => 0,
                'config_updated_at' => 0,
                'config_available' => false
            ]);
        }

        respond([
            'ok' => true,
            'trak_id' => $trak['trak_id'],
            'config_pending' => (int)$config['config_pending'],
            'config_updated_at' => (int)$config['config_updated_at']
        ]);
    }

    if (!$config) {
        respond(['ok' => false, 'error' => 'config_not_found'], 404);
    }

    if ($action === 'config') {
        $lastTimestamp = (int)($input['last_config_timestamp'] ?? 0);
        $serverTimestamp = (int)$config['config_updated_at'];

        if ($serverTimestamp <= $lastTimestamp) {
            respond([
                'ok' => true,
                'config_pending' => 0,
                'config_updated_at' => $serverTimestamp,
                'config_available' => false
            ]);
        }

        respond([
            'ok' => true,
            'config_pending' => 1,
            'config_available' => true,
            'config_updated_at' => $serverTimestamp,
            'config' => [
                'trak_id' => $trak['trak_id'],
                'trak_phone' => (string)$config['trak_phone'],
                'user_phone' => (string)$config['user_phone'],
                'api_key' => (string)$config['api_key'],
                'trackserver_url' => (string)$config['trackserver_url'],
                'dashboard_url' => (string)$config['dashboard_url'],
                'gyro_sens' => (int)($config['gyro_sens'] ?? 2),
                'send_inter' => (int)($config['send_inter'] ?? 1),
                'wifi' => [
                    [
                        'slot' => 1,
                        'ssid' => (string)$config['wifi_ssid_1'],
                        'password' => (string)$config['wifi_password_1']
                    ],
                    [
                        'slot' => 2,
                        'ssid' => (string)$config['wifi_ssid_2'],
                        'password' => (string)$config['wifi_password_2']
                    ],
                    [
                        'slot' => 3,
                        'ssid' => (string)$config['wifi_ssid_3'],
                        'password' => (string)$config['wifi_password_3']
                    ]
                ]
            ]
        ]);
    }

    if ($action === 'ack') {
        $timestamp = (int)($input['config_updated_at'] ?? 0);

        if ($timestamp <= 0) {
            respond(['ok' => false, 'error' => 'missing_config_timestamp'], 400);
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
                respond([
                    'ok' => false,
                    'error' => 'config_timestamp_mismatch',
                    'config_pending' => (int)$config['config_pending'],
                    'config_updated_at' => (int)$config['config_updated_at']
                ], 409);
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

        respond([
            'ok' => true,
            'trak_id' => $trak['trak_id'],
            'config_pending' => 0,
            'config_updated_at' => $timestamp
        ]);
    }

    respond(['ok' => false, 'error' => 'unknown_action'], 400);

} catch (Throwable $e) {
    respond([
        'ok' => false,
        'error' => 'server_exception',
        'message' => $e->getMessage(),
        'file' => basename($e->getFile()),
        'line' => $e->getLine()
    ], 500);
}
