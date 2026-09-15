const $ = (id) => document.getElementById(id);

function show(message) { $('status').textContent = message; }

function showKey(key) {
  if (!key || key.length !== 64) return;
  $('encryptionKey').textContent = key;
  $('keySection').classList.remove('hide');
}

async function loadConfig() {
  try {
    const response = await fetch('/api/config', { cache: 'no-store' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const data = await response.json();
    $('server_url').value = data.server_url || '';
    $('user_phone').value = data.user_phone || '';
    $('trak_phone').value = data.trak_phone || '';
    show('Configuration actuelle chargée.\nTRAK ID : ' + (data.trak_id || '') + '\nProvisionné : ' + (data.provisioned ? 'oui' : 'non'));
    showKey(data.encryption_key);
  } catch (error) { show('Impossible de lire la configuration : ' + error.message); }
}

$('form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const body = new URLSearchParams({ server_url: $('server_url').value, user_phone: $('user_phone').value, trak_phone: $('trak_phone').value });
  try {
    show('Enregistrement...');
    const response = await fetch('/api/config', { method: 'POST', body });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || ('HTTP ' + response.status));
    show('Configuration écrite dans NVS.\nTRAK ID : ' + (data.trak_id || '') + '\nProvisionné : ' + (data.provisioned ? 'oui' : 'non'));
    showKey(data.encryption_key);
  } catch (error) { show('Erreur : ' + error.message); }
});

$('copyKey').addEventListener('click', async () => {
  const key = $('encryptionKey').textContent;
  try { await navigator.clipboard.writeText(key); show('Clé copiée dans le presse-papiers.'); }
  catch (_) { show('Copie automatique impossible. Sélectionne la clé et copie-la manuellement.'); }
});

$('reboot').addEventListener('click', async () => {
  $('form').requestSubmit();
  await new Promise(resolve => setTimeout(resolve, 700));
  try { await fetch('/api/reboot', { method: 'POST' }); show('Redémarrage du TRAK...'); }
  catch (_) { show('Le TRAK redémarre. Reconnecte-toi à son réseau normal après le reboot.'); }
});

loadConfig();
