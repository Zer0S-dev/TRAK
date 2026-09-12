<?php

declare(strict_types=1);

require_once dirname(__DIR__) . '/config.php';

if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    header('Allow: GET');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireDashboardAuth();
jsonResponse(['ok' => true, 'csrf' => csrfToken(), 'user' => $_SESSION['trak_user'] ?? '']);
