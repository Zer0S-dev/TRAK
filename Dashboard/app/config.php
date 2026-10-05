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
    /*
     * Session persistante pour le WebView Android.
     *
     * Le cookie reste présent après fermeture de l'application et
     * redémarrage du téléphone. Il est supprimé par la déconnexion
     * ou lorsque les données/cookies du WebView sont effacés.
     */
    $sessionLifetime = 10 * 365 * 24 * 60 * 60;
    $secure = isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off';

    ini_set('session.gc_maxlifetime', (string) $sessionLifetime);
    ini_set('session.cookie_lifetime', (string) $sessionLifetime);

    session_set_cookie_params([
        'lifetime' => $sessionLifetime,
        'expires' => time() + $sessionLifetime,
        'httponly' => true,
        'secure' => $secure,
        'samesite' => 'Lax',
        'path' => '/',
    ]);

    session_start();
}
