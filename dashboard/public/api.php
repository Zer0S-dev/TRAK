<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();

if ($user['role'] === 'admin') {
    $users = $pdo->query('SELECT id, username, email, pending_email, phone FROM users ORDER BY username COLLATE NOCASE')->fetchAll();
    $traks = $pdo->query('SELECT id, user_id, trak_id, phone, api_key, trakserver_url FROM trak_boxes ORDER BY trak_id COLLATE NOCASE')->fetchAll();
} else {
    $stmt = $pdo->prepare('SELECT id, username, email, pending_email, phone FROM users WHERE id = ?');
    $stmt->execute([(int)$user['id']]);
    $users = $stmt->fetchAll();
    $stmt = $pdo->prepare('SELECT id, user_id, trak_id, phone, api_key, trakserver_url FROM trak_boxes WHERE user_id = ? ORDER BY trak_id COLLATE NOCASE');
    $stmt->execute([(int)$user['id']]);
    $traks = $stmt->fetchAll();
}

page_header('API', $user);
?>
<div class="api-page">

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

    <section id="apiSelection" class="api-selection" hidden aria-hidden="true">

        <div id="apiDataCard" class="card api-data-card" hidden>
<div class="section-heading compact"><div><span class="section-kicker">PAYLOAD</span><h3>Données TRAK</h3></div></div>
<pre id="apiDataCode" class="api-data-code"><code></code></pre>
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
    selection.setAttribute('aria-hidden', 'true');
    document.getElementById('apiDataCard').hidden = true;
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

    const lines = [
        '// USER',
        'user_id : ' + selectedUser.id,
        'user_name : ' + selectedUser.username,
        'user_email : ' + (selectedUser.email || selectedUser.pending_email || ''),
        'user_phone : ' + (selectedUser.phone || ''),
        '',
        '// TRAK BOX',
        'trak_id : ' + trak.trak_id,
        'trak_phone : ' + (trak.phone || ''),
        'api_key : ' + trak.api_key,
        'trakserver_url : ' + trak.trakserver_url
    ];
    document.getElementById('apiDataCode').textContent = lines.join(String.fromCharCode(10));

    document.getElementById('configSms').value = '';
    document.getElementById('copyStatus').textContent = '';
    emptyHint.textContent = '';
    selection.hidden = false;
    selection.setAttribute('aria-hidden', 'false');
    document.getElementById('apiDataCard').hidden = false;
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

</script>
<?php page_footer(); ?>
