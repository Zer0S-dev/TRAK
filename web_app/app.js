/* TRAK 3.0 — dashboard runtime
 * 3.0.4: active/idle recording intervals + LSM6DS3 sensitivity.
 * Existing dashboard HTML/CSS remains the visual base.
 */

let trackerMap = null;
let trackerMarker = null;
let mapFollow = true;
let lastTrackerPosition = null;
let lastData = null;
let refreshBusy = false;
let consecutiveFailures = 0;
let csrf = '';
let motionReturnEndsAt = 0;

const API_DATA = 'api/data/';
const API_SESSION = 'api/session/';
const API_TRACKSERVER = 'api/trackserver/';
const API_INTERVAL = 'api/trak/interval/';
const REFRESH_INTERVAL_MS = 5000;
const OFFLINE_AFTER_FAILURES = 3;
const ACTIVE_INTERVALS = [5, 10, 15, 20];
const IDLE_INTERVALS = [30, 60, 900, 1800, 3600];
const GYRO_SENSITIVITY_LEVELS = [1, 2, 3, 4, 5];
const GYRO_SENSITIVITY_LABELS = ['2 °/s', '4 °/s', '6 °/s', '10 °/s', '15 °/s'];
const $ = id => document.getElementById(id);

function setText(id, value) {
    const el = $(id);
    if (el) el.textContent = value;
}

function formatBytes(bytes) {
    if (!Number.isFinite(Number(bytes)) || Number(bytes) < 0) return '—';
    bytes = Number(bytes);
    if (bytes < 1000) return bytes + ' o';
    if (bytes < 1000000) return (bytes / 1000).toFixed(1) + ' ko';
    return (bytes / 1000000).toFixed(2) + ' Mo';
}

function networkLabel(data) {
    return data?.network || 'Aucun';
}

function networkIconLabel(data) {
    const network = networkLabel(data).toUpperCase();
    return network.includes('4G') || network.includes('CELL') ? '4G' : network;
}

function redirectToLogin() {
    window.location.replace('login.php');
}

function initTrackerMap() {
    if (trackerMap || typeof L === 'undefined') return;
    const mapElement = $('map');
    if (!mapElement) return;
    trackerMap = L.map(mapElement, {
        zoomControl: false,
        minZoom: 2,
        maxZoom: 19
    }).setView([46.6, 1.89], 6);
    L.control.zoom({
        position: 'bottomright'
    }).addTo(trackerMap);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        minZoom: 2,
        maxZoom: 19,
        maxNativeZoom: 19,
        tileSize: 256,
        attribution: '&copy; OpenStreetMap'
    }).addTo(trackerMap);
    trackerMap.on('dragstart', () => {
        mapFollow = false;
        updateFollowButton();
    });
    window.setTimeout(() => trackerMap?.invalidateSize(), 100);
}

function updateMapPosition(lat, lon) {
    lat = Number(lat);
    lon = Number(lon);
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
    initTrackerMap();
    if (!trackerMap) return;
    const changed = !lastTrackerPosition || lastTrackerPosition[0] !== lat || lastTrackerPosition[1] !== lon;
    lastTrackerPosition = [lat, lon];
    if (!trackerMarker) {
        trackerMarker = L.marker([lat, lon], {
            icon: L.divIcon({
                className: 'trak-marker',
                iconSize: [18, 18],
                iconAnchor: [9, 9]
            })
        }).addTo(trackerMap);
        trackerMap.setView([lat, lon], 15);
        return;
    }
    trackerMarker.setLatLng([lat, lon]);
    if (mapFollow && changed) trackerMap.panTo([lat, lon], {
        animate: true,
        duration: 0.35
    });
}

function toggleFollow() {
    mapFollow = !mapFollow;
    updateFollowButton();
    if (mapFollow && lastTrackerPosition && trackerMap) trackerMap.setView(lastTrackerPosition, Math.max(trackerMap.getZoom(), 15), {
        animate: true
    });
}

function updateFollowButton() {
    $('followBtn')?.classList.toggle('active', mapFollow);
}

function toggleFullMap() {
    const full = document.body.classList.toggle('full-map');
    $('fullMapBtn')?.classList.toggle('active', full);
    window.setTimeout(() => trackerMap?.invalidateSize(), 100);
}

function showView(name, button) {
    document.querySelectorAll('.view').forEach(view => view.classList.remove('active'));
    if (name !== 'home') $('view-' + name)?.classList.add('active');
    document.querySelectorAll('.nav-btn').forEach(btn => btn.classList.remove('active'));
    button?.classList.add('active');
    document.body.classList.remove('full-map');
    $('fullMapBtn')?.classList.remove('active');
    if (name === 'home') window.setTimeout(() => trackerMap?.invalidateSize(), 80);
}

function updateHome(data) {
    const hasFix = data.hasFix === true && Number.isFinite(Number(data.latitude)) && Number.isFinite(Number(data.longitude));
    const online = data.online === true;
    document.body.classList.toggle('state-gps-ok', hasFix);
    document.body.classList.toggle('state-gps-search', !hasFix && online);
    document.body.classList.toggle('state-offline', !online);
    setText('topStatus', !online ? 'TRAK · OFFLINE' : (hasFix ? 'GPS ok' : 'Acquisition GPS...'));
    setText('lat', hasFix ? Number(data.latitude).toFixed(6) + '°' : '—');
    setText('lon', hasFix ? Number(data.longitude).toFixed(6) + '°' : '—');
    setText('alt', Number.isFinite(Number(data.altitude)) ? `Alt: ${Number(data.altitude).toFixed(0)} m` : '—');
    const lastUpdate = data && data.lastUpdate ? new Date(data.lastUpdate) : null;
    setText('lastUpdate', lastUpdate && !Number.isNaN(lastUpdate.getTime()) ? '' + lastUpdate.toLocaleString('fr-FR', {
        dateStyle: 'short',
        timeStyle: 'medium'
    }) : '');
    setText('networkIcon', networkIconLabel(data));
    setText('signalTop', Number.isFinite(Number(data.signalPercent)) ? Math.round(Number(data.signalPercent)) + '%' : '—');
    updateMapPosition(data.latitude, data.longitude);
}

function updateMotionCountdown() {
    const remaining = motionReturnEndsAt > Date.now() ? Math.ceil((motionReturnEndsAt - Date.now()) / 1000) : 0;
    setText('statMotionReturn', remaining > 0 ? `${remaining} s` : '—');
}

function updateStats(data) {
    setText('statFix', data.hasFix ? 'OK' : 'Recherche');
    setText('statSats', Number.isFinite(Number(data.satellites)) ? data.satellites : '—');
    setText('statGpsGlo', data.gpsSatellites == null ? '—' : `${data.gpsSatellites} / ${data.glonassSatellites ?? 0}`);
    setText('statBdsGal', data.beidouSatellites == null ? '—' : `${data.beidouSatellites} / ${data.galileoSatellites ?? 0}`);
    setText('statNetwork', networkLabel(data));
    setText('statSignal', Number.isFinite(Number(data.signalPercent)) ? Math.round(Number(data.signalPercent)) + ' %' : '—');
    setText('statInternet', data.internetAvailable ? 'OK' : 'OFF');
    setText('statTx', Number.isFinite(Number(data.txCount)) ? data.txCount : '—');
    setText('statMotion', data.motionMode === 'MOBILE' ? 'MOBILE' : 'IMMOBILE');
    const returnSeconds = Number(data.motionReturnSeconds || 0);
    motionReturnEndsAt = returnSeconds > 0 ? Date.now() + returnSeconds * 1000 : 0;
    updateMotionCountdown();
    setText('statWifi', '—');
    setText('stat4G', '—');
    setText('statTotal', '—');
    setText('statPlan', '—');
    setText('firmwareVersion', data.firmwareVersion || '—');
    setText('serialNumber', data.serialNumber || '—');
    setText('networkMode', data.online ? '4G' : 'Offline');
    setText('signalSetting', Number.isFinite(Number(data.signalPercent)) ? Math.round(Number(data.signalPercent)) + ' %' : '—');
    setText('data4GSetting', '—');
    setText('dataEstimatedSetting', '—');
}
async function api(url, options = {}) {
    const response = await fetch(url, {
        cache: 'no-store',
        ...options
    });
    if (response.status === 401) {
        redirectToLogin();
        throw new Error('unauthorized');
    }
    if (!response.ok) {
        const message = await response.text().catch(() => '');
        throw new Error(message || `HTTP ${response.status}`);
    }
    return response.json();
}
async function loadSession() {
    const data = await api(API_SESSION);
    if (!data || data.ok !== true || typeof data.csrf !== 'string' || data.csrf.length < 32) throw new Error('Session API invalide');
    csrf = data.csrf;
}
async function refresh() {
    if (refreshBusy) return;
    refreshBusy = true;
    try {
        const data = await api(API_DATA);
        if (!data || data.ok !== true) throw new Error('Réponse API invalide');
        lastData = data;
        consecutiveFailures = 0;
        updateHome(data);
        updateStats(data);
    } catch (error) {
        if (error.message === 'unauthorized') return;
        consecutiveFailures++;
        if (consecutiveFailures >= OFFLINE_AFTER_FAILURES) {
            document.body.classList.remove('state-gps-ok', 'state-gps-search');
            document.body.classList.add('state-offline');
            setText('topStatus', 'TRAK · OFFLINE');
        }
        console.warn('[TRAK] API:', error);
    } finally {
        refreshBusy = false;
    }
}
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
            headers: {
                'Content-Type': 'application/json',
                'X-CSRF-Token': csrf
            },
            body: JSON.stringify({
                url
            })
        });
        input.style.borderColor = 'var(--success)';
        window.setTimeout(() => {
            input.style.borderColor = '';
        }, 900);
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

function activeFromSlider(level) {
    const i = Math.max(1, Math.min(4, Number(level))) - 1;
    return ACTIVE_INTERVALS[i] || 10;
}

function activeSliderFromSeconds(seconds) {
    const i = ACTIVE_INTERVALS.indexOf(Number(seconds));
    return i >= 0 ? i + 1 : 2;
}

function idleFromSlider(level) {
    const i = Math.max(1, Math.min(5, Number(level))) - 1;
    return IDLE_INTERVALS[i] || 60;
}

function idleSliderFromSeconds(seconds) {
    const i = IDLE_INTERVALS.indexOf(Number(seconds));
    return i >= 0 ? i + 1 : 2;
}

function sensitivityFromSlider(level) {
    const i = Math.max(1, Math.min(5, Number(level))) - 1;
    return GYRO_SENSITIVITY_LEVELS[i] || 3;
}

function sensitivitySliderFromLevel(level) {
    const n = Number(level);
    return GYRO_SENSITIVITY_LEVELS.includes(n) ? n : 3;
}

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

function previewRecordInterval(level) {
    setActiveIntervalUi(activeFromSlider(level));
}

function previewIdleInterval(level) {
    setIdleIntervalUi(idleFromSlider(level));
}

function previewMotionSensitivity(level) {
    setSensitivityUi(sensitivityFromSlider(level));
}
async function saveMotionSettings(activeSeconds, idleSeconds, sensitivityLevel) {
    if (!csrf) {
        alert('Session de sécurité indisponible. Rechargez la page.');
        return false;
    }
    try {
        const current = await api(API_INTERVAL);
        const active = ACTIVE_INTERVALS.includes(Number(activeSeconds)) ? Number(activeSeconds) : Number(current.active_interval_seconds || 10);
        const idle = IDLE_INTERVALS.includes(Number(idleSeconds)) ? Number(idleSeconds) : Number(current.idle_interval_seconds || 60);
        const sensitivity = GYRO_SENSITIVITY_LEVELS.includes(Number(sensitivityLevel)) ? Number(sensitivityLevel) : Number(current.sensitivity_level || 3);
        const data = await api(API_INTERVAL, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-CSRF-Token': csrf
            },
            body: JSON.stringify({
                active_interval_seconds: active,
                idle_interval_seconds: idle,
                sensitivity_level: sensitivity
            })
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

function prototypeNotice() {
    alert('Cette fonction n’est pas active dans le firmware TRAK 3.0 prototype.');
}

function advanceSentinel() {
    prototypeNotice();
}

function openWifiModal() {
    prototypeNotice();
}

function closeWifiModal() {
    $('wifiModal')?.classList.remove('active', 'open');
}

function wifiModalBackdrop(event) {
    if (event.target === $('wifiModal')) closeWifiModal();
}

function toggleWifiPassword() {
    const input = $('wifiPassword');
    if (input) input.type = input.type === 'password' ? 'text' : 'password';
}

function saveWifiProfile() {
    prototypeNotice();
}

function clearWifiForm() {
    ['wifiSlot', 'wifiSsid', 'wifiPassword'].forEach(id => {
        const el = $(id);
        if (el) el.value = '';
    });
}

function saveSentinelTrakPhone() {
    prototypeNotice();
}

function deleteSentinelTrakPhone() {
    prototypeNotice();
}

function saveDataPlan() {
    prototypeNotice();
}

document.addEventListener('DOMContentLoaded', async () => {
    initTrackerMap();
    updateFollowButton();
    try {
        await loadSession();
        await refresh();
        await loadTrackserver();
        await loadRecordInterval();
    } catch (error) {
        if (error.message !== 'unauthorized') console.warn('[TRAK] Session:', error);
    }
    window.setInterval(refresh, REFRESH_INTERVAL_MS);
    window.setInterval(updateMotionCountdown, 250);
});