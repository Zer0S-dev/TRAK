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
<div class="api-page">
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
  emptyHint.textContent = userSelect.value ? 'Sélectionnez un TRAK ID pour afficher sa configuration.' : 'Sélectionnez un User ID pour afficher ses TRAK.';
  document.getElementById('configSmsList').innerHTML = '<div class="muted">Sélectionnez un TRAK pour générer les SMS.</div>';
}
function fillTraks() {
  const userId = String(userSelect.value || '').trim();
  trakSelect.innerHTML = '<option value="">Sélectionner un TRAK</option>';
  const matchingTraks = apiTraks.filter(t => String(t.user_id ?? '').trim() === userId);
  matchingTraks.forEach(t => { const option = document.createElement('option'); option.value = t.id; option.textContent = t.trak_id; trakSelect.appendChild(option); });
  trakSelect.disabled = !userId || matchingTraks.length === 0;
  emptyHint.textContent = !userId ? 'Sélectionnez un User ID pour afficher ses TRAK.' : matchingTraks.length === 0 ? 'Aucun TRAK n’est associé à cet utilisateur. Vérifiez le User ID dans TRAK Box.' : 'Sélectionnez un TRAK ID pour afficher sa configuration.';
  resetSelection();
}
function randomNonce16() {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  return Array.from(bytes).map(byte => byte.toString(16).padStart(2, '0')).join('');
}
function buildConfigId(trak) { return 'CFG-' + trak.trak_id + '-' + Date.now(); }
function renderSms(label, value, smsList) {
  const row = document.createElement('div');
  row.className = 'sms-config-item';
  const title = document.createElement('div');
  title.className = 'sms-config-label';
  title.textContent = label;
  const textarea = document.createElement('textarea');
  textarea.className = 'sms-config';
  textarea.rows = 4;
  textarea.readOnly = true;
  textarea.value = value;
  const actions = document.createElement('div');
  actions.className = 'sms-actions';
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'copy-button';
  button.textContent = 'Copier ' + label;
  button.addEventListener('click', () => copyText(textarea.value, button, 'SMS vide.'));
  actions.appendChild(button);
  row.appendChild(title); row.appendChild(textarea); row.appendChild(actions); smsList.appendChild(row);
}
async function displaySelection() {
  const userId = Number(userSelect.value);
  const trak = apiTraks.find(t => Number(t.id) === Number(trakSelect.value));
  const selectedUser = apiUsers.find(u => Number(u.id) === userId);
  if (!selectedUser || !trak) { resetSelection(); return; }
  const smsList = document.getElementById('configSmsList');
  smsList.innerHTML = '';
  if (!selectedUser.phone) {
    const error = document.createElement('div'); error.className = 'alert error';
    error.textContent = 'Le compte utilisateur ne possède pas de numéro de téléphone. USER_PHONE est obligatoire pour le SMS 1.';
    smsList.appendChild(error); selection.hidden = false; selection.setAttribute('aria-hidden', 'false'); return;
  }

  const dashboardUrl = String(trak.dashboard_url || '').trim() || new URL('position.php', window.location.href).href;
  const lines = [
    '// USER','user_id : ' + selectedUser.id,'user_name : ' + selectedUser.username,
    'user_email : ' + (selectedUser.email || selectedUser.pending_email || ''),'user_phone : ' + (selectedUser.phone || ''),
    '','// TRAK BOX','trak_id : ' + trak.trak_id,'trak_phone : ' + (trak.phone || ''),
    'api_key : ' + trak.api_key,'trackserver_url : ' + trak.trakserver_url,'dashboard_url : ' + dashboardUrl
  ];
  document.getElementById('apiDataCode').textContent = lines.join(String.fromCharCode(10));

  const configId = buildConfigId(trak);
  const nonce = randomNonce16();
  renderSms('SMS 1', ['TRAKCFG1','1',configId,trak.trak_id,trak.phone || '',selectedUser.phone || ''].join('|'), smsList);

  if (!/^[A-Za-z0-9]{16}$/.test(String(trak.api_key || ''))) {
    const error = document.createElement('div'); error.className = 'alert error';
    error.textContent = 'Ce TRAK possède une clé API qui ne fait pas exactement 16 caractères. Régénérez-la dans TRAK Box avant de configurer le TRAK.';
    smsList.appendChild(error);
  } else {
    renderSms('SMS 2', ['TRAKCFG3','1',configId,trak.api_key,nonce].join('|'), smsList);
    let smsNumber = 3;
    const url = String(trak.trakserver_url || '');
    const chunkSize = 80;
    if (!url) {
      const error = document.createElement('div'); error.className = 'alert error'; error.textContent = 'trackserver_url est vide.'; smsList.appendChild(error);
    } else if (url.length <= chunkSize) {
      renderSms('SMS ' + smsNumber++, ['TRAKCFG2','1',configId,url,'1'].join('|'), smsList);
    } else if (url.length <= chunkSize * 2) {
      renderSms('SMS ' + smsNumber++, ['TRAKCFG2','1',configId,url.slice(0,chunkSize),'0'].join('|'), smsList);
      renderSms('SMS ' + smsNumber++, ['TRAKCFG2','2',configId,url.slice(chunkSize),'1'].join('|'), smsList);
    } else {
      const error = document.createElement('div'); error.className = 'alert error'; error.textContent = 'trackserver_url est trop longue pour le format prévu sur 2 SMS (maximum 160 caractères).'; smsList.appendChild(error);
    }

    const dashboardChunkSize = 80;
    if (dashboardUrl.length > 160) {
      const error = document.createElement('div'); error.className = 'alert error'; error.textContent = 'L’URL Dashboard dépasse 160 caractères.'; smsList.appendChild(error);
    } else if (dashboardUrl.length <= dashboardChunkSize) {
      renderSms('SMS ' + smsNumber++, ['TRAKCFG4','1',configId,dashboardUrl,'1'].join('|'), smsList);
    } else {
      renderSms('SMS ' + smsNumber++, ['TRAKCFG4','1',configId,dashboardUrl.slice(0,dashboardChunkSize),'0'].join('|'), smsList);
      renderSms('SMS ' + smsNumber++, ['TRAKCFG4','2',configId,dashboardUrl.slice(dashboardChunkSize),'1'].join('|'), smsList);
    }
  }

  emptyHint.textContent = '';
  selection.hidden = false; selection.setAttribute('aria-hidden', 'false');
  document.getElementById('apiDataCard').hidden = false;
}
userSelect.addEventListener('change', fillTraks);
trakSelect.addEventListener('change', displaySelection);
</script>
<?php page_footer(); ?>
