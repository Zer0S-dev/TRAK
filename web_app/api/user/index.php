<?php
declare(strict_types=1);

require_once dirname(__DIR__) . '/config.php';

requireDashboardAuth();

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    $user = currentUser();
    if ($user === null) jsonResponse(['ok' => false, 'error' => 'user_not_found'], 404);
    jsonResponse([
        'ok' => true,
        'username' => (string) $user['username'],
        'email' => (string) $user['email'],
    ]);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: GET, POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireCsrf();

$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$user = currentUser();
if ($user === null) jsonResponse(['ok' => false, 'error' => 'user_not_found'], 404);

$email = trim((string) ($data['email'] ?? $user['email']));
$currentPassword = (string) ($data['current_password'] ?? '');
$newPassword = (string) ($data['new_password'] ?? '');
$changePassword = $newPassword !== '' || $currentPassword !== '';

if ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) {
    jsonResponse(['ok' => false, 'error' => 'invalid_email'], 422);
}

if ($changePassword) {
    if ($currentPassword === '' || !password_verify($currentPassword, (string) $user['password_hash'])) {
        usleep(250000);
        jsonResponse(['ok' => false, 'error' => 'invalid_current_password'], 422);
    }
    if (strlen($newPassword) < 8) {
        jsonResponse(['ok' => false, 'error' => 'password_too_short'], 422);
    }
}

$pdo = userDatabase();
$now = gmdate('c');

if ($changePassword) {
    $stmt = $pdo->prepare('UPDATE users SET email = :email, password_hash = :password_hash, updated_at = :updated_at WHERE id = :id');
    $stmt->execute([
        'email' => $email,
        'password_hash' => password_hash($newPassword, PASSWORD_DEFAULT),
        'updated_at' => $now,
        'id' => (int) $user['id'],
    ]);
} else {
    $stmt = $pdo->prepare('UPDATE users SET email = :email, updated_at = :updated_at WHERE id = :id');
    $stmt->execute([
        'email' => $email,
        'updated_at' => $now,
        'id' => (int) $user['id'],
    ]);
}

jsonResponse(['ok' => true, 'username' => (string) $user['username'], 'email' => $email]);
