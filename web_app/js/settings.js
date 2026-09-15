/* TRAK 3.0.5 — settings module */

async function loadTrackserver() {
    try {
        const data = await api(API_TRACKSERVER);
        const input = $('trackserverUrl');
        if (input) input.value = data.url || '';
    } catch (error) {
        if (error.message !== 'unauthorized') console.warn('[TRAK] Trackserver:', error);
    }
}

async function saveTrackserver() {
    const input = $('trackserverUrl');
    if (!input) return;
    const url = input.value.trim();
    if (!csrf) {
        alert('Session de sécurité indisponible. Rechargez la page.');
        return;
    }
    try {
        await api(API_TRACKSERVER, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify({ url })
        });
        input.style.borderColor = 'var(--success)';
        window.setTimeout(() => { input.style.borderColor = ''; }, 900);
    } catch (error) {
        if (error.message !== 'unauthorized') {
            input.style.borderColor = 'var(--danger)';
            alert(error.message);
        }
    }
}

function formatInterval(seconds) {
    seconds = Number(seconds);
    if (seconds >= 3600) return (seconds / 3600) + ' h';
    if (seconds >= 60) return (seconds / 60) + ' min';
    return seconds + ' s';
}
function activeFromSlider(level) { const i = Math.max(1, Math.min(4, Number(level))) - 1; return ACTIVE_INTERVALS[i] || 10; }
function activeSliderFromSeconds(seconds) { const i = ACTIVE_INTERVALS.indexOf(Number(seconds)); return i >= 0 ? i + 1 : 2; }
function idleFromSlider(level) { const i = Math.max(1, Math.min(5, Number(level))) - 1; return IDLE_INTERVALS[i] || 60; }
function idleSliderFromSeconds(seconds) { const i = IDLE_INTERVALS.indexOf(Number(seconds)); return i >= 0 ? i + 1 : 2; }
function sensitivityFromSlider(level) { const i = Math.max(1, Math.min(5, Number(level))) - 1; return GYRO_SENSITIVITY_LEVELS[i] || 3; }
function sensitivitySliderFromLevel(level) { const n = Number(level); return GYRO_SENSITIVITY_LEVELS.includes(n) ? n : 3; }

function setActiveIntervalUi(seconds) {
    const safe = ACTIVE_INTERVALS.includes(Number(seconds)) ? Number(seconds) : 10;
    const slider = $('recordIntervalSlider');
    if (slider) slider.value = String(activeSliderFromSeconds(safe));
    setText('recordIntervalValue', safe + 's');
}
function setIdleIntervalUi(seconds) {
    const safe = IDLE_INTERVALS.includes(Number(seconds)) ? Number(seconds) : 60;
    const slider = $('idleIntervalSlider');
    if (slider) slider.value = String(idleSliderFromSeconds(safe));
    setText('idleIntervalValue', formatInterval(safe));
}
function setSensitivityUi(level) {
    const safe = sensitivitySliderFromLevel(level);
    const slider = $('motionSensitivitySlider');
    if (slider) slider.value = String(safe);
    setText('motionSensitivityValue', GYRO_SENSITIVITY_LABELS[safe - 1]);
}

async function loadRecordInterval() {
    try {
        const data = await api(API_INTERVAL);
        if (data?.ok === true) {
            setActiveIntervalUi(data.active_interval_seconds);
            setIdleIntervalUi(data.idle_interval_seconds);
            setSensitivityUi(data.sensitivity_level);
        }
    } catch (error) {
        if (error.message !== 'unauthorized') console.warn('[TRAK] Reglages mouvement:', error);
    }
}
function previewRecordInterval(level) { setActiveIntervalUi(activeFromSlider(level)); }
function previewIdleInterval(level) { setIdleIntervalUi(idleFromSlider(level)); }
function previewMotionSensitivity(level) { setSensitivityUi(sensitivityFromSlider(level)); }

async function saveMotionSettings(activeSeconds, idleSeconds, sensitivityLevel) {
    if (!csrf) { alert('Session de sécurité indisponible. Rechargez la page.'); return false; }
    try {
        const current = await api(API_INTERVAL);
        const active = ACTIVE_INTERVALS.includes(Number(activeSeconds)) ? Number(activeSeconds) : Number(current.active_interval_seconds || 10);
        const idle = IDLE_INTERVALS.includes(Number(idleSeconds)) ? Number(idleSeconds) : Number(current.idle_interval_seconds || 60);
        const sensitivity = GYRO_SENSITIVITY_LEVELS.includes(Number(sensitivityLevel)) ? Number(sensitivityLevel) : Number(current.sensitivity_level || 3);
        const data = await api(API_INTERVAL, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json', 'X-CSRF-Token': csrf },
            body: JSON.stringify({ active_interval_seconds: active, idle_interval_seconds: idle, sensitivity_level: sensitivity })
        });
        if (data?.ok === true) {
            setActiveIntervalUi(data.active_interval_seconds);
            setIdleIntervalUi(data.idle_interval_seconds);
            setSensitivityUi(data.sensitivity_level);
            return true;
        }
    } catch (error) {
        if (error.message !== 'unauthorized') console.warn('[TRAK] Enregistrement reglages mouvement:', error);
    }
    alert('Impossible d’enregistrer les réglages mouvement.');
    await loadRecordInterval();
    return false;
}
async function saveRecordInterval(level) {
    const active = activeFromSlider(level);
    const idle = Number($('idleIntervalSlider')?.value) ? idleFromSlider($('idleIntervalSlider').value) : 60;
    const sensitivity = Number($('motionSensitivitySlider')?.value) || 3;
    await saveMotionSettings(active, idle, sensitivity);
}
async function saveIdleInterval(level) {
    const active = Number($('recordIntervalSlider')?.value) ? activeFromSlider($('recordIntervalSlider').value) : 10;
    const idle = idleFromSlider(level);
    const sensitivity = Number($('motionSensitivitySlider')?.value) || 3;
    await saveMotionSettings(active, idle, sensitivity);
}
async function saveMotionSensitivity(level) {
    const active = Number($('recordIntervalSlider')?.value) ? activeFromSlider($('recordIntervalSlider').value) : 10;
    const idle = Number($('idleIntervalSlider')?.value) ? idleFromSlider($('idleIntervalSlider').value) : 60;
    const sensitivity = sensitivityFromSlider(level || $('motionSensitivitySlider')?.value || 3);
    await saveMotionSettings(active, idle, sensitivity);
}
