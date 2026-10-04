<?php
declare(strict_types=1);

require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');

if ($user['role'] === 'admin') {
    $stmt = $pdo->query(
        'SELECT tb.trak_id, tp.latitude, tp.longitude, tp.altitude, tp.gps_timestamp, tp.received_at
         FROM trak_boxes tb
         LEFT JOIN trak_positions tp ON tp.trak_box_id = tb.id
         ORDER BY tb.trak_id COLLATE NOCASE'
    );
} else {
    $stmt = $pdo->prepare(
        'SELECT tb.trak_id, tp.latitude, tp.longitude, tp.altitude, tp.gps_timestamp, tp.received_at
         FROM trak_boxes tb
         LEFT JOIN trak_positions tp ON tp.trak_box_id = tb.id
         WHERE tb.user_id = ?
         ORDER BY tb.trak_id COLLATE NOCASE'
    );
    $stmt->execute([(int)$user['id']]);
}

$positions = [];
foreach ($stmt->fetchAll() as $row) {
    if ($row['latitude'] === null || $row['longitude'] === null) {
        continue;
    }
    $positions[] = [
        'trak_id' => (string)$row['trak_id'],
        'latitude' => (float)$row['latitude'],
        'longitude' => (float)$row['longitude'],
        'altitude' => $row['altitude'] !== null ? (float)$row['altitude'] : null,
        'gps_timestamp' => $row['gps_timestamp'] !== null ? (string)$row['gps_timestamp'] : null,
        'received_at' => (string)$row['received_at'],
    ];
}

echo json_encode(['ok' => true, 'positions' => $positions], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
