<?php
declare(strict_types=1);

require_once __DIR__ . '/../app/db.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');

function positionResponse(int $status, array $data): never {
    http_response_code($status);
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

if (!in_array($_SERVER['REQUEST_METHOD'] ?? 'GET', ['GET', 'POST'], true)) {
    header('Allow: GET, POST');
    positionResponse(405, ['ok' => false, 'error' => 'method_not_allowed']);
}

$trakId = trim((string)($_GET['trak_id'] ?? $_POST['trak_id'] ?? ''));
$apiKey = trim((string)($_GET['api_key'] ?? $_POST['api_key'] ?? ''));
$latRaw = $_GET['lat'] ?? $_POST['lat'] ?? null;
$lonRaw = $_GET['lon'] ?? $_POST['lon'] ?? null;
$altRaw = $_GET['altitude'] ?? $_GET['alt'] ?? $_POST['altitude'] ?? $_POST['alt'] ?? null;
$timestamp = trim((string)($_GET['timestamp'] ?? $_POST['timestamp'] ?? ''));
$speedRaw = $_GET['speed'] ?? $_POST['speed'] ?? null;
$bearingRaw = $_GET['bearing'] ?? $_POST['bearing'] ?? null;

if ($trakId === '') $trakId = trim((string)($_SERVER['HTTP_X_TRAK_ID'] ?? ''));
if ($apiKey === '') $apiKey = trim((string)($_SERVER['HTTP_X_API_KEY'] ?? ''));

if ($_SERVER['REQUEST_METHOD'] === 'POST' && str_contains(strtolower((string)($_SERVER['CONTENT_TYPE'] ?? '')), 'application/json')) {
    $json = json_decode((string)file_get_contents('php://input'), true);
    if (is_array($json)) {
        $trakId = $trakId !== '' ? $trakId : trim((string)($json['trak_id'] ?? ''));
        $apiKey = $apiKey !== '' ? $apiKey : trim((string)($json['api_key'] ?? ''));
        $latRaw = $json['lat'] ?? $latRaw;
        $lonRaw = $json['lon'] ?? $lonRaw;
        $altRaw = $json['altitude'] ?? $json['alt'] ?? $altRaw;
        $timestamp = $timestamp !== '' ? $timestamp : trim((string)($json['timestamp'] ?? ''));
        $speedRaw = $json['speed'] ?? $speedRaw;
        $bearingRaw = $json['bearing'] ?? $bearingRaw;
    }
}

if (!preg_match('/^[A-Z0-9][A-Z0-9_-]{0,4}$/', $trakId) || !preg_match('/^[A-Za-z0-9]{16}$/', $apiKey)) {
    positionResponse(401, ['ok' => false, 'error' => 'invalid_credentials']);
}
if (!is_numeric($latRaw) || !is_numeric($lonRaw)) {
    positionResponse(422, ['ok' => false, 'error' => 'invalid_position']);
}

$latitude = (float)$latRaw;
$longitude = (float)$lonRaw;
$altitude = null;
if ($altRaw !== null && $altRaw !== '' && is_numeric($altRaw) && is_finite((float)$altRaw)) {
    $altitude = (float)$altRaw;
}
if (!is_finite($latitude) || !is_finite($longitude) || $latitude < -90 || $latitude > 90 || $longitude < -180 || $longitude > 180) {
    positionResponse(422, ['ok' => false, 'error' => 'invalid_position']);
}
$speed = null;
if ($speedRaw !== null && $speedRaw !== '' && is_numeric($speedRaw) && is_finite((float)$speedRaw)) $speed = (float)$speedRaw;
$bearing = null;
if ($bearingRaw !== null && $bearingRaw !== '' && is_numeric($bearingRaw) && is_finite((float)$bearingRaw)) {
    $bearing = (float)$bearingRaw;
    if ($bearing < 0 || $bearing >= 360) $bearing = fmod($bearing + 360.0, 360.0);
}

try {
    $pdo = db();
    $stmt = $pdo->prepare('SELECT id FROM trak_boxes WHERE trak_id = ? AND api_key = ? LIMIT 1');
    $stmt->execute([$trakId, $apiKey]);
    $trak = $stmt->fetch();
    if (!$trak) positionResponse(401, ['ok' => false, 'error' => 'invalid_credentials']);

    $stmt = $pdo->prepare(
        'INSERT INTO trak_positions (trak_box_id, latitude, longitude, altitude, speed, bearing, gps_timestamp, received_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
         ON CONFLICT(trak_box_id) DO UPDATE SET
            latitude = excluded.latitude,
            longitude = excluded.longitude,
            altitude = excluded.altitude,
            speed = excluded.speed,
            bearing = excluded.bearing,
            gps_timestamp = excluded.gps_timestamp,
            received_at = CURRENT_TIMESTAMP'
    );
    $stmt->execute([(int)$trak['id'], $latitude, $longitude, $altitude, $speed, $bearing, $timestamp !== '' ? $timestamp : null]);

    positionResponse(200, [
        'ok' => true,
        'trak_id' => $trakId,
        'latitude' => $latitude,
        'longitude' => $longitude,
        'altitude' => $altitude,
        'received_at' => gmdate('c'),
    ]);
} catch (Throwable $e) {
    positionResponse(500, ['ok' => false, 'error' => 'server_error']);
}