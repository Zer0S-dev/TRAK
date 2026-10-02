  const page = document.getElementById('homeMapPage');
  const mapElement = document.getElementById('homeMap');
  const centerButton = document.getElementById('mapCenterBtn');
  const fullscreenButton = document.getElementById('mapFullscreenBtn');
  const emptyState = document.getElementById('homeMapEmpty');

  if (!page || !mapElement || typeof L === 'undefined') return;

  const defaultCenter = [46.603354, 1.888334];
  const map = L.map(mapElement, {
    zoomControl: true,
    attributionControl: true
  }).setView(defaultCenter, 6);

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors'
  }).addTo(map);

  let autoCenter = false;
  let mapOnly = false;
  let trakMarker = null;
  let trakPosition = null;

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

  function showPosition(latitude, longitude, label) {
    const lat = Number(latitude);
    const lon = Number(longitude);
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) return;

    trakPosition = [lat, lon];
    emptyState.hidden = true;

    if (!trakMarker) {
      trakMarker = L.marker(trakPosition).addTo(map);
    } else {
      trakMarker.setLatLng(trakPosition);
    }

    if (label) {
      trakMarker.bindTooltip(String(label), {
        permanent: false,
        direction: 'top',
        offset: [0, -8]
      });
    }

    if (autoCenter) {
      map.setView(trakPosition, Math.max(map.getZoom(), 15), { animate: true });
    }
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

  // Point d'integration pour l'API de position qui sera ajoutee ensuite.
  window.trakMapSetPosition = showPosition;

  updateCenterButton();
  updateMapOnlyButton();
