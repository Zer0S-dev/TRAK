<?php
declare(strict_types=1);

const APP_NAME = 'TRAK Connect';
const DB_PATH = __DIR__ . '/../storage/trak.sqlite';

if (session_status() !== PHP_SESSION_ACTIVE) {
    $secure = isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';
    session_set_cookie_params([
        'httponly' => true,
        'secure' => $secure,
        'samesite' => 'Lax',
        'path' => '/',
    ]);
    session_start();
}
