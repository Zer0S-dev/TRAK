<?php

declare(strict_types=1);

require_once __DIR__ . '/api/config.php';
startTrakSession();

$_SESSION = [];
$params = session_get_cookie_params();
if (ini_get('session.use_cookies')) {
    setcookie(session_name(), '', time() - 42000, $params['path'], $params['domain'] ?? '', (bool) $params['secure'], (bool) $params['httponly']);
}
session_destroy();

header('Location: login.php', true, 303);
exit;
