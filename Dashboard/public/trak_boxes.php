<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';
require_once __DIR__ . '/../app/mail.php';

$user = require_login();
$pdo = db();
$message = '';
$error = '';
$editId = 0;

$users = $pdo->query('SELECT id, username, email, phone, pending_email FROM users ORDER BY username COLLATE NOCASE')->fetchAll();

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
            if ($id > 0 && $apiKey === '') {
                $existingKeyStmt = $pdo->prepare('SELECT api_key FROM trak_boxes WHERE id = ?');
                $existingKeyStmt->execute([$id]);
                $apiKey = trim((string)($existingKeyStmt->fetchColumn() ?: ''));
            }
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

            $userStmt = $pdo->prepare('SELECT username, email, phone FROM users WHERE id = ?');
            $userStmt->execute([$userId]);
            $owner = $userStmt->fetch();
            $userPhone = trim((string)($owner['phone'] ?? ''));
            if ($userPhone === '') {
                throw new RuntimeException('Le compte utilisateur ne possède pas de numéro de téléphone. USER_PHONE est obligatoire pour le SMS 1.');
            }
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
            foreach ([['SSID Wi-Fi 1', $wifiSsid1, 64], ['Mot de passe Wi-Fi 1', $wifiPassword1, 128], ['SSID Wi-Fi 2', $wifiSsid2, 64], ['Mot de passe Wi-Fi 2', $wifiPassword2, 128], ['SSID Wi-Fi 3', $wifiSsid3, 64], ['Mot de passe Wi-Fi 3', $wifiPassword3, 128]] as [$label, $value, $max]) {
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

            $configStmt = $pdo->prepare('UPDATE trak_configs SET
                config_pending = 1,
                config_updated_at = (CAST(strftime(\'%s\',\'now\') AS INTEGER) * 1000 + CAST(substr(strftime(\'%f\',\'now\'), 4, 3) AS INTEGER)),
                api_key = ?, trak_phone = ?, user_phone = ?,
                trackserver_url = ?, dashboard_url = ?,
                wifi_ssid_1 = ?, wifi_password_1 = ?,
                wifi_ssid_2 = ?, wifi_password_2 = ?,
                wifi_ssid_3 = ?, wifi_password_3 = ?,
                updated_at = CURRENT_TIMESTAMP
                WHERE trak_box_id = ?');
            $configStmt->execute([$apiKey, $phone, $userPhone, $trakserverUrl, $dashboardUrl, $wifiSsid1, $wifiPassword1, $wifiSsid2, $wifiPassword2, $wifiSsid3, $wifiPassword3, $editId]);

            if ($configStmt->rowCount() === 0) {
                $configStmt = $pdo->prepare('INSERT INTO trak_configs (
                    trak_box_id, config_pending, config_updated_at, api_key, trak_phone, user_phone,
                    trackserver_url, dashboard_url, wifi_ssid_1, wifi_password_1,
                    wifi_ssid_2, wifi_password_2, wifi_ssid_3, wifi_password_3
                ) VALUES (?, 1, (CAST(strftime(\'%s\',\'now\') AS INTEGER) * 1000 + CAST(substr(strftime(\'%f\',\'now\'), 4, 3) AS INTEGER)), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)');
                $configStmt->execute([$editId, $apiKey, $phone, $userPhone, $trakserverUrl, $dashboardUrl, $wifiSsid1, $wifiPassword1, $wifiSsid2, $wifiPassword2, $wifiSsid3, $wifiPassword3]);
            }

            if ($id === 0 && !empty($owner['email'])) {
                send_trak_created_email((string)$owner['email'], (string)$owner['username'], $trakId);
            }

            $message = $id > 0 ? 'TRAK Box modifiée et nouvelle configuration mise en attente.' : 'TRAK Box enregistrée et configuration initiale mise en attente.';
        } elseif ($action === 'delete') {
            $id = (int)($_POST['id'] ?? 0);
            $infoStmt = $pdo->prepare('SELECT tb.trak_id, u.username, u.email FROM trak_boxes tb LEFT JOIN users u ON u.id = tb.user_id WHERE tb.id = ?');
            $infoStmt->execute([$id]);
            $boxInfo = $infoStmt->fetch();

            $stmt = $pdo->prepare('DELETE FROM trak_boxes WHERE id = ?');
            $stmt->execute([$id]);
            if ($stmt->rowCount() === 0) throw new RuntimeException('TRAK Box introuvable.');

            if (!empty($boxInfo['email'])) {
                send_trak_deleted_email((string)$boxInfo['email'], (string)$boxInfo['username'], (string)$boxInfo['trak_id']);
            }

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
$boxes = $pdo->query('SELECT tb.*, u.username, tc.config_pending, tc.config_updated_at, tc.firmware_version, tc.wifi_ssid_1, tc.wifi_password_1, tc.wifi_ssid_2, tc.wifi_password_2, tc.wifi_ssid_3, tc.wifi_password_3 FROM trak_boxes tb LEFT JOIN users u ON u.id = tb.user_id LEFT JOIN trak_configs tc ON tc.trak_box_id = tb.id ORDER BY tb.trak_id COLLATE NOCASE')->fetchAll();

page_header('TRAK Box', $user);
?>
<?php if ($message): ?><div class="alert success"><?=htmlspecialchars($message)?></div><?php endif; ?>
<?php if ($error): ?><div class="alert <?=str_starts_with($error, 'Échec') ? 'fail' : 'error'?>"><?=htmlspecialchars($error)?></div><?php endif; ?>

<div class="section-heading trak-box-header">
    <div>
        <span class="section-kicker">MANAGER</span>
        <h2>TRAK Box</h2>
    </div>
    <button type="button" class="add-trak-button" onclick="openTrakModal()">
        <i class="fa-solid fa-plus"></i> Ajouter un TRAK
    </button>
</div>

<div class="card trak-box-list">
<h3>TRAK Box enregistrées</h3>
<?php if (!$boxes): ?>
<p class="muted">Aucune TRAK Box enregistrée.</p>
<?php else: ?>
<div class="table-wrap"><table class="data-table trak-box-table">
<thead><tr><th>Box ID</th><th>Username</th><th>Config</th><th>Firmware</th><th>Dernière MAJ</th><th>Action</th></tr></thead>
<tbody>
<?php foreach ($boxes as $box): ?>
<tr>
<td><strong><?=htmlspecialchars($box['trak_id'])?></strong></td>
<td><?= $box['username'] !== null && $box['username'] !== '' ? htmlspecialchars((string)$box['username']) : '—' ?></td>
<td>
    <?php if ((int)($box['config_pending'] ?? 0) === 1): ?>
        <span class="config-status pending"><i class="fa-solid fa-clock"></i> À envoyer</span>
    <?php else: ?>
        <span class="config-status ready"><i class="fa-solid fa-check"></i> À jour</span>
    <?php endif; ?>
</td>
<td><?=htmlspecialchars((string)($box['firmware_version'] ?? '')) ?: '—'?></td>
<td>
    <?php
    $configUpdatedAt = (string)($box['config_updated_at'] ?? '');
    $lastUpdate = '—';
    if ($configUpdatedAt !== '' && ctype_digit($configUpdatedAt)) {
        $timestampMs = (int)$configUpdatedAt;
        $timestamp = (int)floor($timestampMs / 1000);
        if ($timestamp > 0) $lastUpdate = date('d/m/Y H:i', $timestamp);
    } elseif ($configUpdatedAt !== '') {
        $timestamp = strtotime($configUpdatedAt);
        if ($timestamp !== false) $lastUpdate = date('d/m/Y H:i', $timestamp);
    }
    ?>
    <?=htmlspecialchars($lastUpdate)?>
</td>
<td class="actions trak-actions">
    <button type="button" class="table-action modify" onclick="openEditModal(<?=htmlspecialchars(json_encode([
        'id'=>(int)$box['id'],'user_id'=>(int)$box['user_id'],'trak_id'=>(string)$box['trak_id'],
        'phone'=>(string)$box['phone'],'api_key'=>(string)$box['api_key'],
        'trakserver_url'=>(string)($box['trakserver_url'] ?? ''),'dashboard_url'=>(string)($box['dashboard_url'] ?? ''),
        'wifi_ssid_1'=>(string)($box['wifi_ssid_1'] ?? ''),'wifi_password_1'=>(string)($box['wifi_password_1'] ?? ''),
        'wifi_ssid_2'=>(string)($box['wifi_ssid_2'] ?? ''),'wifi_password_2'=>(string)($box['wifi_password_2'] ?? ''),
        'wifi_ssid_3'=>(string)($box['wifi_ssid_3'] ?? ''),'wifi_password_3'=>(string)($box['wifi_password_3'] ?? '')
    ], JSON_HEX_TAG|JSON_HEX_APOS|JSON_HEX_AMP|JSON_HEX_QUOT))?>)">
        <i class="fa-solid fa-pen"></i> Modifier
    </button>
    <form method="post" onsubmit="return confirm('Supprimer cette TRAK Box ?');">
        <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
        <input type="hidden" name="action" value="delete">
        <input type="hidden" name="id" value="<?=(int)$box['id']?>">
        <button type="submit" class="table-action danger delete"><i class="fa-solid fa-trash"></i></button>
    </form>
</td>
</tr>
<?php endforeach; ?>
</tbody></table></div>
<?php endif; ?>
</div>

<div class="api-page"
     data-api-users="<?=htmlspecialchars(json_encode($users, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), ENT_QUOTES, 'UTF-8')?>"
     data-api-traks="<?=htmlspecialchars(json_encode($boxes, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), ENT_QUOTES, 'UTF-8')?>">
<section class="card api-selector-card">
        <div class="section-heading">
            <div><span class="section-kicker">CIBLE</span><h3>Sélectionner un TRAK</h3></div>
            <span class="selection-hint">1. Utilisateur &nbsp;→&nbsp; 2. TRAK ID</span>
        </div>
        <div class="api-select-grid">
            <label><span>User ID</span><select id="apiUserSelect"><option value="">Sélectionner un utilisateur</option>
                <?php foreach ($users as $u): ?><option value="<?= (int)$u['id'] ?>"><?= (int)$u['id'] ?> — <?= htmlspecialchars((string)$u['username'], ENT_QUOTES, 'UTF-8') ?></option><?php endforeach; ?>
            </select></label>
            <label><span>TRAK ID</span><select id="apiTrakSelect" disabled><option value="">Sélectionner un TRAK</option></select></label>
        </div>
        <div id="emptyTrakHint" class="selector-empty">Sélectionnez un User ID pour afficher ses TRAK.</div>
    </section>

    <section id="apiSelection" class="api-selection" hidden aria-hidden="true">
        <div id="apiDataCard" class="card api-data-card" hidden>
            <div class="section-heading compact"><div><span class="section-kicker">PAYLOAD</span><h3>Données TRAK</h3></div></div>
            <pre id="apiDataCode" class="api-data-code"><code></code></pre>
        </div>
        <div class="api-two-columns">
            <div class="card api-sms-card">
                <div class="section-heading compact"><div><span class="section-kicker">PROVISIONING</span><h3>SMS de configuration</h3></div><span class="status-pill neutral">Format TRAKCFG v1</span></div>
                <p class="muted">SMS généré automatiquement avec la configuration nécessaire à la communication entre le TRAK et le Dashboard.</p>
                <div id="configSmsList" class="sms-config-list"><div class="muted">Sélectionnez un TRAK pour générer les SMS.</div></div>
            </div>
        </div>
    </section>
</div>

</div>

<script src="assets/api.js" defer></script>

<div class="modal-backdrop" id="trakModal" aria-hidden="true">
    <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="trakModalTitle">
        <div class="modal-header">
            <div><span class="section-kicker">TRAK BOX</span><h2 id="trakModalTitle">Ajouter un TRAK</h2></div>
            <button type="button" class="modal-close" onclick="closeTrakModal()" aria-label="Fermer"><i class="fa-solid fa-xmark"></i></button>
        </div>
        <form method="post" id="trakBoxForm">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <input type="hidden" name="action" value="save">
            <input type="hidden" name="id" id="trakFormId" value="">
            <label>User ID propriétaire
                <select name="user_id" id="trakFormUser" required><option value="">Sélectionner un utilisateur</option>
                    <?php foreach ($users as $u): ?><option value="<?= (int)$u['id'] ?>"><?= (int)$u['id'] ?> — <?=htmlspecialchars($u['username'])?></option><?php endforeach; ?>
                </select>
            </label>
            <label>ID TRAK<input type="text" name="trak_id" id="trakFormIdTrak" maxlength="5" required placeholder="TRK01"></label>
            <label>Numéro de téléphone du TRAK<input type="tel" name="phone" id="trakFormPhone" maxlength="32" required placeholder="+33612345678"></label>
            <label>Clé API<input type="text" name="api_key" id="trakFormApiKey" maxlength="16" pattern="[A-Za-z0-9]{16}" placeholder="Génération automatique" disabled></label>
            <label>URL Trackserver, choisir OsmAnd profile <a href="https://github.com/tinuzz/wp-plugin-trackserver" target="_blank" rel="noopener">(WordPress Plugin)</a>
                <input type="url" name="trakserver_url" id="trakFormTrackserver" maxlength="160" required placeholder="https://monsite.com/trackserver/username/password/?lat={0}&lon={1}&timestamp={2}&altitude={4}&speed={5}&bearing={6}">
                <small class="muted">Pour l'API de position : {0}=latitude, {1}=longitude, {2}=timestamp, {3}=clé API, {7}=TRAK ID. L'URL doit rester en HTTPS.</small>
            </label>
            <label>URL Dashboard
                <input type="url" name="dashboard_url" id="trakFormDashboard" maxlength="160" required placeholder="https://exemple.fr">
                <small class="muted">Endpoint HTTPS utilisé pour la communication Dashboard ↔ TRAK.</small>
            </label>
            <div class="section-heading compact" style="margin-top:24px"><div><span class="section-kicker">CONFIGURATION DISTANTE</span><h3>Wi-Fi du TRAK</h3></div></div>
            <p class="muted">Ces paramètres sont stockés dans la configuration distante. Toute modification crée une nouvelle configuration en attente.</p>
            <label>Wi-Fi 1 — SSID><input type="text" name="wifi_ssid_1" id="wifiSsid1" maxlength="64" placeholder="Nom du réseau"></label>
            <label>Wi-Fi 1 — Mot de passe<div class="password-field"><input type="password" id="wifi_password_1" name="wifi_password_1" maxlength="128" placeholder="Mot de passe"><button type="button" class="password-toggle" onclick="toggleWifiPassword(1, this)">Voir</button></div></label>
            <label>Wi-Fi 2 — SSID><input type="text" name="wifi_ssid_2" id="wifiSsid2" maxlength="64" placeholder="Nom du réseau"></label>
            <label>Wi-Fi 2 — Mot de passe<div class="password-field"><input type="password" id="wifi_password_2" name="wifi_password_2" maxlength="128" placeholder="Mot de passe"><button type="button" class="password-toggle" onclick="toggleWifiPassword(2, this)">Voir</button></div></label>
            <label>Wi-Fi 3 — SSID><input type="text" name="wifi_ssid_3" id="wifiSsid3" maxlength="64" placeholder="Nom du réseau"></label>
            <label>Wi-Fi 3 — Mot de passe<div class="password-field"><input type="password" id="wifi_password_3" name="wifi_password_3" maxlength="128" placeholder="Mot de passe"><button type="button" class="password-toggle" onclick="toggleWifiPassword(3, this)">Voir</button></div></label>
            <div class="modal-actions"><button type="button" class="modal-cancel" onclick="closeTrakModal()">Annuler</button><button type="submit" class="modal-submit" id="trakSubmitButton">Enregistrer</button></div>
        </form>
    </div>
</div>

<script>
function toggleWifiPassword(slot, button) {
    const input = document.getElementById('wifi_password_' + slot);
    if (!input) return;
    const visible = input.type === 'text';
    input.type = visible ? 'password' : 'text';
    button.textContent = visible ? 'Voir' : 'Masquer';
}
function openTrakModal() {
    const modal = document.getElementById('trakModal');
    document.getElementById('trakModalTitle').textContent = 'Ajouter un TRAK';
    document.getElementById('trakSubmitButton').textContent = 'Enregistrer';
    document.getElementById('trakFormId').value = '';
    document.getElementById('trakFormUser').value = '';
    document.getElementById('trakFormIdTrak').value = '';
    document.getElementById('trakFormPhone').value = '';
    document.getElementById('trakFormApiKey').value = '';
    document.getElementById('trakFormTrackserver').value = '';
    document.getElementById('trakFormDashboard').value = '';
    ['wifiSsid1','wifiSsid2','wifiSsid3','wifi_password_1','wifi_password_2','wifi_password_3'].forEach(id => { const el = document.getElementById(id); if (el) el.value = ''; });
    modal.setAttribute('aria-hidden', 'false');
    document.body.classList.add('modal-open');
    document.getElementById('trakFormUser').focus();
}
function openEditModal(box) {
    const modal = document.getElementById('trakModal');
    document.getElementById('trakModalTitle').textContent = 'Modifier une TRAK Box';
    document.getElementById('trakSubmitButton').textContent = 'Enregistrer les modifications';
    document.getElementById('trakFormId').value = box.id || '';
    document.getElementById('trakFormUser').value = box.user_id || '';
    document.getElementById('trakFormIdTrak').value = box.trak_id || '';
    document.getElementById('trakFormPhone').value = box.phone || '';
    document.getElementById('trakFormApiKey').value = box.api_key || '';
    document.getElementById('trakFormTrackserver').value = box.trakserver_url || '';
    document.getElementById('trakFormDashboard').value = box.dashboard_url || '';
    document.getElementById('wifiSsid1').value = box.wifi_ssid_1 || '';
    document.getElementById('wifi_password_1').value = box.wifi_password_1 || '';
    document.getElementById('wifiSsid2').value = box.wifi_ssid_2 || '';
    document.getElementById('wifi_password_2').value = box.wifi_password_2 || '';
    document.getElementById('wifiSsid3').value = box.wifi_ssid_3 || '';
    document.getElementById('wifi_password_3').value = box.wifi_password_3 || '';
    modal.setAttribute('aria-hidden', 'false');
    document.body.classList.add('modal-open');
    document.getElementById('trakFormUser').focus();
}
function closeTrakModal() {
    const modal = document.getElementById('trakModal');
    if (!modal) return;
    modal.setAttribute('aria-hidden', 'true');
    document.body.classList.remove('modal-open');
}
document.getElementById('trakModal')?.addEventListener('click', function(event) { if (event.target === this) closeTrakModal(); });
document.addEventListener('keydown', function(event) { if (event.key === 'Escape') closeTrakModal(); });
</script>
<?php page_footer(); ?>