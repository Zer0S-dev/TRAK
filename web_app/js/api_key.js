/* TRAK Connect API-key provisioning */

function ensureApiKeySection() {
    const settings = $('view-settings');
    if (!settings || $('trakApiKeySection')) return;

    const section = document.createElement('div');
    section.className = 'section';
    section.id = 'trakApiKeySection';
    section.innerHTML = `
        <div class="section-title">🔑 TRAK Connect</div>
        <div class="flat-list">
            <div class="flat-row" style="display:block">
                <div class="flat-label">Identification du TRAK</div>
                <div class="flat-description">Saisissez le TRAK ID affiché dans le Wizard, puis collez la clé API générée par ce même Wizard.</div>
                <input id="trakIdInput" type="text" maxlength="64" autocomplete="off" placeholder="TRACK-XXXXXX">
                <div class="flat-label" style="margin-top:10px">Clé API du TRAK</div>
                <input id="trakApiKeyInput" type="password" maxlength="17" autocomplete="off" placeholder="TRAK_7f3a91c84e2b">
                <div class="sentinel-phone-actions">
                    <button class="btn-primary" id="trakApiKeySave" onclick="saveTrakApiKey()">Enregistrer la clé</button>
                </div>
                <div class="flat-description" id="trakApiKeyStatus" style="margin-top:8px">TRAK non configuré.</div>
            </div>
        </div>`;

    settings.appendChild(section);
}

function currentTrakId() {
    const input = $('trakIdInput');
    const detected = lastData?.trak_id || '';
    if (input && detected && !input.value.trim()) input.value = detected;
    return input?.value.trim() || detected;
}

async function loadApiKeySettings() {
    ensureApiKeySection();
    const status = $('trakApiKeyStatus');
    const trakId = currentTrakId();
    if (!trakId) {
        if (status) status.textContent = 'Saisissez le TRAK ID affiché dans le Wizard.';
        return;
    }

    try {
        const data = await api('api/trak/key/?trak_id=' + encodeURIComponent(trakId));
        if (status) status.textContent = data.configured
            ? '✓ Clé API enregistrée pour ' + trakId
            : 'Aucune clé API enregistrée pour ' + trakId;
    } catch (error) {
        if (error.message !== 'unauthorized' && status) status.textContent = 'Impossible de vérifier la clé API.';
    }
}

async function saveTrakApiKey() {
    const idInput = $('trakIdInput');
    const input = $('trakApiKeyInput');
    const status = $('trakApiKeyStatus');
    const trakId = idInput?.value.trim() || lastData?.trak_id || '';
    const key = input?.value.trim() || '';

    if (!/^[A-Za-z0-9._-]{1,64}$/.test(trakId)) {
        alert('TRAK ID invalide. Utilisez celui affiché dans le Wizard, par exemple TRACK-123456.');
        return;
    }
    if (!/^TRAK_[0-9a-fA-F]{12}$/.test(key)) {
        alert('Clé API invalide. Format attendu : TRAK_7f3a91c84e2b');
        return;
    }
    if (!csrf) {
        alert('Session de sécurité indisponible. Rechargez la page.');
        return;
    }

    try {
        if (status) status.textContent = 'Enregistrement de la clé...';
        await api('api/trak/key/', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify({ trak_id: trakId, api_key: key })
        });
        input.value = '';
        if (status) status.textContent = '✓ Clé enregistrée pour ' + trakId + '. Seul son hash est conservé dans SQLite.';
    } catch (error) {
        if (error.message !== 'unauthorized') {
            if (status) status.textContent = 'Erreur : ' + error.message;
        }
    }
}
