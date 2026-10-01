<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

$count = user_count();
if ($count > 0) require_admin();

$error = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $username = trim((string)($_POST['username'] ?? ''));
    $password = (string)($_POST['password'] ?? '');
    $confirm = (string)($_POST['password_confirm'] ?? '');
    $email = trim((string)($_POST['email'] ?? ''));
    $phone = trim((string)($_POST['phone'] ?? ''));

    if (!preg_match('/^[A-Za-z0-9_.-]{3,64}$/', $username)) $error = 'Identifiant invalide (3 à 64 caractères).';
    elseif (strlen($password) < 10) $error = 'Le mot de passe doit contenir au moins 10 caractères.';
    elseif ($password !== $confirm) $error = 'Les mots de passe ne correspondent pas.';
    elseif ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) $error = 'Adresse email invalide.';
    elseif (strlen($email) > 254) $error = 'Adresse email trop longue.';
    elseif (strlen($phone) > 32) $error = 'Numéro de téléphone trop long.';
    else {
        try {
            $role = ($count === 0) ? 'admin' : 'user';
            $stmt = db()->prepare('INSERT INTO users (username, password_hash, email, phone, role) VALUES (?, ?, ?, ?, ?)');
            $stmt->execute([$username, password_hash($password, PASSWORD_DEFAULT), $email !== '' ? $email : null, $phone !== '' ? $phone : null, $role]);
            if ($count === 0) {
                $id = (int)db()->lastInsertId();
                login_user(['id'=>$id]);
                header('Location: home.php'); exit;
            }
            header('Location: account.php?created=1'); exit;
        } catch (PDOException $e) {
            $error = ((int)$e->errorInfo[1] === 19) ? 'Cet identifiant existe déjà.' : 'Impossible de créer le compte.';
        }
    }
}
?><!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title><?= $count === 0 ? 'Créer le compte administrateur' : 'Ajouter un utilisateur' ?> — TRAK Connect</title><link rel="stylesheet" href="assets/style.css"></head><body class="auth-page"><main class="auth-card"><div class="brand">TRAK <span>Connect</span></div><h1><?= $count === 0 ? 'Initialisation' : 'Nouvel utilisateur' ?></h1><p class="muted"><?= $count === 0 ? 'Aucun compte n’existe. Le premier compte sera administrateur.' : 'Seul un administrateur peut créer un utilisateur.' ?></p><?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?><form method="post"><input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>"><label>Identifiant<input name="username" minlength="3" maxlength="64" autocomplete="username" required autofocus></label><label>Email<input type="email" name="email" maxlength="254" autocomplete="email"></label><label>Téléphone<input type="tel" name="phone" maxlength="32" autocomplete="tel"></label><label>Mot de passe<input type="password" name="password" id="register-password" minlength="10" autocomplete="new-password" required></label><label>Confirmation<input type="password" name="password_confirm" id="register-password-confirm" minlength="10" autocomplete="new-password" required></label><label class="password-toggle"><input type="checkbox" id="show-register-password"> Voir le mot de passe</label><button type="submit"><?= $count === 0 ? 'Créer l’administrateur' : 'Créer l’utilisateur' ?></button></form><?php if ($count > 0): ?><a class="back" href="home.php">Retour au dashboard</a><?php endif; ?></main><script>document.getElementById('show-register-password').addEventListener('change',function(){const type=this.checked?'text':'password';document.getElementById('register-password').type=type;document.getElementById('register-password-confirm').type=type;});</script></body></html>