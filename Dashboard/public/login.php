<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

if (user_count() === 0) { header('Location: register.php'); exit; }
if (current_user()) { header('Location: home.php'); exit; }

$error = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $username = trim((string)($_POST['username'] ?? ''));
    $password = (string)($_POST['password'] ?? '');
    $stmt = db()->prepare('SELECT * FROM users WHERE username = ?');
    $stmt->execute([$username]);
    $user = $stmt->fetch();
    if ($user && password_verify($password, $user['password_hash'])) {
        login_user($user);
        header('Location: home.php');
        exit;
    }
    $error = 'Identifiant ou mot de passe incorrect.';
}
?><!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Connexion — TRAK Connect</title><link rel="stylesheet" href="assets/style.css"></head><body class="auth-page"><main class="auth-card"><div class="brand"><img src="img/logo_light_web.png"></div><h1>Connexion</h1><?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?><form method="post"><input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>"><label>Identifiant<input name="username" autocomplete="username" required autofocus></label><label>Mot de passe<input type="password" name="password" id="login-password" autocomplete="current-password" required></label><label class="password-toggle"><input type="checkbox" id="show-login-password"> Voir le mot de passe</label><button type="submit">Se connecter</button></form><a class="back" href="forgot-password.php">Mot de passe oublié ?</a></main><script>document.getElementById('show-login-password').addEventListener('change',function(){document.getElementById('login-password').type=this.checked?'text':'password';});</script></body></html>