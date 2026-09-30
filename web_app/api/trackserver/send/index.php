<?php

declare(strict_types=1);

require_once dirname(__DIR__, 2) . '/config.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireDashboardAuth();
requireCsrf();

$positions = loadPositions();
if ($positions === []) jsonResponse(['ok' => false, 'error' => 'no_position'], 404);

// The prototype dashboard controls one active TRAK: use the most recently
// received position rather than trusting a tracker id supplied by the browser.
$latest = null;
foreach ($positions as $position) {
    if (!is_array($position)) continue;
    if ($latest === null || strtotime((string) ($position['received_at'] ?? '')) > strtotime((string) ($latest['received_at'] ?? ''))) {
        $latest = $position;
    }
}
if ($latest === null) jsonResponse(['ok' => false, 'error' => 'no_position'], 404);

$relay = sendToTrackserver($latest);
if (!$relay['configured']) jsonResponse(['ok' => false, 'error' => 'trackserver_not_configured'], 409);

jsonResponse([
    'ok' => $relay['ok'],
    'position' => $latest,
    'trackserver' => [
        'ok' => $relay['ok'],
        'status' => $relay['status'],
    ],
], $relay['ok'] ? 200 : 502);
