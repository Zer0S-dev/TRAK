<?php
declare(strict_types=1);

require_once __DIR__ . '/api/config.php';
startTrakSession();

try {
    $existingUsers = userCount();
} catch (Throwable $e) {
    http_response_code(500);
    $existingUsers = 0;
    $error = 'Erreur SQLite : ' . $e->getMessage();
}

if ($existingUsers > 0) {
    header('Location: login.php', true, 303);
    exit;
}

$error = $error ?? '';
$username = '';
$email = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim((string) ($_POST['username'] ?? ''));
    $email = trim((string) ($_POST['email'] ?? ''));
    $password = (string) ($_POST['password'] ?? '');
    $password2 = (string) ($_POST['password_confirm'] ?? '');

    if ($password !== $password2) {
        $error = 'Les mots de passe ne correspondent pas.';
    } elseif ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $error = 'Adresse email invalide.';
    } elseif ($username === '' || strlen($username) < 3) {
        $error = 'Le nom utilisateur doit contenir au moins 3 caractères.';
    } elseif (strlen($password) < 8) {
        $error = 'Le mot de passe doit contenir au moins 8 caractères.';
    } else {
        try {
            $pdo = userDatabase();
            if ((int) $pdo->query('SELECT COUNT(*) FROM users')->fetchColumn() > 0) {
                header('Location: login.php', true, 303);
                exit;
            }
            $now = gmdate('c');
            $stmt = $pdo->prepare('INSERT INTO users (username, email, password_hash, created_at, updated_at) VALUES (:username, :email, :password_hash, :created_at, :updated_at)');
            $stmt->execute([
                'username' => $username,
                'email' => $email,
                'password_hash' => password_hash($password, PASSWORD_DEFAULT),
                'created_at' => $now,
                'updated_at' => $now,
            ]);
            header('Location: login.php?created=1', true, 303);
            exit;
        } catch (Throwable $e) {
            $error = 'Impossible de créer le compte. Vérifiez que SQLite est activé et que web_app/storage est accessible en écriture par PHP.';
        }
    }
}
?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<title>TRAK — Créer un compte</title>
<style>
:root{--bg:#f7f8fc;--surface:rgba(255,255,255,.9);--text:#171722;--muted:#747585;--line:#e8e9f0;--accent:#635bff;--danger:#ed3b76}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:Inter,ui-sans-serif,system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text)}
body{min-height:100vh;display:grid;place-items:center;padding:24px}.shell{width:min(100%,460px)}
.card{background:var(--surface);border:1px solid #fff;border-radius:28px;padding:34px;box-shadow:0 24px 70px rgba(38,34,76,.1)}
.brand{text-align:center;margin-bottom:22px}.brand img{width:144px;height:50px;object-fit:contain}.subtitle{text-align:center;color:var(--muted);margin:8px 0 28px}
.field{margin-bottom:16px}.field label{display:block;margin-bottom:8px;font-size:.8rem;font-weight:700;color:#555667}
input{width:100%;height:50px;padding:0 15px;border-radius:14px;border:1px solid var(--line);background:#f8f9fd;color:var(--text);outline:none;font:inherit}
input:focus{border-color:rgba(99,91,255,.45);box-shadow:0 0 0 4px rgba(99,91,255,.08)}
#error{min-height:22px;margin:4px 0 12px;text-align:center;color:var(--danger);font-size:.82rem;font-weight:650}
button{width:100%;height:52px;border:0;border-radius:15px;background:#183029;color:#b1d600;font:inherit;font-weight:800;cursor:pointer}
.note{margin-top:18px;text-align:center;color:var(--muted);font-size:.76rem;line-height:1.5}
</style>
</head>
<body>
<main class="shell">
<section class="card">
<div class="brand"><img src="logo_light_web.png" alt="TRAK"></div>
<h1 style="text-align:center;margin:0;font-size:1.8rem">Créer un compte</h1>
<p class="subtitle">Créez le compte utilisateur de TRAK.</p>
<form method="post" autocomplete="off">
<div class="field"><label for="username">Utilisateur</label><input id="username" name="username" value="<?= htmlspecialchars($username, ENT_QUOTES, 'UTF-8') ?>" autocomplete="username" required autofocus></div>
<div class="field"><label for="email">Email</label><input id="email" name="email" type="email" value="<?= htmlspecialchars($email, ENT_QUOTES, 'UTF-8') ?>" autocomplete="email" placeholder="optionnel"></div>
<div class="field"><label for="password">Mot de passe</label><input id="password" name="password" type="password" autocomplete="new-password" minlength="8" required></div>
<div class="field"><label for="password_confirm">Confirmer le mot de passe</label><input id="password_confirm" name="password_confirm" type="password" autocomplete="new-password" minlength="8" required></div>
<div id="error"><?= htmlspecialchars($error, ENT_QUOTES, 'UTF-8') ?></div>
<button type="submit">S'inscrire</button>
</form>
<div class="note">Les identifiants sont stockés dans SQLite. Le mot de passe n'est jamais enregistré en clair.</div>
</section>
</main>
</body>
</html>
