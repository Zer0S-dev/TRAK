(() => {
  const page = document.getElementById('homeMapPage');
  const mapElement = document.getElementById('homeMap');
  const centerButton = document.getElementById('mapCenterBtn');
  const fullscreenButton = document.getElementById('mapFullscreenBtn');
  const emptyState = document.getElementById('homeMapEmpty');
  const trakDatas = document.getElementById('trak-datas');

  if (!page || !mapElement || typeof L === 'undefined') return;

  const defaultCenter = [46.603354, 1.888334];
  const map = L.map(mapElement, {
    zoomControl: true,
    attributionControl: true
  }).setView(defaultCenter, 8);

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);

  let autoCenter = false;
  let mapOnly = false;
  let trakPosition = null;
  let latestReceivedAt = null;
  const trakMarkers = new Map();
  let pollTimer = null;

  function updateCenterButton() {
    centerButton.classList.toggle('active', autoCenter);
    centerButton.setAttribute('aria-pressed', autoCenter ? 'true' : 'false');
  }

  function updateMapOnlyButton() {
    fullscreenButton.classList.toggle('active', mapOnly);
    fullscreenButton.setAttribute('aria-pressed', mapOnly ? 'true' : 'false');
    page.classList.toggle('map-only', mapOnly);
    document.body.classList.toggle('home-map-only', mapOnly);
    setTimeout(() => map.invalidateSize(), 50);
  }

  function updateTrakDatas(latitude, longitude, altitude) {
    if (!trakDatas) return;
    const lat = Number(latitude);
    const lon = Number(longitude);
    const alt = altitude === null || altitude === undefined || altitude === '' ? null : Number(altitude);
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;
    trakDatas.textContent = 'lat: ' + lat.toFixed(6) + ' / long: ' + lon.toFixed(6) + ' /alt: ' + (Number.isFinite(alt) ? alt.toFixed(1) + ' m' : '-');
  }

  function updateMarker(trakId, latitude, longitude, receivedAt, altitude) {
    const lat = Number(latitude);
    const lon = Number(longitude);
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;

    const position = [lat, lon];
    trakPosition = position;
    emptyState.hidden = true;

    let marker = trakMarkers.get(trakId);
    if (!marker) {
      marker = L.marker(position).addTo(map);
      trakMarkers.set(trakId, marker);
    } else {
      marker.setLatLng(position);
    }

    const receivedText = receivedAt ? 'Dernière réception : ' + receivedAt + ' UTC' : '';
    updateTrakDatas(lat, lon, altitude);
    marker.bindTooltip(String(trakId) + (receivedText ? '<br>' + receivedText : ''), {
      permanent: false,
      direction: 'top',
      offset: [0, -8]
    });

    if (autoCenter) {
      map.setView(position, Math.max(map.getZoom(), 15), { animate: true });
    }
  }

  function removeMissingMarkers(currentIds) {
    for (const [trakId, marker] of trakMarkers.entries()) {
      if (!currentIds.has(trakId)) {
        map.removeLayer(marker);
        trakMarkers.delete(trakId);
      }
    }
  }

  async function refreshPositions() {
    try {
      const response = await fetch('map_positions.php', {
        method: 'GET',
        credentials: 'same-origin',
        cache: 'no-store',
        headers: { 'Accept': 'application/json' }
      });

      if (!response.ok) return;

      const data = await response.json();
      if (!data.ok || !Array.isArray(data.positions)) return;

      const ids = new Set();

      for (const item of data.positions) {
        const trakId = String(item.trak_id || '').trim();
        if (!trakId) continue;
        ids.add(trakId);
        const receivedAt = item.received_at || '';
        if (latestReceivedAt === null || receivedAt >= latestReceivedAt) {
          latestReceivedAt = receivedAt;
          updateTrakDatas(item.latitude, item.longitude, item.altitude);
        }
        updateMarker(trakId, item.latitude, item.longitude, item.received_at, item.altitude);
      }

      removeMissingMarkers(ids);
      emptyState.hidden = trakMarkers.size > 0;
    } catch (_) {
      // Une erreur réseau ne doit pas perturber la carte.
      // Le prochain polling reprendra automatiquement.
    }
  }

  function startPositionPolling() {
    refreshPositions();
    pollTimer = window.setInterval(refreshPositions, 2000);
  }

  centerButton.addEventListener('click', () => {
    autoCenter = !autoCenter;
    updateCenterButton();

    if (autoCenter && trakPosition) {
      map.setView(trakPosition, Math.max(map.getZoom(), 15), { animate: true });
    }
  });

  fullscreenButton.addEventListener('click', () => {
    mapOnly = !mapOnly;
    updateMapOnlyButton();
  });

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && mapOnly) {
      mapOnly = false;
      updateMapOnlyButton();
    }
  });

  // Compatibilité avec une éventuelle injection directe d'une position.
  window.trakMapSetPosition = (latitude, longitude, label) => {
    updateMarker(String(label || 'TRAK'), latitude, longitude, null, null);
  };

  updateCenterButton();
  updateMapOnlyButton();
  startPositionPolling();

  window.addEventListener('beforeunload', () => {
    if (pollTimer !== null) window.clearInterval(pollTimer);
  });
})();