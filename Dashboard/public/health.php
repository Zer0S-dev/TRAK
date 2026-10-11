<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();
$localZone = new DateTimeZone('Europe/Paris');
$utcZone = new DateTimeZone('UTC');

// Flux JSON du graphique live.
if (($_GET['live'] ?? '') === '1') {
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');
    $boxId = filter_input(INPUT_GET, 'trak_box_id', FILTER_VALIDATE_INT);
    if (!$boxId || $boxId < 1) { http_response_code(400); echo json_encode(['ok' => false, 'error' => 'invalid_trak_box_id']); exit; }
    $boxStmt = $pdo->prepare('SELECT tb.id, tb.trak_id, th.volt, th.amp, th.received_at,
        td.gnss_fix, td.fix_mode, td.satellites_total, td.satellites_used,
        td.gps_satellites, td.glonass_satellites, td.galileo_satellites, td.beidou_satellites,
        td.hdop, td.pdop, td.vdop, td.gnss_timestamp, td.received_at AS diagnostic_received_at
        FROM trak_boxes tb
        LEFT JOIN trak_health th ON th.trak_box_id = tb.id
        LEFT JOIN (
            SELECT h.*
            FROM trak_diagnostics_history h
            INNER JOIN (
                SELECT trak_box_id, MAX(id) AS id
                FROM trak_diagnostics_history
                GROUP BY trak_box_id
            ) latest ON latest.id = h.id
        ) td ON td.trak_box_id = tb.id
        WHERE tb.id = ? LIMIT 1');
    $boxStmt->execute([$boxId]);
    $liveBox = $boxStmt->fetch();
    if (!$liveBox) { http_response_code(404); echo json_encode(['ok' => false, 'error' => 'trak_not_found']); exit; }
    $liveStmt = $pdo->prepare("SELECT volt, amp, received_at FROM health WHERE trak_box_id = ? AND received_at >= datetime('now', '-1 hour') ORDER BY received_at ASC");
    $liveStmt->execute([$boxId]);
    $liveSeries = [];
    foreach ($liveStmt->fetchAll() as $point) {
        try {
            $received = new DateTimeImmutable((string)$point['received_at'], $utcZone);
            $liveSeries[] = ['time' => $received->setTimezone($localZone)->format('H:i:s'), 'volt' => (float)$point['volt'], 'amp' => (float)$point['amp'], 'power' => (float)$point['volt'] * (float)$point['amp']];
        } catch (Throwable $e) { /* Ignore dates historiques invalides. */ }
    }
    $diagnostic = null;
    if ($liveBox['diagnostic_received_at'] !== null) {
        $diagnostic = [
            'gnss_fix' => $liveBox['gnss_fix'] !== null ? (bool)$liveBox['gnss_fix'] : null,
            'fix_mode' => $liveBox['fix_mode'] !== null ? (int)$liveBox['fix_mode'] : null,
            'satellites_total' => $liveBox['satellites_total'] !== null ? (int)$liveBox['satellites_total'] : null,
            'satellites_used' => $liveBox['satellites_used'] !== null ? (int)$liveBox['satellites_used'] : null,
            'gps_satellites' => $liveBox['gps_satellites'] !== null ? (int)$liveBox['gps_satellites'] : null,
            'glonass_satellites' => $liveBox['glonass_satellites'] !== null ? (int)$liveBox['glonass_satellites'] : null,
            'galileo_satellites' => $liveBox['galileo_satellites'] !== null ? (int)$liveBox['galileo_satellites'] : null,
            'beidou_satellites' => $liveBox['beidou_satellites'] !== null ? (int)$liveBox['beidou_satellites'] : null,
            'hdop' => $liveBox['hdop'] !== null ? (float)$liveBox['hdop'] : null,
            'pdop' => $liveBox['pdop'] !== null ? (float)$liveBox['pdop'] : null,
            'vdop' => $liveBox['vdop'] !== null ? (float)$liveBox['vdop'] : null,
            'gnss_timestamp' => $liveBox['gnss_timestamp'],
            'received_at' => $liveBox['diagnostic_received_at']
        ];
    }
    echo json_encode([
        'ok' => true,
        'trak_id' => (string)$liveBox['trak_id'],
        'volt' => $liveBox['volt'] !== null ? (float)$liveBox['volt'] : null,
        'amp' => $liveBox['amp'] !== null ? (float)$liveBox['amp'] : null,
        'received_at' => $liveBox['received_at'],
        'diagnostic' => $diagnostic,
        'series' => $liveSeries
    ], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

$boxes = $pdo->query(
    'SELECT tb.id AS trak_box_id, tb.trak_id, tb.phone, u.username,
            th.volt, th.amp, th.health_timestamp, th.received_at,
            td.gnss_fix, td.fix_mode, td.satellites_total, td.satellites_used,
            td.gps_satellites, td.glonass_satellites, td.galileo_satellites,
            td.beidou_satellites, td.hdop, td.pdop, td.vdop,
            td.gnss_timestamp, td.received_at AS diagnostic_received_at
     FROM trak_boxes tb
     LEFT JOIN users u ON u.id = tb.user_id
     LEFT JOIN trak_health th ON th.trak_box_id = tb.id
     LEFT JOIN (
            SELECT h.*
            FROM trak_diagnostics_history h
            INNER JOIN (
                SELECT trak_box_id, MAX(id) AS id
                FROM trak_diagnostics_history
                GROUP BY trak_box_id
            ) latest ON latest.id = h.id
        ) td ON td.trak_box_id = tb.id
     ORDER BY tb.trak_id COLLATE NOCASE'
)->fetchAll();

$historyByTrak = [];
$historyStmt = $pdo->prepare(
    "SELECT volt, amp, received_at
     FROM health
     WHERE trak_box_id = ? AND received_at >= datetime('now', '-1 hour')
     ORDER BY received_at ASC"
);
foreach ($boxes as $box) {
    $historyStmt->execute([(int)$box['trak_box_id']]);
    $series = [];
    foreach ($historyStmt->fetchAll() as $point) {
        try {
            $received = new DateTimeImmutable((string)$point['received_at'], $utcZone);
            $localTime = $received->setTimezone($localZone);
            $volt = (float)$point['volt'];
            $amp = (float)$point['amp'];
            $series[] = [
                'time' => $localTime->format('H:i:s'),
                'volt' => $volt,
                'amp' => $amp,
                'power' => $volt * $amp,
            ];
        } catch (Throwable $e) {
            // Ignore malformed legacy timestamps rather than breaking the page.
        }
    }
    $historyByTrak[(int)$box['trak_box_id']] = $series;
}

page_header('Santé', $user);
?>
<div class="section-heading">
  <div>
    <span class="section-kicker">SURVEILLANCE</span>
    <h2>Santé</h2>
    <p class="muted">Dernières mesures électriques et dernier diagnostic GNSS par TRAK ID. Les graphiques affichent l'historique électrique de la dernière heure.</p>
  </div>
</div>

<div class="card">
  <div class="table-wrap">
    <table class="data-table">
      <thead>
        <tr>
          <th>Voir</th>
          <th>TRAK ID</th>
          <th>Utilisateur</th>
          <th>GNSS</th>
          <th>Dernier diag</th>
        </tr>
      </thead>
      <tbody>
      <?php if (!$boxes): ?>
        <tr><td colspan="8">Aucun TRAK enregistré.</td></tr>
      <?php else: ?>
        <?php foreach ($boxes as $box): ?>
          <?php
            $receivedLocal = 'Aucune mesure';
            if ($box['received_at'] !== null) {
                try {
                    $receivedLocal = (new DateTimeImmutable((string)$box['received_at'], $utcZone))
                        ->setTimezone($localZone)->format('Y-m-d H:i:s');
                } catch (Throwable $e) {
                    $receivedLocal = (string)$box['received_at'];
                }
            }
            $seriesJson = json_encode(
                $historyByTrak[(int)$box['trak_box_id']] ?? [],
                JSON_UNESCAPED_UNICODE | JSON_HEX_TAG | JSON_HEX_APOS | JSON_HEX_QUOT | JSON_HEX_AMP
            );
            $gnssFix = (int)($box['gnss_fix'] ?? 0) === 1 || (int)($box['fix_mode'] ?? 0) >= 2;
            $gnssFixLabel = $box['diagnostic_received_at'] === null
                ? 'Aucun diagnostic'
                : ($gnssFix
                    ? ((int)($box['fix_mode'] ?? 0) === 3 ? 'FIX 3D' : 'FIX')
                    : 'Sans fix');
            $satelliteLabel = $box['diagnostic_received_at'] === null
                ? '—'
                : (string)($box['satellites_total'] ?? '—') . ' / ' . (string)($box['satellites_used'] ?? '—');
            $constellationLabel = $box['diagnostic_received_at'] === null
                ? '—'
                : (string)($box['gps_satellites'] ?? '—') . ' / ' . (string)($box['galileo_satellites'] ?? '—') . ' / ' .
                  (string)($box['glonass_satellites'] ?? '—') . ' / ' . (string)($box['beidou_satellites'] ?? '—');
            $dopLabel = $box['diagnostic_received_at'] === null
                ? '—'
                : (string)($box['hdop'] ?? '—') . ' / ' . (string)($box['pdop'] ?? '—') . ' / ' . (string)($box['vdop'] ?? '—');
            $diagnosticReceivedLocal = '—';
            if ($box['diagnostic_received_at'] !== null) {
                try {
                    $diagnosticReceivedLocal = (new DateTimeImmutable((string)$box['diagnostic_received_at'], $utcZone))
                        ->setTimezone($localZone)->format('Y-m-d H:i:s');
                } catch (Throwable $e) {
                    $diagnosticReceivedLocal = (string)$box['diagnostic_received_at'];
                }
            }
          ?>
          <tr>
            <td>
              <button type="button" class="health-view-btn"
                data-trak="<?=htmlspecialchars((string)$box['trak_id'], ENT_QUOTES, 'UTF-8')?>"
                data-trak-box-id="<?= (int)$box['trak_box_id'] ?>"
                data-series="<?=htmlspecialchars((string)$seriesJson, ENT_QUOTES, 'UTF-8')?>"><i class="fa-solid fa-eye"></i> Voir</button>
            </td>
            <td><strong><?=htmlspecialchars((string)$box['trak_id'])?></strong></td>
            <td><?=htmlspecialchars((string)($box['username'] ?? '—'))?></td>
            <td><span class="health-gnss-status <?= $box['diagnostic_received_at'] !== null && ((int)($box['gnss_fix'] ?? 0) === 1 || (int)($box['fix_mode'] ?? 0) >= 2) ? 'is-fixed' : 'is-no-fix' ?>"><?=htmlspecialchars($gnssFixLabel, ENT_QUOTES, 'UTF-8')?></span></td>
            <td><?=htmlspecialchars($diagnosticReceivedLocal, ENT_QUOTES, 'UTF-8')?></td>
          </tr>
        <?php endforeach; ?>
      <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<dialog id="healthChartDialog" class="health-dialog" aria-labelledby="healthDialogTitle">
  <div class="health-dialog-header">
    <div class="diag">
      <span class="section-kicker">HISTORIQUE · DERNIÈRE HEURE</span>
      <h2 id="healthDialogTitle">Santé du TRAK</h2>
      <p id="healthLiveStatus" class="health-live-status" role="status"><span class="health-live-dot"></span> LIVE — connexion en cours…</p>
      <section id="healthGnssDiagnostic" class="health-gnss-card" aria-label="Diagnostic GNSS">
        <div class="health-gnss-card-title">
        
        </div>
        <div class="health-gnss-grid">
          <div class="health-gnss-metric"><p id="healthGnssReceived" class="health-gnss-updated">En attente de données</p>
            <span id="healthGnssFix" class="health-gnss-status">En attente</span></div>
          <div class="health-gnss-metric"><span>Satellites visibles</span><strong id="healthGnssVisible">—</strong></div>
          <div class="health-gnss-metric"><span>GPS</span><strong id="healthGnssGps">—</strong></div>
          <div class="health-gnss-metric"><span>Galileo</span><strong id="healthGnssGalileo">—</strong></div>
          <div class="health-gnss-metric"><span>GLONASS</span><strong id="healthGnssGlonass">—</strong></div>
          <div class="health-gnss-metric"><span>BeiDou</span><strong id="healthGnssBeidou">—</strong></div>
          <div class="health-gnss-metric"><span>HDOP · précision horizontale</span><strong id="healthGnssHdop">—</strong></div>
          <div class="health-gnss-metric"><span>PDOP · précision globale</span><strong id="healthGnssPdop">—</strong></div>
          <div class="health-gnss-metric"><span>VDOP · précision verticale</span><strong id="healthGnssVdop">—</strong></div>
        </div>
      </section>
    </div>
    <button type="button" id="healthDialogClose" class="health-close" aria-label="Fermer">×</button>
  </div>
  <div id="healthNoData" class="muted health-no-data" hidden>Aucune mesure enregistrée pour ce TRAK durant la dernière heure.</div>
  <div class="health-chart-controls" aria-label="Valeurs à afficher">
  </div>
  <div class="health-chart-card">
    <div class="health-canvas health-canvas-combined"><canvas id="healthCombinedChart"></canvas></div>
  </div>
</dialog>

<link rel="stylesheet" href="assets/health.css?v=1">

<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<script src="assets/health.js?v=1"></script>

<?php page_footer(); ?>
