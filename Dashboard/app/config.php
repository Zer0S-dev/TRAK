<?php
declare(strict_types=1);

const APP_NAME = 'TRAK Connect';
const DB_PATH = __DIR__ . '/../storage/trak.sqlite';

function app_url(): string {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || (isset($_SERVER['SERVER_PORT']) && (int)$_SERVER['SERVER_PORT'] === 443);
    $scheme = $https ? 'https' : 'http';
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';
    return $scheme . '://' . $host;
}

function mail_from(): string {
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';
    $host = preg_replace('/:d+$/', '', $host);
    return 'noreply@' . $host;
}

const MAIL_FROM_NAME = 'TRAK Connect';

if (session_status() !== PHP_SESSION_ACTIVE) {
    $secure = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || ((int)($_SERVER['SERVER_PORT'] ?? 0) === 443);

    // La session PHP peut expirer côté serveur. La reconnexion est assurée
    // automatiquement par le cookie TRAK_REMEMBER stocké dans SQLite.
    session_set_cookie_params([
        'lifetime' => 0,
        'httponly' => true,
        'secure' => $secure,
        'samesite' => 'Lax',
        'path' => '/',
    ]);

    session_start();
}
