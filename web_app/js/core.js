/* TRAK 3.0.5 — dashboard core */

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

async function api(url, options = {}) {
    const response = await fetch(url, { cache: 'no-store', ...options });
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
    if (!data || data.ok !== true || typeof data.csrf !== 'string' || data.csrf.length < 32) {
        throw new Error('Session API invalide');
    }
    csrf = data.csrf;
}
