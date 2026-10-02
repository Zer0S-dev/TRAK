<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();

if ($user['role'] === 'admin') {
    $users = $pdo->query('SELECT id, username, email, pending_email, phone FROM users ORDER BY username COLLATE NOCASE')->fetchAll();
    $traks = $pdo->query('SELECT id, user_id, trak_id, phone, api_key, trakserver_url, dashboard_url FROM trak_boxes ORDER BY trak_id COLLATE NOCASE')->fetchAll();
} else {
    $stmt = $pdo->prepare('SELECT id, username, email, pending_email, phone FROM users WHERE id = ?');
    $stmt->execute([(int)$user['id']]);
    $users = $stmt->fetchAll();
    $stmt = $pdo->prepare('SELECT id, user_id, trak_id, phone, api_key, trakserver_url, dashboard_url FROM trak_boxes WHERE user_id = ? ORDER BY trak_id COLLATE NOCASE');
    $stmt->execute([(int)$user['id']]);
    $traks = $stmt->fetchAll();
}

page_header('API', $user);
?>
<div class="api-page"
     data-api-users="<?=htmlspecialchars(json_encode($users, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), ENT_QUOTES, 'UTF-8')?>"
     data-api-traks="<?=htmlspecialchars(json_encode($traks, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES), ENT_QUOTES, 'UTF-8')?>">
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

<script src="assets/api.js" defer></script>
<?php page_footer(); ?>
