/* TRAK 3.0.5 — map module */

function initTrackerMap() {
    if (trackerMap || typeof L === 'undefined') return;
    const mapElement = $('map');
    if (!mapElement) return;
    trackerMap = L.map(mapElement, { zoomControl: false, minZoom: 2, maxZoom: 19 }).setView([46.6, 1.89], 6);
    L.control.zoom({ position: 'bottomright' }).addTo(trackerMap);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        minZoom: 2, maxZoom: 19, maxNativeZoom: 19, tileSize: 256,
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
            icon: L.divIcon({ className: 'trak-marker', iconSize: [18, 18], iconAnchor: [9, 9] })
        }).addTo(trackerMap);
        trackerMap.setView([lat, lon], 15);
        return;
    }
    trackerMarker.setLatLng([lat, lon]);
    if (mapFollow && changed) trackerMap.panTo([lat, lon], { animate: true, duration: 0.35 });
}

function toggleFollow() {
    mapFollow = !mapFollow;
    updateFollowButton();
    if (mapFollow && lastTrackerPosition && trackerMap) {
        trackerMap.setView(lastTrackerPosition, Math.max(trackerMap.getZoom(), 15), { animate: true });
    }
}

function updateFollowButton() {
    $('followBtn')?.classList.toggle('active', mapFollow);
}

function toggleFullMap() {
    const full = document.body.classList.toggle('full-map');
    $('fullMapBtn')?.classList.toggle('active', full);
    window.setTimeout(() => trackerMap?.invalidateSize(), 100);
}
