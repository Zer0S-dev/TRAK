<?php

declare(strict_types=1);

require_once dirname(__DIR__, 2) . '/config.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireApiKey();

$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$trakId = trim((string) ($data['trak_id'] ?? ''));
if ($trakId === '' || strlen($trakId) > 64 || !preg_match('/^[A-Za-z0-9._-]+$/', $trakId)) jsonResponse(['ok' => false, 'error' => 'invalid_trak_id'], 422);

$latitude = filter_var($data['latitude'] ?? null, FILTER_VALIDATE_FLOAT);
$longitude = filter_var($data['longitude'] ?? null, FILTER_VALIDATE_FLOAT);
$altitude = filter_var($data['altitude'] ?? 0, FILTER_VALIDATE_FLOAT);
$speed = filter_var($data['speed'] ?? $data['speed_kmh'] ?? 0, FILTER_VALIDATE_FLOAT);
$bearing = filter_var($data['bearing'] ?? 0, FILTER_VALIDATE_FLOAT);
$version = trim((string) ($data['version'] ?? ''));
$motion = strtoupper(trim((string) ($data['motion'] ?? 'IMMOBILE')));
$motionReturnMs = filter_var($data['motion_return_ms'] ?? 0, FILTER_VALIDATE_INT);
$network = trim((string) ($data['network'] ?? '4G'));
$signalPercent = filter_var($data['signal_percent'] ?? null, FILTER_VALIDATE_INT);

if ($version !== '' && (strlen($version) > 32 || !preg_match('/^[A-Za-z0-9._-]+$/', $version))) {
    jsonResponse(['ok' => false, 'error' => 'invalid_version'], 422);
}
if ($motion !== 'MOBILE' && $motion !== 'IMMOBILE') {
    jsonResponse(['ok' => false, 'error' => 'invalid_motion'], 422);
}
if ($motionReturnMs === false || $motionReturnMs < 0 || $motionReturnMs > 8000) $motionReturnMs = 0;
if ($network !== 'WiFi' && $network !== '4G') $network = '4G';
if ($network === 'WiFi') $signalPercent = null;
if ($signalPercent !== false && ($signalPercent < 0 || $signalPercent > 100)) $signalPercent = false;

if ($latitude === false || $longitude === false || $altitude === false || !is_finite((float) $latitude) || !is_finite((float) $longitude) || !is_finite((float) $altitude)) jsonResponse(['ok' => false, 'error' => 'invalid_position'], 422);
if ($latitude < -90 || $latitude > 90 || $longitude < -180 || $longitude > 180) jsonResponse(['ok' => false, 'error' => 'coordinates_out_of_range'], 422);
if ($speed !== false && !is_finite((float) $speed)) $speed = false;
if ($bearing !== false && !is_finite((float) $bearing)) $bearing = false;

$position = [
    'trak_id' => $trakId,
    'latitude' => round((float) $latitude, 6),
    'longitude' => round((float) $longitude, 6),
    'altitude' => round((float) $altitude, 1),
    'speed_kmh' => $speed === false ? 0.0 : round((float) $speed, 1),
    'bearing' => $bearing === false ? 0.0 : round((float) $bearing, 1),
    'timestamp' => normaliseTimestamp(isset($data['timestamp']) ? (string) $data['timestamp'] : null),
    'received_at' => gmdate('Y-m-d\TH:i:s\Z'),
    'network' => $network,
    'signal_percent' => $signalPercent === false ? null : ($signalPercent === null ? null : (int) $signalPercent),
    'hasFix' => true,
    'firmware_version' => $version !== '' ? $version : null,
    // Motion comes exclusively from the TRAK LSM6DS3 gyro.
    'motionMode' => $motion,
    'motionReturnSeconds' => (int) ceil((int) $motionReturnMs / 1000),
];

$position = storePosition($trakId, $position);
$relay = sendToTrackserver($position);
$relayFailed = $relay['configured'] && !$relay['ok'];

jsonResponse([
    'ok' => !$relayFailed,
    'received' => $position,
    'trackserver' => [
        'configured' => $relay['configured'],
        'ok' => $relay['ok'],
        'status' => $relay['status'],
    ],
], $relayFailed ? 502 : 200);
