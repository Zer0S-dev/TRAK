<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();

if ($user['role'] === 'admin') {
    $users = $pdo->query('SELECT id, username, email, phone FROM users ORDER BY username COLLATE NOCASE')->fetchAll();
    $traks = $pdo->query('SELECT id, user_id, trak_id, phone, api_key, osmand_url FROM trak_boxes ORDER BY trak_id COLLATE NOCASE')->fetchAll();
} else {
    $stmt = $pdo->prepare('SELECT id, username, email, phone FROM users WHERE id = ?');
    $stmt->execute([(int)$user['id']]);
    $users = $stmt->fetchAll();
    $stmt = $pdo->prepare('SELECT id, user_id, trak_id, phone, api_key, osmand_url FROM trak_boxes WHERE user_id = ? ORDER BY trak_id COLLATE NOCASE');
    $stmt->execute([(int)$user['id']]);
    $traks = $stmt->fetchAll();
}

page_header('API', $user);
?>
<div class="api-page">

    <section class="api-hero">
        <div>
            <div class="eyebrow">API / PROVISIONING</div>
            <h2>Configuration TRAK</h2>
            <p>Sélectionnez un utilisateur puis son TRAK pour afficher uniquement les informations associées.</p>
        </div>
        <div class="api-hero-badge">TRAK Connect</div>
    </section>

    <section class="card api-selector-card">
        <div class="section-heading">
            <div>
                <span class="section-kicker">CIBLE</span>
                <h3>Sélectionner un TRAK</h3>
            </div>
            <span class="selection-hint">1. Utilisateur &nbsp;→&nbsp; 2. TRAK ID</span>
        </div>

        <div class="api-select-grid">
            <label>
                <span>User ID</span>
                <select id="apiUserSelect">
                    <option value="">Sélectionner un utilisateur</option>
                    <?php foreach ($users as $u): ?>
                        <option value="<?= (int)$u['id'] ?>">
                            <?= (int)$u['id'] ?> — <?= htmlspecialchars((string)$u['username'], ENT_QUOTES, 'UTF-8') ?>
                        </option>
                    <?php endforeach; ?>
                </select>
            </label>

            <label>
                <span>TRAK ID</span>
                <select id="apiTrakSelect" disabled>
                    <option value="">Sélectionner un TRAK</option>
                </select>
            </label>
        </div>

        <div id="emptyTrakHint" class="selector-empty">Sélectionnez un User ID pour afficher ses TRAK.</div>
    </section>

    <section id="apiSelection" class="api-selection" hidden>

        <div class="api-identity">
            <div class="api-identity-main">
                <span class="section-kicker">TRAK SÉLECTIONNÉ</span>
                <h2 id="selectedTrakId"></h2>
                <p id="selectedUsername"></p>
            </div>
            <div class="api-identity-meta">
                <div><span>User ID</span><strong id="selectedUserId"></strong></div>
                <div><span>Téléphone TRAK</span><strong id="selectedTrakPhone"></strong></div>
            </div>
        </div>

        <div class="api-info-grid">
            <div class="card api-info-card">
                <div class="card-icon">U</div>
                <div>
                    <span class="detail-label">Utilisateur</span>
                    <strong id="selectedUserNameCard"></strong>
                    <small id="selectedEmail"></small>
                    <small id="selectedUserPhone"></small>
                </div>
            </div>

            <div class="card api-info-card">
                <div class="card-icon">T</div>
                <div>
                    <span class="detail-label">TRAK</span>
                    <strong id="selectedTrakPhoneCard"></strong>
                    <small>TRAK ID : <code id="selectedTrakIdCard"></code></small>
                    <small>User ID : <code id="selectedUserIdCard"></code></small>
                </div>
            </div>

            <div class="card api-info-card api-secret-card">
                <div class="card-icon">K</div>
                <div class="secret-content">
                    <span class="detail-label">API key</span>
                    <div class="secret-row">
                        <code id="selectedApiKey"></code>
                        <button type="button" class="mini-copy" id="copyApiKey">Copier</button>
                    </div>
                </div>
            </div>

            <div class="card api-info-card">
                <div class="card-icon">↗</div>
                <div>
                    <span class="detail-label">OsmAnd / Trakserver</span>
                    <a id="selectedOsmand" href="#" target="_blank" rel="noopener"></a>
                </div>
            </div>
        </div>

        <div class="api-two-columns">
            <div class="card api-sms-card">
                <div class="section-heading compact">
                    <div>
                        <span class="section-kicker">PROVISIONING</span>
                        <h3>SMS de configuration</h3>
                    </div>
                    <span class="status-pill neutral">À définir</span>
                </div>
                <p class="muted">Le format SMS sera défini avant l'activation du provisioning. Le champ reste volontairement vide.</p>
                <textarea id="configSms" class="sms-config" rows="6" placeholder=""></textarea>
                <div class="sms-actions">
                    <button type="button" id="copySms" class="copy-button">Copier le SMS</button>
                    <span id="copyStatus" class="copy-status"></span>
                </div>
            </div>

            <div class="card api-contract-card">
                <div class="section-heading compact">
                    <div>
                        <span class="section-kicker">CONTRAT API</span>
                        <h3>Données prévues</h3>
                    </div>
                </div>
                <div class="contract-list">
                    <div><strong>Position</strong><span>GPS, altitude, vitesse, cap</span></div>
                    <div><strong>GNSS</strong><span>Satellites visibles / utilisés + constellations</span></div>
                    <div><strong>Réseau</strong><span>Wi-Fi, 4G, SSID, opérateur, IP</span></div>
                    <div><strong>État</strong><span>Motion, sentinel, statut TRAK</span></div>
                    <div><strong>Télémétrie</strong><span>Batterie, alimentation, température</span></div>
                    <div><strong>Événements</strong><span>Événement + horodatage</span></div>
                    <div><strong>Commandes</strong><span>Dashboard → TRAK + ACK / résultat</span></div>
                    <div><strong>Configuration</strong><span>Paramètres TRAK selon les droits</span></div>
                </div>
            </div>
        </div>
    </section>
</div>

<script>
const apiUsers = <?= json_encode($users, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) ?>;
const apiTraks = <?= json_encode($traks, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES) ?>;

const userSelect = document.getElementById('apiUserSelect');
const trakSelect = document.getElementById('apiTrakSelect');
const selection = document.getElementById('apiSelection');
const emptyHint = document.getElementById('emptyTrakHint');

function resetSelection() {
    selection.hidden = true;
    emptyHint.textContent = userSelect.value
        ? 'Sélectionnez un TRAK ID pour afficher sa configuration.'
        : 'Sélectionnez un User ID pour afficher ses TRAK.';
    document.getElementById('configSms').value = '';
    document.getElementById('copyStatus').textContent = '';
}

function fillTraks() {
    const userId = Number(userSelect.value);

    trakSelect.innerHTML = '<option value="">Sélectionner un TRAK</option>';

    apiTraks
        .filter(t => Number(t.user_id) === userId)
        .forEach(t => {
            const option = document.createElement('option');
            option.value = t.id;
            option.textContent = t.trak_id;
            trakSelect.appendChild(option);
        });

    trakSelect.disabled = !userId || trakSelect.options.length === 1;
    resetSelection();
}

function displaySelection() {
    const userId = Number(userSelect.value);
    const trak = apiTraks.find(t => Number(t.id) === Number(trakSelect.value));
    const selectedUser = apiUsers.find(u => Number(u.id) === userId);

    if (!selectedUser || !trak) {
        resetSelection();
        return;
    }

    document.getElementById('selectedTrakId').textContent = trak.trak_id;
    document.getElementById('selectedUsername').textContent = selectedUser.username;
    document.getElementById('selectedUserId').textContent = selectedUser.id;
    document.getElementById('selectedTrakPhone').textContent = trak.phone;

    document.getElementById('selectedUserNameCard').textContent = selectedUser.username;
    document.getElementById('selectedEmail').textContent = selectedUser.email || 'Email non renseigné';
    document.getElementById('selectedUserPhone').textContent = selectedUser.phone || 'Téléphone non renseigné';

    document.getElementById('selectedTrakPhoneCard').textContent = trak.phone || 'Téléphone non renseigné';
    document.getElementById('selectedTrakIdCard').textContent = trak.trak_id;
    document.getElementById('selectedUserIdCard').textContent = selectedUser.id;

    document.getElementById('selectedApiKey').textContent = trak.api_key;
    const osmand = document.getElementById('selectedOsmand');
    osmand.textContent = trak.osmand_url;
    osmand.href = trak.osmand_url;

    document.getElementById('configSms').value = '';
    document.getElementById('copyStatus').textContent = '';
    emptyHint.textContent = '';
    selection.hidden = false;
}

userSelect.addEventListener('change', fillTraks);
trakSelect.addEventListener('change', displaySelection);

async function copyText(value, statusElement, emptyMessage) {
    if (!value) {
        statusElement.textContent = emptyMessage;
        return;
    }

    try {
        await navigator.clipboard.writeText(value);
    } catch (e) {
        const helper = document.createElement('textarea');
        helper.value = value;
        document.body.appendChild(helper);
        helper.select();
        document.execCommand('copy');
        helper.remove();
    }

    statusElement.textContent = 'Copié.';
    setTimeout(() => statusElement.textContent = '', 1800);
}

document.getElementById('copySms').addEventListener('click', () => {
    copyText(
        document.getElementById('configSms').value,
        document.getElementById('copyStatus'),
        'Le champ SMS est vide pour le moment.'
    );
});

document.getElementById('copyApiKey').addEventListener('click', (event) => {
    const button = event.currentTarget;
    const value = document.getElementById('selectedApiKey').textContent;
    copyText(value, button, 'Aucune API key.');
    const old = button.textContent;
    button.textContent = 'Copié';
    setTimeout(() => button.textContent = old, 1400);
});
</script>
<?php page_footer(); ?>
