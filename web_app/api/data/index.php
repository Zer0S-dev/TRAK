<?php

declare(strict_types=1);

require_once dirname(__DIR__) . '/config.php';
requireDashboardAuth();

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    header('Allow: GET');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

$positions = loadPositions();
$trakId = trim((string) ($_GET['trak_id'] ?? ''));
$position = null;

if ($trakId !== '' && isset($positions[$trakId])) {
    $position = $positions[$trakId];
} elseif ($positions !== []) {
    $values = array_values($positions);
    usort($values, static fn(array $a, array $b): int => strcmp((string) ($b['received_at'] ?? ''), (string) ($a['received_at'] ?? '')));
    $position = $values[0];
}

if ($position === null) {
    jsonResponse([
        'ok' => true, 'online' => false, 'hasFix' => false, 'network' => 'None', 'internetAvailable' => false,
        'signalPercent' => null, 'satellites' => null, 'serialNumber' => null, 'firmwareVersion' => null,
        'txCount' => 0, 'timestamp' => null, 'lastUpdate' => null, 'ageSeconds' => null,
        'latitude' => null, 'longitude' => null, 'altitude' => null,
        'motionMode' => 'IMMOBILE', 'motionReturnSeconds' => 0,
    ]);
}

$receivedAt = (string) ($position['received_at'] ?? '');
$receivedUnix = strtotime($receivedAt);
$age = $receivedUnix === false ? PHP_INT_MAX : max(0, time() - $receivedUnix);
$online = $age <= 30;

// Motion is produced exclusively by the TRAK LSM6DS3 gyro.
// When the firmware starts the fixed 8 s return-to-idle confirmation, it sends
// an immediate position. The server keeps that event as the latest state and
// computes the remaining time from received_at, so the dashboard never gets a
// stale "8 seconds" value while waiting for the next GPS position.
$motionMode = (($position['motionMode'] ?? '') === 'MOBILE') ? 'MOBILE' : 'IMMOBILE';
$motionReturnSeconds = ($motionMode === 'MOBILE') ? max(0, (int) ($position['motionReturnSeconds'] ?? 0)) : 0;

if ($motionMode === 'MOBILE' && $motionReturnSeconds > 0) {
    $elapsedSinceReturnStart = $receivedUnix === false ? 0 : max(0, time() - $receivedUnix);
    $motionReturnSeconds = max(0, $motionReturnSeconds - $elapsedSinceReturnStart);
    if ($motionReturnSeconds === 0) {
        $motionMode = 'IMMOBILE';
    }
}

$network = (string) ($position['network'] ?? 'None');
if ($network !== 'WiFi' && $network !== '4G') {
    $network = 'None';
}

jsonResponse([
    'ok' => true,
    'online' => $online,
    'hasFix' => (bool) ($position['hasFix'] ?? true),
    'latitude' => (float) $position['latitude'],
    'longitude' => (float) $position['longitude'],
    'altitude' => (float) $position['altitude'],
    'speedKmh' => (float) ($position['speed_kmh'] ?? 0),
    'bearing' => (float) ($position['bearing'] ?? 0),
    'timestamp' => $position['timestamp'] ?? null,
    'lastUpdate' => $receivedAt,
    'ageSeconds' => $age,
    'network' => $network,
    'internetAvailable' => $online,
    'signalPercent' => null,
    'satellites' => null,
    'gpsSatellites' => null,
    'glonassSatellites' => null,
    'beidouSatellites' => null,
    'galileoSatellites' => null,
    'serialNumber' => $position['trak_id'],
    'firmwareVersion' => $position['firmware_version'] ?? null,
    'txCount' => (int) ($position['tx_count'] ?? 0),
    'motionMode' => $motionMode,
    'motionReturnSeconds' => $motionReturnSeconds,
    'motionDps' => null,
    'motionXDps' => null,
    'motionYDps' => null,
    'motionZDps' => null,
    'motionStationaryConfirmationRemainingMs' => $motionReturnSeconds * 1000,
    'stationaryConfirmed' => $motionMode === 'IMMOBILE' && $motionReturnSeconds === 0,
    'sentinelState' => 'OFF', 'phoneConfigured' => false, 'sentinelPhoneConfigured' => false, 'trakPhoneConfigured' => false,
    'communicationReady' => $online, 'phone' => '', 'trakPhone' => '', 'sentinelPhone' => '', 'sentinelConfirmationRemainingMs' => 0,
    'wifiProfiles' => [['slot' => 1, 'ssid' => ''], ['slot' => 2, 'ssid' => ''], ['slot' => 3, 'ssid' => '']],
    'wifiBytes' => 0, 'cellularBytes' => 0, 'estimated4GBytes' => 0, 'totalBytes' => 0, 'dataPlanMb' => 0, 'dataCoefficient' => 1,
]);
