/* TRAK 3.0.5 — dashboard data/status module */

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
    setText('lastUpdate', lastUpdate && !Number.isNaN(lastUpdate.getTime()) ? '' + lastUpdate.toLocaleString('fr-FR', { dateStyle: 'short', timeStyle: 'medium' }) : '');
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
