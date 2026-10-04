(() => {
  const root = document.querySelector('.api-page');
  if (!root) return;

  const apiUsers = JSON.parse(root.dataset.apiUsers || '[]');
  const apiTraks = JSON.parse(root.dataset.apiTraks || '[]');


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
async function copyText(value, button, emptyMessage) {
  const text = String(value || '');
  if (!text.trim()) {
    if (button) button.textContent = emptyMessage || 'Vide.';
    return false;
  }

  try {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
    } else {
      const helper = document.createElement('textarea');
      helper.value = text;
      helper.setAttribute('readonly', '');
      helper.style.position = 'fixed';
      helper.style.opacity = '0';
      helper.style.pointerEvents = 'none';
      document.body.appendChild(helper);
      helper.focus();
      helper.select();
      helper.setSelectionRange(0, helper.value.length);
      const copied = document.execCommand('copy');
      helper.remove();
      if (!copied) throw new Error('copy_failed');
    }

    if (button) {
      const original = button.textContent;
      button.textContent = 'Copié !';
      button.disabled = true;
      window.setTimeout(() => {
        button.textContent = original;
        button.disabled = false;
      }, 1200);
    }
    return true;
  } catch (_) {
    if (button) {
      const original = button.textContent;
      button.textContent = 'Sélectionner';
      window.setTimeout(() => { button.textContent = original; }, 1200);
    }
    return false;
  }
}

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

})();
