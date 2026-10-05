<?php
declare(strict_types=1);
require_once __DIR__.'/partials.php';
require_once __DIR__.'/../app/mail.php';

$user = require_login();
$message = '';
$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $email = trim((string)($_POST['email'] ?? ''));
    $phone = trim((string)($_POST['phone'] ?? ''));

    if ($phone === '') {
        $error = 'Le numéro de téléphone est obligatoire pour recevoir les SMS de configuration TRAK.';
    } elseif ($email !== '' && !filter_var($email, FILTER_VALIDATE_EMAIL)) {
        $error = 'Adresse email invalide.';
    } elseif (strlen($email) > 254) {
        $error = 'Adresse email trop longue.';
    } elseif (strlen($phone) > 32) {
        $error = 'Numéro de téléphone trop long.';
    } else {
        $pdo = db();
        $stmt = $pdo->prepare('UPDATE users SET phone = ? WHERE id = ?');
        $stmt->execute([$phone !== '' ? $phone : null, (int)$user['id']]);

        if ($email !== ($user['email'] ?? '')) {
            if ($email === '') {
                $pdo->prepare('UPDATE users SET email = NULL, email_verified_at = NULL, pending_email = NULL, email_token_hash = NULL, email_token_expires = NULL WHERE id = ?')->execute([(int)$user['id']]);
                $message = 'Adresse email supprimée.';
            } else {
                $token = bin2hex(random_bytes(32));
                $pdo->prepare('UPDATE users SET pending_email = ?, email_token_hash = ?, email_token_expires = ? WHERE id = ?')->execute([$email, hash('sha256', $token), time() + 86400, (int)$user['id']]);
                if (send_email_verification($email, $token)) {
                    $message = 'Un email de confirmation a été envoyé à la nouvelle adresse.';
                } else {
                    $error = 'Impossible d’envoyer l’email de confirmation. Vérifiez la configuration email du serveur.';
                }
            }
        } else {
            $message = 'Informations enregistrées.';
        }
        $user = current_user();
    }
}

page_header('User account', $user);
?>
<a href="logout.php" class="button logout"><i class="fa-solid fa-right-from-bracket"></i> Logout</a>
<a href="users.php" class="button register"><i class="fa-solid fa-users" style="margin-right:5px;"></i>Gerer utilisateurs</a>
<div class="card"><h2><?=htmlspecialchars($user['username'])?></h2>
<?php if ($message): ?><div class="alert success"><?=htmlspecialchars($message)?></div><?php endif; ?>
<?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?>
<form method="post">
<input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
<label>Email<input type="email" name="email" maxlength="254" autocomplete="email" value="<?=htmlspecialchars($user['email'] ?? ($user['pending_email'] ?? ''))?>"></label>
<label>Téléphone<input type="tel" name="phone" maxlength="32" autocomplete="tel" value="<?=htmlspecialchars($user['phone'] ?? '')?>" required><small class="muted">Obligatoire pour recevoir les SMS de configuration du TRAK.</small></label>
<button type="submit">Enregistrer</button>
</form>
<?php if (!empty($user['email'])): ?><p>Email : <strong><?=htmlspecialchars($user['email'])?></strong> <?php if (!empty($user['email_verified_at'])): ?>✓ confirmé<?php else: ?><span class="muted">non confirmé</span><?php endif; ?></p><?php elseif (!empty($user['pending_email'])): ?><p>Email en attente : <strong><?=htmlspecialchars($user['pending_email'])?></strong></p><?php endif; ?>
<p>Rôle : <strong><?=htmlspecialchars($user['role'])?></strong></p><p>Créé le : <?=htmlspecialchars($user['created_at'])?></p>
<?php if ($user['role']==='admin' && isset($_GET['created'])): ?><div class="alert success">Utilisateur créé.</div><?php endif; ?>
</div><?php page_footer(); ?>