<?php
declare(strict_types=1);
require_once __DIR__ . '/db.php';

const REMEMBER_COOKIE = 'TRAK_REMEMBER';
const REMEMBER_LIFETIME = 365 * 24 * 60 * 60;

function user_count(): int {
    return (int) db()->query('SELECT COUNT(*) FROM users')->fetchColumn();
}

function remember_cookie_options(int $expires): array {
    $secure = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off')
        || ((int)($_SERVER['SERVER_PORT'] ?? 0) === 443);

    return [
        'expires' => $expires,
        'path' => '/',
        'secure' => $secure,
        'httponly' => true,
        'samesite' => 'Lax',
    ];
}

function clear_remember_cookie(): void {
    setcookie(REMEMBER_COOKIE, '', remember_cookie_options(time() - 3600));
    unset($_COOKIE[REMEMBER_COOKIE]);
}

function create_remember_token(int $userId): void {
    $token = bin2hex(random_bytes(32));
    $hash = hash('sha256', $token);
    $now = time();
    $expires = $now + REMEMBER_LIFETIME;

    db()->prepare('INSERT INTO remember_tokens (user_id, token_hash, expires_at, created_at, last_used_at) VALUES (?, ?, ?, ?, ?)')
        ->execute([$userId, $hash, $expires, $now, $now]);

    setcookie(REMEMBER_COOKIE, $token, remember_cookie_options($expires));
    $_COOKIE[REMEMBER_COOKIE] = $token;
}

function restore_remembered_user(): void {
    if (!empty($_SESSION['user_id'])) return;

    $token = (string)($_COOKIE[REMEMBER_COOKIE] ?? '');
    if (!preg_match('/^[a-f0-9]{64}$/', $token)) return;

    $stmt = db()->prepare(
        'SELECT rt.id, rt.user_id
         FROM remember_tokens rt
         INNER JOIN users u ON u.id = rt.user_id
         WHERE rt.token_hash = ? AND rt.expires_at >= ?'
    );
    $stmt->execute([hash('sha256', $token), time()]);
    $row = $stmt->fetch();

    if (!$row) {
        clear_remember_cookie();
        return;
    }

    db()->prepare('DELETE FROM remember_tokens WHERE id = ?')->execute([(int)$row['id']]);

    session_regenerate_id(true);
    $_SESSION['user_id'] = (int)$row['user_id'];
    create_remember_token((int)$row['user_id']);
}

function current_user(): ?array {
    static $loaded = false;
    static $user = null;

    if ($loaded) return $user;
    $loaded = true;

    restore_remembered_user();

    if (empty($_SESSION['user_id'])) return null;

    $stmt = db()->prepare('SELECT id, username, email, phone, email_verified_at, pending_email, role, created_at FROM users WHERE id = ?');
    $stmt->execute([(int) $_SESSION['user_id']]);
    $user = $stmt->fetch() ?: null;

    if (!$user) {
        unset($_SESSION['user_id']);
        clear_remember_cookie();
    }

    return $user;
}

function login_user(array $user): void {
    session_regenerate_id(true);
    $_SESSION['user_id'] = (int) $user['id'];
    create_remember_token((int) $user['id']);
}

function logout_user(): void {
    $token = (string)($_COOKIE[REMEMBER_COOKIE] ?? '');
    if (preg_match('/^[a-f0-9]{64}$/', $token)) {
        db()->prepare('DELETE FROM remember_tokens WHERE token_hash = ?')
            ->execute([hash('sha256', $token)]);
    }

    clear_remember_cookie();
    $_SESSION = [];

    if (ini_get('session.use_cookies')) {
        $params = session_get_cookie_params();
        setcookie(session_name(), '', [
            'expires' => time() - 42000,
            'path' => $params['path'] ?? '/',
            'domain' => $params['domain'] ?? '',
            'secure' => (bool)($params['secure'] ?? false),
            'httponly' => (bool)($params['httponly'] ?? true),
            'samesite' => $params['samesite'] ?? 'Lax',
        ]);
    }

    session_destroy();
}

function require_login(): array {
    $user = current_user();
    if (!$user) {
        header('Location: login.php');
        exit;
    }
    return $user;
}

function require_admin(): array {
    $user = require_login();
    if ($user['role'] !== 'admin') {
        http_response_code(403);
        exit('Accès administrateur requis.');
    }
    return $user;
}

function csrf_token(): string {
    if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(32));
    return $_SESSION['csrf'];
}

function verify_csrf(): void {
    if (!hash_equals($_SESSION['csrf'] ?? '', $_POST['csrf'] ?? '')) {
        http_response_code(400);
        exit('Requête invalide.');
    }
}
