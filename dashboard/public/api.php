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

function api_value(string $value): string {
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}
?>
<div class="card">
    <h2>Sélection API</h2>
    <p class="muted">Sélectionnez d'abord le <strong>User ID</strong>, puis le <strong>TRAK ID</strong>. Les informations affichées dessous correspondent uniquement à la sélection.</p>
    <div class="api-select-grid">
        <label>User ID
            <select id="apiUserSelect">
                <option value="">Sélectionner un User ID</option>
                <?php foreach ($users as $u): ?>
                    <option value="<?= (int)$u['id'] ?>"><?= (int)$u['id'] ?> — <?=api_value((string)$u['username'])?></option>
                <?php endforeach; ?>
            </select>
        </label>
        <label>TRAK ID
            <select id="apiTrakSelect" disabled>
                <option value="">Sélectionner un TRAK ID</option>
            </select>
        </label>
    </div>
</div>

<div id="apiSelection" class="api-selection" hidden>
    <div class="card">
        <h2>Identifiants sélectionnés</h2>
        <div class="api-details-grid">
            <div><span class="detail-label">User ID</span><code id="selectedUserId"></code></div>
            <div><span class="detail-label">Utilisateur</span><code id="selectedUsername"></code></div>
            <div><span class="detail-label">Email</span><span id="selectedEmail"></span></div>
            <div><span class="detail-label">Téléphone utilisateur</span><span id="selectedUserPhone"></span></div>
            <div><span class="detail-label">TRAK ID</span><code id="selectedTrakId"></code></div>
            <div><span class="detail-label">Téléphone TRAK</span><span id="selectedTrakPhone"></span></div>
            <div><span class="detail-label">API key</span><code id="selectedApiKey"></code></div>
            <div><span class="detail-label">OsmAnd / Trakserver</span><code id="selectedOsmand"></code></div>
        </div>
    </div>

    <div class="card api-card">
        <h2>SMS de configuration</h2>
        <p class="muted">Format volontairement laissé vide pour le moment. Il sera défini avant la mise en place du provisioning.</p>
        <textarea id="configSms" class="sms-config" rows="7" placeholder=""></textarea>
        <button type="button" id="copySms" class="copy-button">Copier</button>
        <div id="copyStatus" class="muted copy-status"></div>
    </div>

    <div class="card api-card">
        <h2>Données servies / utilisables par l'API</h2>
        <p class="muted">Ces données constituent le contrat prévu pour le TRAK sélectionné.</p>
        <div class="table-wrap">
            <table class="data-table">
                <thead><tr><th>Domaine</th><th>Donnée</th><th>Type</th><th>Accès</th></tr></thead>
                <tbody>
                    <tr><td>Identité</td><td>User ID</td><td>entier</td><td>lecture</td></tr>
                    <tr><td>Identité</td><td>TRAK ID</td><td>texte</td><td>lecture / ciblage</td></tr>
                    <tr><td>Identité</td><td>Téléphone TRAK</td><td>texte</td><td>lecture</td></tr>
                    <tr><td>Authentification</td><td>API key</td><td>secret</td><td>authentification</td></tr>
                    <tr><td>Position</td><td>latitude / longitude</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Position</td><td>altitude / vitesse / cap</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>GNSS</td><td>satellites visibles / utilisés</td><td>entier</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>GNSS</td><td>GPS / GLONASS / Galileo / BeiDou</td><td>entiers</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Réseau</td><td>Wi-Fi / 4G / SSID / opérateur / IP</td><td>texte / état</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>État</td><td>motion / sentinel</td><td>état</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Télémétrie</td><td>batterie / alimentation / température</td><td>nombre / état</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Événements</td><td>événement + horodatage</td><td>texte + date</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Commandes</td><td>commande dashboard → TRAK</td><td>texte</td><td>écriture dashboard / lecture TRAK</td></tr>
                    <tr><td>Commandes</td><td>ACK / résultat</td><td>texte / état</td><td>lecture / écriture TRAK</td></tr>
                    <tr><td>Configuration</td><td>configuration TRAK</td><td>paramètres</td><td>lecture / écriture selon droit</td></tr>
                </tbody>
            </table>
        </div>
    </div>
</div>

<script>
const apiUsers = <?=json_encode($users, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)?>;
const apiTraks = <?=json_encode($traks, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES)?>;

const userSelect = document.getElementById('apiUserSelect');
const trakSelect = document.getElementById('apiTrakSelect');
const selection = document.getElementById('apiSelection');

function resetSelection() {
    selection.hidden = true;
    document.getElementById('configSms').value = '';
    document.getElementById('copyStatus').textContent = '';
}

function fillTraks() {
    const userId = Number(userSelect.value);
    trakSelect.innerHTML = '<option value="">Sélectionner un TRAK ID</option>';
    apiTraks.filter(t => Number(t.user_id) === userId).forEach(t => {
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
    document.getElementById('selectedUserId').textContent = selectedUser.id;
    document.getElementById('selectedUsername').textContent = selectedUser.username;
    document.getElementById('selectedEmail').textContent = selectedUser.email || 'non renseigné';
    document.getElementById('selectedUserPhone').textContent = selectedUser.phone || 'non renseigné';
    document.getElementById('selectedTrakId').textContent = trak.trak_id;
    document.getElementById('selectedTrakPhone').textContent = trak.phone;
    document.getElementById('selectedApiKey').textContent = trak.api_key;
    document.getElementById('selectedOsmand').textContent = trak.osmand_url;
    document.getElementById('configSms').value = '';
    document.getElementById('copyStatus').textContent = '';
    selection.hidden = false;
}

userSelect.addEventListener('change', fillTraks);
trakSelect.addEventListener('change', displaySelection);

document.getElementById('copySms').addEventListener('click', async () => {
    const sms = document.getElementById('configSms').value;
    if (!sms) {
        document.getElementById('copyStatus').textContent = 'Le champ SMS est vide pour le moment.';
        return;
    }
    try {
        await navigator.clipboard.writeText(sms);
        document.getElementById('copyStatus').textContent = 'SMS copié.';
    } catch (e) {
        document.getElementById('configSms').select();
        document.execCommand('copy');
        document.getElementById('copyStatus').textContent = 'SMS copié.';
    }
});
</script>
<?php page_footer(); ?>