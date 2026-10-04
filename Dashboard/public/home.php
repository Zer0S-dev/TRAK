<?php require_once __DIR__.'/partials.php'; $user=require_login(); page_header('Home',$user); ?>

<div class="home-map-page" id="homeMapPage">
  <div class="home-map-toolbar" id="homeMapToolbar">
    <div class="home-map-actions">
      <button type="button" class="map-control" id="mapCenterBtn" aria-pressed="false">
        <i class="fa-solid fa-crosshairs"></i>
        <span>Centrer</span>
      </button>
      <button type="button" class="map-control" id="mapFullscreenBtn" aria-pressed="false">
        <i class="fa-solid fa-map"></i>
        <span>Map</span>
      </button>
    </div>
  </div>
<div id="trak-datas">lat: - / long: - /alt: - </div>
  <div class="home-map" id="homeMap" aria-label="Carte des TRAK"></div>

  <div class="home-map-empty" id="homeMapEmpty">
    <i class="fa-solid fa-location-dot"></i>
    <span>Aucune position TRAK disponible</span>
  </div>
</div>

<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
<script src="assets/home.js"></script>

<?php page_footer(); ?>