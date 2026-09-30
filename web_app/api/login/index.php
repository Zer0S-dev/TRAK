<?php

declare(strict_types=1);

require_once dirname(__DIR__) . '/config.php';

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}


$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$providedUser = trim((string) ($data['username'] ?? ''));
$providedPassword = (string) ($data['password'] ?? '');
$user = findUserByUsername($providedUser);
$validPassword = $user !== null && password_verify($providedPassword, (string) $user['password_hash']);

if ($user === null || !$validPassword) {
    usleep(250000);
    jsonResponse(['ok' => false, 'error' => 'invalid_credentials'], 401);
}

startTrakSession();
session_regenerate_id(true);
$_SESSION['trak_authenticated'] = true;
$_SESSION['trak_user'] = (string) $user['username'];
$_SESSION['trak_csrf'] = bin2hex(random_bytes(32));

jsonResponse(['ok' => true, 'csrf' => $_SESSION['trak_csrf']]);
