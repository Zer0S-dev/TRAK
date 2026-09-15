/* TRAK 3.0.5 — Wi-Fi dashboard module */

const WIFI_API = 'api/wifi/';
let wifiProfiles = [];

async function loadWifiProfiles() {
    try {
        const data = await api(WIFI_API);
        if (data?.ok === true) {
            wifiProfiles = Array.isArray(data.profiles) ? data.profiles : [];
            renderWifiProfiles();
            renderWifiSlots();
        }
    } catch (error) {
        if (error.message !== 'unauthorized') console.warn('[TRAK] Wi-Fi:', error);
    }
}

function renderWifiProfiles() {
    const preview = $('wifiProfilesPreview');
    const summary = $('wifiSummary');
    const configured = wifiProfiles.filter(profile => profile?.configured === true || profile?.ssid);

    if (summary) {
        summary.textContent = configured.length === 0
            ? 'Aucun réseau enregistré.'
            : configured.length + ' réseau' + (configured.length > 1 ? 'x' : '') + ' enregistré' + (configured.length > 1 ? 's' : '') + '.';
    }

    if (!preview) return;
    preview.innerHTML = '';

    if (configured.length === 0) {
        const empty = document.createElement('div');
        empty.className = 'wifi-profile-empty';
        empty.textContent = 'Aucun réseau Wi-Fi configuré.';
        preview.appendChild(empty);
        return;
    }

    configured.forEach(profile => {
        const row = document.createElement('div');
        row.className = 'wifi-profile-row';
        row.innerHTML = '<div><strong>Wi-Fi ' + (Number(profile.slot) + 1) + '</strong><div>' +
            escapeWifiHtml(profile.ssid || '') + '</div></div>';
        preview.appendChild(row);
    });
}

function renderWifiSlots() {
    const box = $('wifiSlots');
    if (!box) return;
    box.innerHTML = '';

    wifiProfiles.forEach(profile => {
        const row = document.createElement('div');
        row.className = 'wifi-slot';
        const configured = profile.configured === true || !!profile.ssid;
        row.innerHTML = '<div><strong>Wi-Fi ' + (Number(profile.slot) + 1) + '</strong><div>' +
            escapeWifiHtml(profile.ssid || 'Aucun réseau') + ' · ' + (configured ? 'Configuré' : 'Non configuré') +
            '</div></div><div class="wifi-slot-actions"><button type="button" class="btn-secondary" onclick="editWifiProfile(' + Number(profile.slot) + ')">' +
            (configured ? 'Modifier' : 'Configurer') + '</button>' +
            (configured ? '<button type="button" class="wifi-mini-btn danger" onclick="deleteWifiProfile(' + Number(profile.slot) + ')">Supprimer</button>' : '') +
            '</div>';
        box.appendChild(row);
    });
}

function escapeWifiHtml(value) {
    return String(value).replace(/[&<>'"]/g, char => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'
    })[char]);
}

async function openWifiModal() {
    $('wifiModal')?.classList.add('active', 'open');
    clearWifiForm();
    await loadWifiProfiles();
}
function closeWifiModal() { $('wifiModal')?.classList.remove('active', 'open'); }
function wifiModalBackdrop(event) { if (event.target === $('wifiModal')) closeWifiModal(); }
function toggleWifiPassword() {
    const input = $('wifiPassword');
    if (input) input.type = input.type === 'password' ? 'text' : 'password';
}
function editWifiProfile(slot) {
    const p = wifiProfiles.find(x => Number(x.slot) === Number(slot));
    $('wifiSlot').value = String(slot);
    $('wifiSsid').value = p?.ssid || '';
    $('wifiPassword').value = '';
    const title = $('wifiFormTitle');
    if (title) title.textContent = 'Réseau Wi-Fi ' + (Number(slot) + 1);
    const hint = $('wifiFormHint');
    if (hint) hint.textContent = p?.configured ?
        'Laissez le mot de passe vide pour conserver celui déjà enregistré.' :
        'Enregistrez le SSID et le mot de passe du réseau.';
}
async function saveWifiProfile() {
    if (!csrf) { alert('Session de sécurité indisponible. Rechargez la page.'); return; }
    const slot = Number($('wifiSlot')?.value);
    const ssid = $('wifiSsid')?.value.trim() || '';
    const password = $('wifiPassword')?.value || '';
    if (!Number.isInteger(slot) || slot < 0 || slot > 2 || !ssid) { alert('SSID obligatoire.'); return; }
    try {
        const body = { slot, ssid };
        if (password) body.password = password;
        const data = await api(WIFI_API, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify(body)
        });
        if (data?.ok === true) {
            wifiProfiles = data.profiles || [];
            renderWifiProfiles();
            renderWifiSlots();
            clearWifiForm();
            alert('Réseau Wi-Fi enregistré. Le TRAK le récupérera lors de sa prochaine synchronisation.');
        }
    } catch (error) {
        if (error.message !== 'unauthorized') alert(error.message);
    }
}

async function deleteWifiProfile(slot) {
    if (!csrf) { alert('Session de sécurité indisponible. Rechargez la page.'); return; }
    const profile = wifiProfiles.find(x => Number(x.slot) === Number(slot));
    if (!profile?.ssid) return;
    if (!confirm('Supprimer le réseau « ' + profile.ssid + ' » ?')) return;

    try {
        const data = await api(WIFI_API, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify({ action: 'delete', slot: Number(slot) })
        });
        if (data?.ok === true) {
            wifiProfiles = data.profiles || [];
            renderWifiProfiles();
            renderWifiSlots();
            clearWifiForm();
        }
    } catch (error) {
        if (error.message !== 'unauthorized') alert(error.message);
    }
}

function clearWifiForm() {
    ['wifiSlot', 'wifiSsid', 'wifiPassword'].forEach(id => { const el = $(id); if (el) el.value = ''; });
    const title = $('wifiFormTitle');
    if (title) title.textContent = 'Enregistrer un réseau';
    const hint = $('wifiFormHint');
    if (hint) hint.textContent = 'Jusqu’à 3 réseaux peuvent être mémorisés dans le TRAK.';
}
