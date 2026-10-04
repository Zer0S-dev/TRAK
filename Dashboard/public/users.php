<?php
declare(strict_types=1);

require_once __DIR__ . '/../app/auth.php';
require_once __DIR__ . '/../app/mail.php';
require_once __DIR__ . '/partials.php';

$user = require_admin();
$pdo = db();
$error = '';
$success = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $action = $_POST['action'] ?? '';

    if ($action === 'create') {
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
                $stmt = $pdo->prepare('INSERT INTO users (username, password_hash, email, phone, role) VALUES (?, ?, ?, ?, ?)');
                $stmt->execute([$username, password_hash($password, PASSWORD_DEFAULT), $email, $phone, 'user']);
                $id = (int)$pdo->lastInsertId();

                $mailSent = false;
                if ($email !== '') {
                    $token = bin2hex(random_bytes(32));
                    $stmt = $pdo->prepare('UPDATE users SET pending_email = ?, email = NULL, email_token_hash = ?, email_token_expires = ? WHERE id = ?');
                    $stmt->execute([$email, hash('sha256', $token), time() + 86400, $id]);
                    $mailSent = send_email_verification($email, $token);
                    if ($mailSent) send_user_created_email($email, $username);
                }

                header('Location: users.php?created=1&mail=' . ($mailSent ? 'sent' : 'error'));
                exit;
            } catch (PDOException $e) {
                $error = ((int)($e->errorInfo[1] ?? 0) === 19) ? 'Cet identifiant existe déjà.' : 'Impossible de créer le compte.';
            }
        }
    } elseif ($action === 'delete') {
        $id = (int)($_POST['user_id'] ?? 0);
        if ($id === (int)$user['id']) {
            $error = 'Vous ne pouvez pas supprimer votre propre compte.';
        } else {
            $stmt = $pdo->prepare('SELECT username, email, pending_email FROM users WHERE id = ?');
            $stmt->execute([$id]);
            $target = $stmt->fetch();

            if (!$target) {
                $error = 'Utilisateur introuvable.';
            } else {
                $mail = (string)($target['email'] ?: $target['pending_email'] ?: '');
                if ($mail !== '') send_user_deleted_email($mail, (string)$target['username']);
                $stmt = $pdo->prepare('DELETE FROM users WHERE id = ?');
                $stmt->execute([$id]);
                header('Location: users.php?deleted=1');
                exit;
            }
        }
    }
}

if (isset($_GET['created'])) {
    $success = $_GET['mail'] === 'sent' ? 'Utilisateur créé. Les emails ont été envoyés.' : 'Utilisateur créé. Aucun email de confirmation n’a pu être envoyé.';
} elseif (isset($_GET['deleted'])) {
    $success = 'Utilisateur supprimé.';
}

$users = $pdo->query('SELECT id, username, email, pending_email, phone, role, created_at FROM users ORDER BY id ASC')->fetchAll();

page_header('Users', $user);
?>
<div class="section-heading">
    <div>
        <span class="section-kicker">ADMINISTRATION</span>
        <h2>Users</h2>
    </div>
    <button type="button" onclick="openUserModal()">
        <i class="fa-solid fa-plus"></i> Ajouter
    </button>
</div>

<?php if ($error): ?><div class="alert error"><?=htmlspecialchars($error)?></div><?php endif; ?>
<?php if ($success): ?><div class="alert success"><?=htmlspecialchars($success)?></div><?php endif; ?>

<div class="card">
    <div class="table-wrap">
        <table class="data-table">
            <thead>
                <tr>
                    <th>ID</th>
                    <th>Utilisateur</th>
                    <th>Email</th>
                    <th>Téléphone</th>
                    <th>Rôle</th>
                    <th>Créé le</th>
                    <th>Action</th>
                </tr>
            </thead>
            <tbody>
            <?php foreach ($users as $row): ?>
                <tr>
                    <td><?= (int)$row['id'] ?></td>
                    <td><?=htmlspecialchars($row['username'])?></td>
                    <td><?=htmlspecialchars((string)($row['email'] ?: $row['pending_email'] ?: ''))?></td>
                    <td><?=htmlspecialchars($row['phone'] ?? '')?></td>
                    <td><?=htmlspecialchars($row['role'])?></td>
                    <td><?=htmlspecialchars($row['created_at'])?></td>
                    <td>
                        <?php if ((int)$row['id'] !== (int)$user['id']): ?>
                            <form method="post" onsubmit="return confirm('Supprimer cet utilisateur ?');" style="display:inline;">
                                <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
                                <input type="hidden" name="action" value="delete">
                                <input type="hidden" name="user_id" value="<?= (int)$row['id'] ?>">
                                <button type="submit" class="danger small-action" title="Supprimer">
                                    <i class="fa-solid fa-trash"></i>
                                </button>
                            </form>
                        <?php else: ?>
                            <span class="muted">Compte actuel</span>
                        <?php endif; ?>
                    </td>
                </tr>
            <?php endforeach; ?>
            </tbody>
        </table>
    </div>
</div>

<div class="modal-backdrop" id="userModal" aria-hidden="true">
    <div class="modal" role="dialog" aria-modal="true" aria-labelledby="userModalTitle">
        <div class="modal-header">
            <div>
                <span class="section-kicker">UTILISATEUR</span>
                <h3 id="userModalTitle">Ajouter un utilisateur</h3>
            </div>
            <button type="button" class="modal-close" onclick="closeUserModal()" aria-label="Fermer">&times;</button>
        </div>

        <form method="post">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <input type="hidden" name="action" value="create">

            <label>Identifiant
                <input name="username" minlength="3" maxlength="64" autocomplete="username" required>
            </label>
            <label>Email
                <input type="email" name="email" maxlength="254" autocomplete="email">
            </label>
            <label>Téléphone
                <input type="tel" name="phone" maxlength="32" autocomplete="tel">
            </label>
            <label>Mot de passe
                <input type="password" name="password" id="user-password" minlength="10" autocomplete="new-password" required>
            </label>
            <label>Confirmation
                <input type="password" name="password_confirm" id="user-password-confirm" minlength="10" autocomplete="new-password" required>
            </label>
            <label class="password-toggle">
                <input type="checkbox" id="show-user-password"> Voir le mot de passe
            </label>

            <div class="modal-actions">
                <button type="button" class="secondary" onclick="closeUserModal()">Annuler</button>
                <button type="submit"><i class="fa-solid fa-user-plus"></i> Ajouter</button>
            </div>
        </form>
    </div>
</div>

<script>
function openUserModal() {
    const modal = document.getElementById('userModal');
    modal.classList.add('open');
    modal.setAttribute('aria-hidden', 'false');
    document.body.classList.add('modal-open');
    setTimeout(() => modal.querySelector('input[name="username"]').focus(), 50);
}

function closeUserModal() {
    const modal = document.getElementById('userModal');
    modal.classList.remove('open');
    modal.setAttribute('aria-hidden', 'true');
    document.body.classList.remove('modal-open');
}

document.getElementById('userModal').addEventListener('click', function(e) {
    if (e.target === this) closeUserModal();
});

document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') closeUserModal();
});

document.getElementById('show-user-password').addEventListener('change', function() {
    const type = this.checked ? 'text' : 'password';
    document.getElementById('user-password').type = type;
    document.getElementById('user-password-confirm').type = type;
});
</script>

<?php page_footer(); ?>
