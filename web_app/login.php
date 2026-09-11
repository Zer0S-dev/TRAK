<?php

declare(strict_types=1);

require_once __DIR__ . '/api/config.php';
startTrakSession();

if (!empty($_SESSION['trak_authenticated']) && $_SESSION['trak_authenticated'] === true) {
    header('Location: index.php', true, 303);
    exit;
}

$error = '';
$username = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $username = trim((string) ($_POST['username'] ?? ''));
    $password = (string) ($_POST['password'] ?? '');
    $configuredUser = dashboardUser();
    $passwordHash = dashboardPasswordHash();

    if ($configuredUser === '' || $passwordHash === '') {
        $error = 'Authentification non configurée sur le serveur.';
    } elseif (hash_equals($configuredUser, $username) && password_verify($password, $passwordHash)) {
        session_regenerate_id(true);
        $_SESSION['trak_authenticated'] = true;
        $_SESSION['trak_user'] = $configuredUser;
        $_SESSION['trak_csrf'] = bin2hex(random_bytes(32));
        header('Location: index.php', true, 303);
        exit;
    } else {
        usleep(250000);
        $error = 'Identifiant ou mot de passe incorrect.';
    }
}
?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#f7f8fc">
<meta name="color-scheme" content="light">
<title>TRAK — Connexion</title>
<style>
:root{--bg:#f7f8fc;--surface:rgba(255,255,255,.88);--text:#171722;--muted:#747585;--line:#e8e9f0;--accent:#635bff;--cyan:#19c7d8;--danger:#ed3b76;--shadow:0 24px 70px rgba(38,34,76,.10),0 4px 16px rgba(38,34,76,.06);--ease:cubic-bezier(.22,1,.36,1)}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}body{min-height:100vh;display:grid;place-items:center;padding:24px;overflow:hidden}body:before,body:after{content:"";position:fixed;pointer-events:none;filter:blur(10px);border-radius:999px}body:before{width:520px;height:520px;left:-190px;top:-190px;background:radial-gradient(circle,rgba(99,91,255,.18),transparent 68%)}body:after{width:480px;height:480px;right:-190px;bottom:-210px;background:radial-gradient(circle,rgba(25,199,216,.14),transparent 68%)}.shell{position:relative;width:min(100%,430px);animation:enter .7s var(--ease) both}.login{position:relative;background:var(--surface);border:1px solid rgba(255,255,255,.95);border-radius:28px;padding:34px;box-shadow:var(--shadow);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px)}.login:before{content:"";position:absolute;inset:0;border-radius:inherit;background:linear-gradient(135deg,rgba(255,255,255,.72),transparent 42%);pointer-events:none}.brand{position:relative;display:flex;justify-content:center;margin:0 0 24px}.brand img{display:block;width:144px;height:50px;object-fit:contain}.subtitle{position:relative;text-align:center;color:var(--muted);margin:10px 0 30px;font-size:.92rem}.field{position:relative;margin-bottom:17px}.field label{display:block;margin:0 0 8px;font-size:.79rem;font-weight:700;color:#555667}.input-wrap{position:relative}.input-wrap:after{content:"";position:absolute;left:14px;right:14px;bottom:0;height:2px;border-radius:2px;background:linear-gradient(90deg,var(--accent),var(--cyan));transform:scaleX(0);transform-origin:center;transition:transform .38s var(--ease)}.input-wrap:focus-within:after{transform:scaleX(1)}input{width:100%;height:50px;padding:0 15px;border-radius:14px;border:1px solid var(--line);background:rgba(248,249,253,.88);color:var(--text);outline:none;font:inherit;transition:border-color .25s var(--ease),box-shadow .25s var(--ease),background .25s var(--ease),transform .25s var(--ease)}input:hover{background:#fff;border-color:#dedfea}input:focus{background:#fff;border-color:rgba(99,91,255,.35);box-shadow:0 8px 24px rgba(99,91,255,.09),0 0 0 4px rgba(99,91,255,.08);transform:translateY(-1px)}.password-row{display:flex;align-items:center;gap:9px;margin:2px 0 21px;color:var(--muted);font-size:.8rem}.password-row input{width:17px;height:17px;margin:0;accent-color:var(--accent);cursor:pointer}.password-row label{cursor:pointer}#error{min-height:22px;margin:-2px 0 10px;padding:0 4px;text-align:center;color:var(--danger);font-size:.82rem;font-weight:650}button{position:relative;overflow:hidden;width:100%;height:52px;padding:0 16px;border:0;border-radius:15px;background:#183029;color:#b1d600;font:inherit;font-weight:800;cursor:pointer;box-shadow:0 12px 24px rgba(99,91,255,.22);transition:transform .2s var(--ease),box-shadow .25s var(--ease),filter .25s var(--ease)}button:hover{transform:translateY(-2px);box-shadow:0 16px 30px rgba(99,91,255,.27);filter:saturate(1.05)}button:active{transform:translateY(1px) scale(.985)}.footer{position:relative;text-align:center;color:#9899a8;font-size:.72rem;margin-top:19px}.status-dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:#2bd576;box-shadow:0 0 0 4px rgba(43,213,118,.1);margin-right:7px;vertical-align:middle}@keyframes enter{from{opacity:0;transform:translateY(18px) scale(.985)}to{opacity:1;transform:none}}@media(max-width:520px){body{padding:16px}.login{padding:28px 22px;border-radius:24px}.shell{width:100%}}@media(prefers-reduced-motion:reduce){*,*:before,*:after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important;scroll-behavior:auto!important}}
</style>
</head>
<body>
<main class="shell">
<section class="login" aria-labelledby="loginTitle">
<div class="brand"><img src="logo_light_web.png" alt="TRAK"></div>
<h1 id="loginTitle" style="position:relative;text-align:center;margin:0;font-size:clamp(1.7rem,5vw,2rem);letter-spacing:-.045em;line-height:1.05">Connexion</h1>
<p class="subtitle">Connectez-vous à votre espace de suivi.</p>
<form method="post" autocomplete="on">
<div class="field"><label for="username">Utilisateur</label><div class="input-wrap"><input id="username" name="username" type="text" value="<?= htmlspecialchars($username, ENT_QUOTES, 'UTF-8') ?>" autocomplete="username" required autofocus></div></div>
<div class="field"><label for="password">Mot de passe</label><div class="input-wrap"><input id="password" name="password" type="password" autocomplete="current-password" required></div></div>
<div class="password-row"><input id="showPassword" type="checkbox"><label for="showPassword">Afficher le mot de passe</label></div>
<div id="error" role="alert" aria-live="polite"><?= htmlspecialchars($error, ENT_QUOTES, 'UTF-8') ?></div>
<button type="submit">Se connecter</button>
</form>
</section>
<div class="footer"><span class="status-dot"></span>TRAK · Connexion sécurisée</div>
</main>
<script>document.getElementById('showPassword').addEventListener('change',function(){document.getElementById('password').type=this.checked?'text':'password';});</script>
</body>
</html>
