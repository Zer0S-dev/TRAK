<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

$token = trim((string)($_GET['token'] ?? ''));
$message = 'Lien invalide ou expiré.';
if ($token !== '') {
    $stmt = db()->prepare('SELECT id, pending_email FROM users WHERE email_token_hash = ? AND email_token_expires >= ?');
    $stmt->execute([hash('sha256', $token), time()]);
    $user = $stmt->fetch();
    if ($user && !empty($user['pending_email'])) {
        db()->prepare('UPDATE users SET email = pending_email, pending_email = NULL, email_verified_at = CURRENT_TIMESTAMP, email_token_hash = NULL, email_token_expires = NULL WHERE id = ?')->execute([(int)$user['id']]);
        $message = 'Adresse email confirmée. Vous pouvez fermer cette page.';
    }
}
?><!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Confirmation email — TRAK Connect</title><link rel="stylesheet" href="assets/style.css"></head><body class="auth-page"><main class="auth-card"><div class="brand">TRAK <span>Connect</span></div><h1>Confirmation email</h1><p><?=htmlspecialchars($message)?></p><a class="back" href="login.php">Retour à la connexion</a></main></body></html>