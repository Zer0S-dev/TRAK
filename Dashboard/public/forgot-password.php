<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';
require_once __DIR__ . '/../app/mail.php';

if (current_user()) { header('Location: home.php'); exit; }
$message = 'Si un compte avec cette adresse existe, un email de récupération vient d’être envoyé.';
$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $email = trim((string)($_POST['email'] ?? ''));
    if (!filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $error = 'Adresse email invalide.';
    } else {
        $stmt = db()->prepare('SELECT id, email FROM users WHERE email = ? AND email_verified_at IS NOT NULL LIMIT 1');
        $stmt->execute([$email]);
        $user = $stmt->fetch();
        if ($user) {
            db()->prepare('DELETE FROM password_resets WHERE user_id = ? OR expires_at < ?')->execute([(int)$user['id'], time()]);
            $token = bin2hex(random_bytes(32));
            db()->prepare('INSERT INTO password_resets (user_id, token_hash, expires_at, created_at) VALUES (?, ?, ?, ?)')->execute([(int)$user['id'], hash('sha256', $token), time() + 3600, time()]);
            send_password_reset($email, $token);
        }
    }
}
?><!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Mot de passe oublié — TRAK Connect</title><link rel="stylesheet" href="assets/style.css"></head><body class="auth-page"><main class="auth-card"><div class="brand">TRAK <span>Connect</span></div><h1>Mot de passe oublié</h1><?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?><p class="muted"><?=htmlspecialchars($message)?></p><form method="post"><input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>"><label>Email<input type="email" name="email" autocomplete="email" required autofocus></label><button type="submit">Envoyer le lien</button></form><a class="back" href="login.php">Retour à la connexion</a></main></body></html>