<?php
declare(strict_types=1);

const APP_NAME = 'TRAK Connect';
const DB_PATH = __DIR__ . '/../storage/trak.sqlite';

/*
 * Détection automatique du serveur.
 * L'URL et l'adresse d'expédition sont générées à partir du domaine
 * sur lequel le dashboard est actuellement exécuté.
 */
function app_url(): string {
    $https = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || (isset($_SERVER['SERVER_PORT']) && (int)$_SERVER['SERVER_PORT'] === 443);

    $scheme = $https ? 'https' : 'http';
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';

    return $scheme . '://' . $host;
}

function mail_from(): string {
    $host = $_SERVER['HTTP_HOST'] ?? 'localhost';
    $host = preg_replace('/:\d+$/', '', $host);

    return 'noreply@' . $host;
}

const MAIL_FROM_NAME = 'TRAK Connect';

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
