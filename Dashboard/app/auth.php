<?php
declare(strict_types=1);
require_once __DIR__ . '/db.php';

function user_count(): int {
    return (int) db()->query('SELECT COUNT(*) FROM users')->fetchColumn();
}

function current_user(): ?array {
    static $loaded = false;
    static $user = null;
    if ($loaded) return $user;
    $loaded = true;
    if (empty($_SESSION['user_id'])) return null;
    $stmt = db()->prepare('SELECT id, username, email, phone, email_verified_at, pending_email, role, created_at FROM users WHERE id = ?');
    $stmt->execute([(int) $_SESSION['user_id']]);
    $user = $stmt->fetch() ?: null;
    if (!$user) unset($_SESSION['user_id']);
    return $user;
}

function login_user(array $user): void {
    session_regenerate_id(true);
    $_SESSION['user_id'] = (int) $user['id'];
}

function logout_user(): void {
    $_SESSION = [];
    if (ini_get('session.use_cookies')) {
        $params = session_get_cookie_params();
        setcookie(session_name(), '', time() - 42000, $params['path'], '', $params['secure'], $params['httponly']);
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
