<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';
require_once __DIR__ . '/../app/mail.php';

$count = user_count();
$wizardUserId = (int)($_SESSION['wizard_user_id'] ?? 0);
$wizardTrakStep = $wizardUserId > 0;

if ($count > 0) require_admin();

$error = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();

    if ($wizardTrakStep) {
        $trakId = strtoupper(trim((string)($_POST['trak_id'] ?? '')));
        $trakPhone = trim((string)($_POST['trak_phone'] ?? ''));
        $trakServerUrl = trim((string)($_POST['trakserver_url'] ?? ''));
        $dashboardUrl = trim((string)($_POST['dashboard_url'] ?? ''));
        $apiKey = trim((string)($_POST['api_key'] ?? ''));

        if (!preg_match('/^[A-Z0-9_-]{1,5}$/', $trakId)) $error = 'ID TRAK invalide.';
        elseif ($trakPhone === '' || strlen($trakPhone) > 32) $error = 'Numéro de téléphone TRAK invalide.';
        elseif (strlen($trakServerUrl) > 160 || !filter_var($trakServerUrl, FILTER_VALIDATE_URL) || !str_starts_with(strtolower($trakServerUrl), 'https://')) $error = 'trackserver_url doit être une URL HTTPS valide de 160 caractères maximum.';
        elseif (strlen($dashboardUrl) > 160 || !filter_var($dashboardUrl, FILTER_VALIDATE_URL) || !str_starts_with(strtolower($dashboardUrl), 'https://')) $error = 'L’URL du Dashboard doit être une URL HTTPS valide de 160 caractères maximum.';
        elseif ($apiKey !== '' && !preg_match('/^[A-Za-z0-9]{16}$/', $apiKey)) $error = 'La clé API doit contenir exactement 16 caractères alphanumériques.';
        else {
            try {
                $pdo = db();
                $apiKey = $apiKey !== '' ? $apiKey : bin2hex(random_bytes(8));
                $stmt = $pdo->prepare('INSERT INTO trak_boxes (user_id, trak_id, phone, api_key, trakserver_url, dashboard_url) VALUES (?, ?, ?, ?, ?, ?)');
                $stmt->execute([$wizardUserId, $trakId, $trakPhone, $apiKey, $trakServerUrl, $dashboardUrl]);
                unset($_SESSION['wizard_user_id']);
                header('Location: home.php?wizard=complete');
                exit;
            } catch (PDOException $e) {
                $error = ((int)($e->errorInfo[1] ?? 0) === 19) ? 'Cet ID TRAK existe déjà.' : 'Impossible d’enregistrer le TRAK Box.';
            }
        }
    } else {
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
                $pdo = db();
                $stmt = $pdo->prepare('INSERT INTO users (username, password_hash, email, phone, role) VALUES (?, ?, ?, ?, ?)');
                $stmt->execute([$username, password_hash($password, PASSWORD_DEFAULT), $email, $phone, $role]);
                $id = (int)$pdo->lastInsertId();

                $mailSent = false;
                if ($email !== '') {
                    $token = bin2hex(random_bytes(32));
                    $stmt = $pdo->prepare('UPDATE users SET pending_email = ?, email = NULL, email_token_hash = ?, email_token_expires = ? WHERE id = ?');
                    $stmt->execute([$email, hash('sha256', $token), time() + 86400, $id]);
                    $mailSent = send_email_verification($email, $token);
                    if ($mailSent) {
                        send_user_created_email($email, $username);
                    }
                }

                if ($count === 0) {
                    login_user(['id'=>$id]);
                    $_SESSION['wizard_user_id'] = $id;
                    header('Location: register.php');
                    exit;
                }

                header('Location: account.php?created=1&mail=' . ($mailSent ? 'sent' : 'error'));
                exit;
            } catch (PDOException $e) {
                $error = ((int)($e->errorInfo[1] ?? 0) === 19) ? 'Cet identifiant existe déjà.' : 'Impossible de créer le compte.';
            }
        }
    }
}
?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title><?= $wizardTrakStep ? 'Wizard — Premier TRAK' : ($count === 0 ? 'Créer le compte administrateur' : 'Ajouter un utilisateur') ?> — TRAK Connect</title>
<link rel="stylesheet" href="assets/style.css">
</head>
<body class="auth-page">
<main class="auth-card">
    <div class="brand"><img src="img/logo_dark_web.png"></div>

    <?php if ($wizardTrakStep): ?>
        <h1>🧙‍♂️ Wizard</h1>
        <p class="muted">Compte créé. Configurez maintenant votre premier TRAK Box pour terminer l’installation.</p>
        <?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?>
        <form method="post">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <label>ID TRAK<input name="trak_id" maxlength="5" value="<?=htmlspecialchars((string)($_POST['trak_id'] ?? 'TRK01'))?>" required autofocus></label>
            <label>Téléphone TRAK<input type="tel" name="trak_phone" maxlength="32" value="<?=htmlspecialchars((string)($_POST['trak_phone'] ?? ''))?>" required></label>
            <label>Trackserver WP plugin url<input type="url" name="trakserver_url" maxlength="160" value="<?=htmlspecialchars((string)($_POST['trakserver_url'] ?? ''))?>" placeholder="https://..." required></label>
            <label>URL Dashboard / réception position
                <input type="url" name="dashboard_url" id="dashboard-url" maxlength="160" value="<?=htmlspecialchars((string)($_POST['dashboard_url'] ?? ''))?>" placeholder="https://.../position.php" required>
            </label>
            <label class="new_key_api">Clé API<input type="text" name="api_key" maxlength="16" pattern="[A-Za-z0-9]{16}" value="<?=htmlspecialchars((string)($_POST['api_key'] ?? ''))?>" placeholder="Laisser vide pour générer automatiquement"></label>
            <button type="submit">Enregistrer le TRAK</button>
        </form>
    <?php else: ?>
        <h1><?= $count === 0 ? '🧙‍♂️ Wizard' : 'Nouvel utilisateur' ?></h1>
        <p class="muted"><?= $count === 0 ? 'Créez votre compte administrateur pour commencer.' : 'Seul un administrateur peut créer un utilisateur.' ?></p>
        <?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?>
        <form method="post">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <label>Identifiant<input name="username" minlength="3" maxlength="64" autocomplete="username" required autofocus></label>
            <label>Email<input type="email" name="email" maxlength="254" autocomplete="email"></label>
            <label>Téléphone<input type="tel" name="phone" maxlength="32" autocomplete="tel"></label>
            <label>Mot de passe<input type="password" name="password" id="register-password" minlength="10" autocomplete="new-password" required></label>
            <label>Confirmation<input type="password" name="password_confirm" id="register-password-confirm" minlength="10" autocomplete="new-password" required></label>
            <label class="password-toggle"><input type="checkbox" id="show-register-password"> Voir le mot de passe</label>
            <button type="submit"><?= $count === 0 ? 'Créer le compte' : 'Créer l’utilisateur' ?></button>
        </form>
        <?php if ($count > 0): ?><a class="back" href="home.php">< Retour au dashboard</a><?php endif; ?>
    <?php endif; ?>
</main>
<?php if ($wizardTrakStep): ?>
<script>
(() => {
  const input = document.getElementById('dashboard-url');
  if (!input || input.value.trim()) return;
  const url = new URL('position.php', window.location.href);
  input.value = url.href;
})();
</script>
<?php else: ?>
<script>document.getElementById('show-register-password').addEventListener('change',function(){const type=this.checked?'text':'password';document.getElementById('register-password').type=type;document.getElementById('register-password-confirm').type=type;});</script>
<?php endif; ?>
</body>
</html>
