<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();
$message = '';
$error = '';
$editId = isset($_GET['edit']) ? (int)$_GET['edit'] : 0;

$users = $pdo->query('SELECT id, username FROM users ORDER BY username COLLATE NOCASE')->fetchAll();

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    verify_csrf();
    $action = (string)($_POST['action'] ?? '');

    try {
        if ($action === 'save') {
            $id = (int)($_POST['id'] ?? 0);
            $userId = (int)($_POST['user_id'] ?? 0);
            $trakId = strtoupper(trim((string)($_POST['trak_id'] ?? '')));
            $phone = trim((string)($_POST['phone'] ?? ''));
            $apiKey = trim((string)($_POST['api_key'] ?? ''));
            $trakserverUrl = trim((string)($_POST['trakserver_url'] ?? ''));

            if ($userId <= 0) throw new RuntimeException('Veuillez sélectionner un User ID.');
            $checkUser = $pdo->prepare('SELECT COUNT(*) FROM users WHERE id = ?');
            $checkUser->execute([$userId]);
            if ((int)$checkUser->fetchColumn() !== 1) throw new RuntimeException('User ID invalide.');
            if (!preg_match('/^[A-Z0-9][A-Z0-9_-]{0,31}$/', $trakId)) {
                throw new RuntimeException('ID TRAK invalide. Utilisez 1 à 32 caractères : A-Z, 0-9, _ ou -.');
            }
            if ($phone === '' || strlen($phone) > 32) {
                throw new RuntimeException('Numéro de téléphone TRAK invalide.');
            }
            if ($trakserverUrl === '' || !filter_var($trakserverUrl, FILTER_VALIDATE_URL) || !preg_match('#^https://#i', $trakserverUrl)) {
                throw new RuntimeException('URL OsmAnd/Trakserver invalide. Utilisez une URL HTTPS.');
            }
            if ($apiKey === '') $apiKey = bin2hex(random_bytes(25));
            if (!preg_match('/^[A-Za-z0-9]{50}$/', $apiKey)) throw new RuntimeException('La clé API doit contenir exactement 50 caractères alphanumériques.');

            if ($id > 0) {
                $stmt = $pdo->prepare('UPDATE trak_boxes SET user_id = ?, trak_id = ?, phone = ?, api_key = ?, trakserver_url = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?');
                $stmt->execute([$userId, $trakId, $phone, $apiKey, $trakserverUrl, $id]);
                $message = 'TRAK Box modifiée avec succès.';
                $editId = $id;
            } else {
                $stmt = $pdo->prepare('INSERT INTO trak_boxes (user_id, trak_id, phone, api_key, trakserver_url) VALUES (?, ?, ?, ?, ?)');
                $stmt->execute([$userId, $trakId, $phone, $apiKey, $trakserverUrl]);
                $message = 'TRAK Box enregistrée avec succès.';
                $editId = (int)$pdo->lastInsertId();
            }
        } elseif ($action === 'delete') {
            $id = (int)($_POST['id'] ?? 0);
            $stmt = $pdo->prepare('DELETE FROM trak_boxes WHERE id = ?');
            $stmt->execute([$id]);
            if ($stmt->rowCount() === 0) throw new RuntimeException('TRAK Box introuvable.');
            $message = 'TRAK Box supprimée avec succès.';
            $editId = 0;
        }
    } catch (Throwable $e) {
        if ($e instanceof PDOException && str_contains($e->getMessage(), 'UNIQUE')) {
            $error = 'Impossible d’enregistrer : cet ID TRAK existe déjà.';
        } elseif ($e instanceof PDOException) {
            $error = 'Erreur lors de l’enregistrement de la TRAK Box.';
        } else {
            $error = 'Échec de l’opération : ' . $e->getMessage();
        }
    }
}

$edit = null;
if ($editId > 0) {
    $stmt = $pdo->prepare('SELECT * FROM trak_boxes WHERE id = ?');
    $stmt->execute([$editId]);
    $edit = $stmt->fetch() ?: null;
    if (!$edit && $error === '') $error = 'TRAK Box introuvable.';
}
$boxes = $pdo->query('SELECT tb.*, u.username FROM trak_boxes tb LEFT JOIN users u ON u.id = tb.user_id ORDER BY tb.trak_id COLLATE NOCASE')->fetchAll();

page_header('TRAK Box', $user);
?>
<?php if ($message): ?><div class="alert success"><?=htmlspecialchars($message)?></div><?php endif; ?>
<?php if ($error): ?><div class="alert <?=str_starts_with($error, 'Échec') ? 'fail' : 'error'?>"><?=htmlspecialchars($error)?></div><?php endif; ?>

<div class="card">
<h2><?= $edit ? 'Modifier une TRAK Box' : 'Enregistrer une TRAK Box' ?></h2>
<form method="post">
<input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
<input type="hidden" name="action" value="save">
<input type="hidden" name="id" value="<?= (int)($edit['id'] ?? 0) ?>">
<label>User ID propriétaire
<select name="user_id" required>
<option value="">Sélectionner un utilisateur</option>
<?php foreach ($users as $u): ?>
<option value="<?= (int)$u['id'] ?>" <?= ((int)($edit['user_id'] ?? 0) === (int)$u['id']) ? 'selected' : '' ?>><?= (int)$u['id'] ?> — <?=htmlspecialchars($u['username'])?></option>
<?php endforeach; ?>
</select>
</label>
<label>ID TRAK
<input type="text" name="trak_id" maxlength="32" required value="<?=htmlspecialchars($edit['trak_id'] ?? '')?>" placeholder="TRK-001">
</label>
<label>Numéro de téléphone du TRAK
<input type="tel" name="phone" maxlength="32" required value="<?=htmlspecialchars($edit['phone'] ?? '')?>" placeholder="+33612345678">
</label>
<label>Clé API
<input type="text" name="api_key" maxlength="50" pattern="[A-Za-z0-9]{50}" value="<?=htmlspecialchars($edit['api_key'] ?? '')?>" placeholder="Laisser vide pour générer une nouvelle clé">
</label>
<label>URL Trakserver URL
<input type="url" name="trakserver_url" maxlength="160" required value="<?=htmlspecialchars($edit['trakserver_url'] ?? '')?>" placeholder="https://exemple.fr/trakserver">
</label>
<button type="submit"><?= $edit ? 'Enregistrer les modifications' : 'Enregistrer la TRAK Box' ?></button>
<?php if ($edit): ?><a class="back" href="trak_boxes.php">Annuler</a><?php endif; ?>
</form>
</div>

<div class="card" style="margin-top:24px">
<h2>TRAK Box enregistrées</h2>
<?php if (!$boxes): ?>
<p class="muted">Aucune TRAK Box enregistrée.</p>
<?php else: ?>
<div class="table-wrap"><table class="data-table">
<thead><tr><th>User ID</th><th>TRAK ID</th><th>Téléphone</th><th>Clé API</th><th>Trakserver URL</th><th>Actions</th></tr></thead>
<tbody>
<?php foreach ($boxes as $box): ?>
<tr>
<td><strong><?= $box['user_id'] !== null ? (int)$box['user_id'] : '—' ?></strong></td>
<td><strong><?=htmlspecialchars($box['trak_id'])?></strong></td>
<td><?=htmlspecialchars($box['phone'])?></td>
<td><code><?=htmlspecialchars($box['api_key'])?></code></td>
<td><a href="<?=htmlspecialchars($box['trakserver_url'])?>" target="_blank" rel="noopener"><?=htmlspecialchars($box['trakserver_url'])?></a></td>
<td class="actions">
<a href="trak_boxes.php?edit=<?=(int)$box['id']?>">Modifier</a>
<form method="post" onsubmit="return confirm('Supprimer cette TRAK Box ?');">
<input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
<input type="hidden" name="action" value="delete">
<input type="hidden" name="id" value="<?=(int)$box['id']?>">
<button type="submit" class="danger">Supprimer</button>
</form>
</td>
</tr>
<?php endforeach; ?>
</tbody></table></div>
<?php endif; ?>
</div>
<?php page_footer(); ?>