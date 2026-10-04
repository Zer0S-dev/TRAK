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
            $dashboardUrl = trim((string)($_POST['dashboard_url'] ?? ''));
            $wifiSsid1 = trim((string)($_POST['wifi_ssid_1'] ?? ''));
            $wifiPassword1 = (string)($_POST['wifi_password_1'] ?? '');
            $wifiSsid2 = trim((string)($_POST['wifi_ssid_2'] ?? ''));
            $wifiPassword2 = (string)($_POST['wifi_password_2'] ?? '');
            $wifiSsid3 = trim((string)($_POST['wifi_ssid_3'] ?? ''));
            $wifiPassword3 = (string)($_POST['wifi_password_3'] ?? '');

            if ($userId <= 0) throw new RuntimeException('Veuillez sélectionner un User ID.');
            $checkUser = $pdo->prepare('SELECT COUNT(*) FROM users WHERE id = ?');
            $checkUser->execute([$userId]);
            if ((int)$checkUser->fetchColumn() !== 1) throw new RuntimeException('User ID invalide.');
            if (!preg_match('/^[A-Z0-9][A-Z0-9_-]{0,4}$/', $trakId)) {
                throw new RuntimeException('ID TRAK invalide. Utilisez 1 à 5 caractères : A-Z, 0-9, _ ou -.');
            }
            if ($phone === '' || strlen($phone) > 32) {
                throw new RuntimeException('Numéro de téléphone TRAK invalide.');
            }
            if ($trakserverUrl === '' || strlen($trakserverUrl) > 160 || !filter_var($trakserverUrl, FILTER_VALIDATE_URL) || !preg_match('#^https://#i', $trakserverUrl)) {
                throw new RuntimeException('URL OsmAnd/Trakserver invalide. Utilisez une URL HTTPS.');
            }
            if ($dashboardUrl === '' || strlen($dashboardUrl) > 160 || !filter_var($dashboardUrl, FILTER_VALIDATE_URL) || !preg_match('#^https://#i', $dashboardUrl)) {
                throw new RuntimeException('URL Dashboard invalide. Utilisez une URL HTTPS.');
            }
            foreach ([
                ['SSID Wi-Fi 1', $wifiSsid1, 64], ['Mot de passe Wi-Fi 1', $wifiPassword1, 128],
                ['SSID Wi-Fi 2', $wifiSsid2, 64], ['Mot de passe Wi-Fi 2', $wifiPassword2, 128],
                ['SSID Wi-Fi 3', $wifiSsid3, 64], ['Mot de passe Wi-Fi 3', $wifiPassword3, 128],
            ] as [$label, $value, $max]) {
                if (strlen($value) > $max) throw new RuntimeException($label . ' trop long.');
            }
            if ($apiKey === '') $apiKey = bin2hex(random_bytes(8));
            if (!preg_match('/^[A-Za-z0-9]{16}$/', $apiKey)) throw new RuntimeException('La clé API doit contenir exactement 16 caractères alphanumériques.');

            if ($id > 0) {
                $stmt = $pdo->prepare('UPDATE trak_boxes SET user_id = ?, trak_id = ?, phone = ?, api_key = ?, trakserver_url = ?, dashboard_url = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?');
                $stmt->execute([$userId, $trakId, $phone, $apiKey, $trakserverUrl, $dashboardUrl, $id]);
                $editId = $id;
            } else {
                $stmt = $pdo->prepare('INSERT INTO trak_boxes (user_id, trak_id, phone, api_key, trakserver_url, dashboard_url) VALUES (?, ?, ?, ?, ?, ?)');
                $stmt->execute([$userId, $trakId, $phone, $apiKey, $trakserverUrl, $dashboardUrl]);
                $editId = (int)$pdo->lastInsertId();
            }

            $userPhoneStmt = $pdo->prepare('SELECT phone FROM users WHERE id = ?');
            $userPhoneStmt->execute([$userId]);
            $userPhone = trim((string)($userPhoneStmt->fetchColumn() ?: ''));

            $configStmt = $pdo->prepare('INSERT INTO trak_configs (
                trak_box_id, config_pending, config_updated_at, api_key, trak_phone, user_phone,
                trackserver_url, dashboard_url, wifi_ssid_1, wifi_password_1,
                wifi_ssid_2, wifi_password_2, wifi_ssid_3, wifi_password_3, updated_at
            ) VALUES (?, 1, strftime("%s","now"), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
            ON CONFLICT(trak_box_id) DO UPDATE SET
                config_pending = 1,
                config_updated_at = strftime("%s","now"),
                api_key = excluded.api_key,
                trak_phone = excluded.trak_phone,
                user_phone = excluded.user_phone,
                trackserver_url = excluded.trackserver_url,
                dashboard_url = excluded.dashboard_url,
                wifi_ssid_1 = excluded.wifi_ssid_1,
                wifi_password_1 = excluded.wifi_password_1,
                wifi_ssid_2 = excluded.wifi_ssid_2,
                wifi_password_2 = excluded.wifi_password_2,
                wifi_ssid_3 = excluded.wifi_ssid_3,
                wifi_password_3 = excluded.wifi_password_3,
                updated_at = CURRENT_TIMESTAMP');
            $configStmt->execute([
                $editId, $apiKey, $phone, $userPhone, $trakserverUrl, $dashboardUrl,
                $wifiSsid1, $wifiPassword1, $wifiSsid2, $wifiPassword2, $wifiSsid3, $wifiPassword3
            ]);

            $message = $id > 0 ? 'TRAK Box modifiée et nouvelle configuration mise en attente.' : 'TRAK Box enregistrée et configuration initiale mise en attente.';
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
    if ($edit) {
        $cfg = $pdo->prepare('SELECT wifi_ssid_1, wifi_password_1, wifi_ssid_2, wifi_password_2, wifi_ssid_3, wifi_password_3 FROM trak_configs WHERE trak_box_id = ?');
        $cfg->execute([$editId]);
        $edit = array_merge($edit, $cfg->fetch() ?: []);
    }
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
<input type="text" name="trak_id" maxlength="5" required value="<?=htmlspecialchars($edit['trak_id'] ?? '')?>" placeholder="TRK01">
</label>
<label>Numéro de téléphone du TRAK
<input type="tel" name="phone" maxlength="32" required value="<?=htmlspecialchars($edit['phone'] ?? '')?>" placeholder="+33612345678">
</label>
<label>Clé API
<input type="text" name="api_key" maxlength="16" pattern="[A-Za-z0-9]{16}" value="<?=htmlspecialchars($edit['api_key'] ?? '')?>" placeholder="Generation automatique" disabled>
</label>
<label>URL Trackserver, choisir OsmAnd profile <a href="https://github.com/tinuzz/wp-plugin-trackserver">(WordPress Plugin)</a>
<input type="url" name="trakserver_url" maxlength="160" required value="<?=htmlspecialchars($edit['trakserver_url'] ?? '')?>" placeholder="https://monsite.com/trackserver/username/password/?lat={0}&lon={1}&timestamp={2}&altitude={4}&speed={5}&bearing={6}">
<small class="muted">Pour l'API de position : {0}=latitude, {1}=longitude, {2}=timestamp, {3}=clé API, {7}=TRAK ID. L'URL doit rester en HTTPS et faire au maximum 160 caractères.</small>
</label>
<label>URL Dashboard
<input type="url" name="dashboard_url" maxlength="160" required value="<?=htmlspecialchars($edit['dashboard_url'] ?? '')?>" placeholder="https://exemple.fr">
<small class="muted">Endpoint HTTPS utilisé pour la communication Dashboard ↔ TRAK.</small>
</label>

<div class="section-heading compact" style="margin-top:24px"><div><span class="section-kicker">CONFIGURATION DISTANTE</span><h3>Wi-Fi du TRAK</h3></div></div>
<p class="muted">Ces paramètres sont stockés dans la table de configuration distante. Toute modification crée une nouvelle configuration en attente pour ce TRAK.</p>

<label>Wi-Fi 1 — SSID
<input type="text" name="wifi_ssid_1" maxlength="64" value="<?=htmlspecialchars($edit['wifi_ssid_1'] ?? '')?>" placeholder="Nom du réseau">
</label>
<label>Wi-Fi 1 — Mot de passe
<input type="password" name="wifi_password_1" maxlength="128" value="<?=htmlspecialchars($edit['wifi_password_1'] ?? '')?>" placeholder="Mot de passe">
</label>

<label>Wi-Fi 2 — SSID
<input type="text" name="wifi_ssid_2" maxlength="64" value="<?=htmlspecialchars($edit['wifi_ssid_2'] ?? '')?>" placeholder="Nom du réseau">
</label>
<label>Wi-Fi 2 — Mot de passe
<input type="password" name="wifi_password_2" maxlength="128" value="<?=htmlspecialchars($edit['wifi_password_2'] ?? '')?>" placeholder="Mot de passe">
</label>

<label>Wi-Fi 3 — SSID
<input type="text" name="wifi_ssid_3" maxlength="64" value="<?=htmlspecialchars($edit['wifi_ssid_3'] ?? '')?>" placeholder="Nom du réseau">
</label>
<label>Wi-Fi 3 — Mot de passe
<input type="password" name="wifi_password_3" maxlength="128" value="<?=htmlspecialchars($edit['wifi_password_3'] ?? '')?>" placeholder="Mot de passe">
</label>
<button type="submit"><?= $edit ? 'Enregistrer les modifications' : 'Enregistrer' ?></button>
<?php if ($edit): ?><a class="back" href="trak_boxes.php">Annuler</a><?php endif; ?>
</form>
</div>

<div class="card" style="margin-top:24px">
<h2>TRAK Box enregistrées</h2>
<?php if (!$boxes): ?>
<p class="muted">Aucune TRAK Box enregistrée.</p>
<?php else: ?>
<div class="table-wrap"><table class="data-table">
<thead><tr><th>User ID</th><th>TRAK ID</th><th>Téléphone</th><th>Clé API</th><th>trackserver_url</th><th>Dashboard</th><th>Actions</th></tr></thead>
<tbody>
<?php foreach ($boxes as $box): ?>
<tr>
<td><strong><?= $box['user_id'] !== null ? (int)$box['user_id'] : '—' ?></strong></td>
<td><strong><?=htmlspecialchars($box['trak_id'])?></strong></td>
<td><?=htmlspecialchars($box['phone'])?></td>
<td><code><?=htmlspecialchars($box['api_key'])?></code></td>
<td class="long-text"><a href="<?=htmlspecialchars($box['trakserver_url'])?>" target="_blank" rel="noopener"><?=htmlspecialchars($box['trakserver_url'])?></a></td>
<td class="long-text"><a href="<?=htmlspecialchars($box['dashboard_url'])?>" target="_blank" rel="noopener"><?=htmlspecialchars($box['dashboard_url'])?></a></td>
<td class="actions">
<a href="trak_boxes.php?edit=<?=(int)$box['id']?>" class="modify">Modifier</a>
<form method="post" onsubmit="return confirm('Supprimer cette TRAK Box ?');">
<input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
<input type="hidden" name="action" value="delete">
<input type="hidden" name="id" value="<?=(int)$box['id']?>">
<button type="submit" class="danger delete">Supprimer</button>
</form>
</td>
</tr>
<?php endforeach; ?>
</tbody></table></div>
<?php endif; ?>
</div>
<?php page_footer(); ?>