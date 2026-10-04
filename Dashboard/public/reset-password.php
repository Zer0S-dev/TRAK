<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

$token = trim((string)($_GET['token'] ?? $_POST['token'] ?? ''));
$error = '';
$success = false;
$valid = false;
$userId = null;
$resetId = null;

if ($token !== '') {
    $stmt = db()->prepare('SELECT id, user_id FROM password_resets WHERE token_hash = ? AND used_at IS NULL AND expires_at >= ?');
    $stmt->execute([hash('sha256', $token), time()]);
    $reset = $stmt->fetch();
    if ($reset) {
        $valid = true;
        $userId = (int)$reset['user_id'];
        $resetId = (int)$reset['id'];
    }
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && $valid) {
    verify_csrf();
    $password = (string)($_POST['password'] ?? '');
    $confirm = (string)($_POST['password_confirm'] ?? '');
    if (strlen($password) < 10) $error = 'Le mot de passe doit contenir au moins 10 caractères.';
    elseif ($password !== $confirm) $error = 'Les mots de passe ne correspondent pas.';
    else {
        $pdo = db();
        $pdo->prepare('UPDATE users SET password_hash = ? WHERE id = ?')->execute([password_hash($password, PASSWORD_DEFAULT), $userId]);
        $pdo->prepare('UPDATE password_resets SET used_at = ? WHERE id = ?')->execute([time(), $resetId]);
        $pdo->prepare('DELETE FROM password_resets WHERE user_id = ? AND id <> ?')->execute([$userId, $resetId]);
        $success = true;
    }
}
?><!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Nouveau mot de passe — TRAK Connect</title><link rel="stylesheet" href="assets/style.css"></head><body class="auth-page"><main class="auth-card"><div class="brand">TRAK <span>Connect</span></div><h1>Nouveau mot de passe</h1><?php if ($success): ?><div class="alert success">Mot de passe modifié. Vous pouvez maintenant vous connecter.</div><a class="back" href="login.php">Se connecter</a><?php elseif (!$valid): ?><div class="alert error">Lien invalide ou expiré.</div><a class="back" href="forgot-password.php">Demander un nouveau lien</a><?php else: ?><?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?><form method="post"><input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>"><input type="hidden" name="token" value="<?=htmlspecialchars($token)?>"><label>Nouveau mot de passe<input type="password" name="password" id="reset-password" minlength="10" autocomplete="new-password" required></label><label>Confirmation<input type="password" name="password_confirm" id="reset-password-confirm" minlength="10" autocomplete="new-password" required></label><label class="password-toggle"><input type="checkbox" id="show-reset-password"> Voir le mot de passe</label><button type="submit">Modifier le mot de passe</button></form><?php endif; ?></main><?php if (!$success && $valid): ?><script>document.getElementById('show-reset-password').addEventListener('change',function(){const type=this.checked?'text':'password';document.getElementById('reset-password').type=type;document.getElementById('reset-password-confirm').type=type;});</script><?php endif; ?></body></html>