<?php
declare(strict_types=1);

require_once __DIR__ . '/api/config.php';
startTrakSession();

if (empty($_SESSION['trak_authenticated']) || $_SESSION['trak_authenticated'] !== true) {
    header('Location: login.php', true, 303);
    exit;
}
?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<title>TRAK — Paramètre utilisateur</title>
<style>
:root{--bg:#f7f8fc;--surface:#fff;--text:#171722;--muted:#747585;--line:#e8e9f0;--accent:#635bff;--success:#19a974;--danger:#ed3b76}
*{box-sizing:border-box}html,body{margin:0;min-height:100%;font-family:Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}
body{padding:32px 20px}.shell{width:min(100%,720px);margin:0 auto}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:24px}.brand img{width:120px;height:42px;object-fit:contain}.back{display:inline-flex;align-items:center;justify-content:center;height:42px;padding:0 16px;border:1px solid var(--line);border-radius:12px;background:#fff;color:var(--text);text-decoration:none;font-weight:700;font-size:.84rem}
.card{background:var(--surface);border:1px solid #fff;border-radius:24px;padding:28px;box-shadow:0 18px 50px rgba(38,34,76,.08);margin-bottom:18px}.title{margin:0;font-size:1.65rem;letter-spacing:-.035em}.subtitle{margin:7px 0 0;color:var(--muted);font-size:.9rem}.section-title{font-size:.95rem;font-weight:800;margin-bottom:18px}.field{margin-bottom:16px}.field label{display:block;margin-bottom:8px;font-size:.8rem;font-weight:700;color:#555667}input{width:100%;height:48px;padding:0 14px;border-radius:12px;border:1px solid var(--line);background:#f8f9fd;color:var(--text);outline:none;font:inherit}input:focus{border-color:rgba(99,91,255,.4);box-shadow:0 0 0 4px rgba(99,91,255,.08)}input[readonly]{background:#f1f2f6;color:#777887}.hint{color:var(--muted);font-size:.75rem;margin-top:7px}.actions{display:flex;justify-content:flex-end;gap:10px;margin-top:20px}button{height:46px;padding:0 18px;border:0;border-radius:12px;background:#183029;color:#b1d600;font:inherit;font-weight:800;cursor:pointer}.message{min-height:20px;margin-top:14px;font-size:.82rem;font-weight:700}.message.success{color:var(--success)}.message.error{color:var(--danger)}.danger-note{margin-top:10px;color:var(--muted);font-size:.76rem}
@media(max-width:560px){body{padding:18px 14px}.card{padding:22px}.top{align-items:flex-start}.brand img{width:105px}.back{height:38px}}
</style>
</head>
<body>
<main class="shell">
<header class="top"><div class="brand"><img src="logo_light_web.png" alt="TRAK"></div><a class="back" href="index.php">← Retour au dashboard</a></header>
<section class="card">
<h1 class="title">Paramètre utilisateur</h1>
<p class="subtitle">Gérez les informations de votre compte TRAK.</p>
</section>
<section class="card">
<div class="section-title">Informations du compte</div>
<div class="field"><label for="username">Utilisateur</label><input id="username" type="text" readonly></div>
<div class="field"><label for="email">Email</label><input id="email" type="email" autocomplete="email" placeholder="optionnel"><div class="hint">Utilisé comme adresse de contact du compte.</div></div>
<div class="actions"><button type="button" onclick="saveEmail()">Enregistrer l'email</button></div>
<div id="emailMessage" class="message" role="status"></div>
</section>
<section class="card">
<div class="section-title">Changer le mot de passe</div>
<div class="field"><label for="currentPassword">Mot de passe actuel</label><input id="currentPassword" type="password" autocomplete="current-password"></div>
<div class="field"><label for="newPassword">Nouveau mot de passe</label><input id="newPassword" type="password" autocomplete="new-password" minlength="8"><div class="hint">Minimum 8 caractères.</div></div>
<div class="field"><label for="confirmPassword">Confirmer le nouveau mot de passe</label><input id="confirmPassword" type="password" autocomplete="new-password" minlength="8"></div>
<div class="actions"><button type="button" onclick="changePassword()">Changer le mot de passe</button></div>
<div id="passwordMessage" class="message" role="status"></div>
<div class="danger-note">Après modification, reconnectez-vous avec le nouveau mot de passe.</div>
</section>
</main>
<script>
let csrf = '';

function message(id, text, ok) {
    const el = document.getElementById(id);
    el.textContent = text || '';
    el.className = 'message ' + (ok ? 'success' : 'error');
}

async function api(url, options = {}) {
    const response = await fetch(url, {cache:'no-store', ...options});
    if (response.status === 401) {
        window.location.replace('login.php');
        throw new Error('unauthorized');
    }
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || ('HTTP ' + response.status));
    return data;
}

async function load() {
    const session = await api('api/session/');
    csrf = session.csrf || '';
    const data = await api('api/user/');
    document.getElementById('username').value = data.username || '';
    document.getElementById('email').value = data.email || '';
}

async function saveEmail() {
    try {
        const email = document.getElementById('email').value.trim();
        const data = await api('api/user/', {method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},body:JSON.stringify({email})});
        document.getElementById('email').value = data.email || '';
        message('emailMessage','Email enregistré.',true);
    } catch (e) {
        message('emailMessage', e.message === 'invalid_email' ? 'Adresse email invalide.' : 'Impossible d’enregistrer l’email.',false);
    }
}

async function changePassword() {
    const currentPassword = document.getElementById('currentPassword').value;
    const newPassword = document.getElementById('newPassword').value;
    const confirmPassword = document.getElementById('confirmPassword').value;
    if (newPassword !== confirmPassword) {
        message('passwordMessage','Les mots de passe ne correspondent pas.',false);
        return;
    }
    if (newPassword.length < 8) {
        message('passwordMessage','Le nouveau mot de passe doit contenir au moins 8 caractères.',false);
        return;
    }
    try {
        await api('api/user/', {method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},body:JSON.stringify({current_password:currentPassword,new_password:newPassword})});
        document.getElementById('currentPassword').value = '';
        document.getElementById('newPassword').value = '';
        document.getElementById('confirmPassword').value = '';
        message('passwordMessage','Mot de passe modifié. Vous pouvez vous reconnecter.',true);
    } catch (e) {
        const labels = {
            invalid_current_password:'Mot de passe actuel incorrect.',
            password_too_short:'Le nouveau mot de passe est trop court.'
        };
        message('passwordMessage', labels[e.message] || 'Impossible de modifier le mot de passe.',false);
    }
}

load().catch(() => {});
</script>
</body>
</html>
