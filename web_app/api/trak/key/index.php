<?php

declare(strict_types=1);

require_once dirname(__DIR__, 2) . '/config.php';
require_once dirname(__DIR__) . '/crypto.php';

requireDashboardAuth();

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}
requireCsrf();

$trakId = trim((string) ($_POST['trak_id'] ?? ''));
$key = trim((string) ($_POST['encryption_key'] ?? ''));

try {
    saveTrakEncryptionKey($trakId, $key);
    jsonResponse(['ok' => true, 'trak_id' => $trakId]);
} catch (InvalidArgumentException $e) {
    jsonResponse(['ok' => false, 'error' => $e->getMessage()], 422);
} catch (Throwable $e) {
    jsonResponse(['ok' => false, 'error' => 'database_error'], 500);
}
