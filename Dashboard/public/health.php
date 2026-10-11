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
    $boxStmt = $pdo->prepare('SELECT tb.id, tb.trak_id, th.volt, th.amp, th.received_at FROM trak_boxes tb LEFT JOIN trak_health th ON th.trak_box_id = tb.id WHERE tb.id = ? LIMIT 1');
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
    echo json_encode(['ok' => true, 'trak_id' => (string)$liveBox['trak_id'], 'volt' => $liveBox['volt'] !== null ? (float)$liveBox['volt'] : null, 'amp' => $liveBox['amp'] !== null ? (float)$liveBox['amp'] : null, 'received_at' => $liveBox['received_at'], 'series' => $liveSeries], JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    exit;
}

$boxes = $pdo->query(
    'SELECT tb.id AS trak_box_id, tb.trak_id, tb.phone, u.username,
            th.volt, th.amp, th.health_timestamp, th.received_at
     FROM trak_boxes tb
     LEFT JOIN users u ON u.id = tb.user_id
     LEFT JOIN trak_health th ON th.trak_box_id = tb.id
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
    <p class="muted">Dernières mesures électriques. Les graphiques affichent l'historique de la dernière heure.</p>
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
          <th>Volt (V)</th>
          <th>Amp (A)</th>
          <th>Dernière réception (heure locale)</th>
        </tr>
      </thead>
      <tbody>
      <?php if (!$boxes): ?>
        <tr><td colspan="6">Aucun TRAK enregistré.</td></tr>
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
          ?>
          <tr>
            <td>
              <button type="button" class="health-view-btn"
                data-trak="<?=htmlspecialchars((string)$box['trak_id'], ENT_QUOTES, 'UTF-8')?>"
                data-trak-box-id="<?= (int)$box['trak_box_id'] ?>"
                data-series="<?=htmlspecialchars((string)$seriesJson, ENT_QUOTES, 'UTF-8')?>">Voir</button>
            </td>
            <td><strong><?=htmlspecialchars((string)$box['trak_id'])?></strong></td>
            <td><?=htmlspecialchars((string)($box['username'] ?? '—'))?></td>
            <td><?= $box['volt'] !== null ? number_format((float)$box['volt'], 3, ',', ' ') . ' V' : '—' ?></td>
            <td><?= $box['amp'] !== null ? number_format((float)$box['amp'], 4, ',', ' ') . ' A' : '—' ?></td>
            <td><?=htmlspecialchars($receivedLocal, ENT_QUOTES, 'UTF-8')?></td>
          </tr>
        <?php endforeach; ?>
      <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>

<dialog id="healthChartDialog" class="health-dialog" aria-labelledby="healthDialogTitle">
  <div class="health-dialog-header">
    <div>
      <span class="section-kicker">HISTORIQUE · DERNIÈRE HEURE</span>
      <h2 id="healthDialogTitle">Santé du TRAK</h2>
      <p class="muted">Heures locales — puissance calculée : P = U × I.</p>
      <p id="healthLiveStatus" class="health-live-status" role="status"><span class="health-live-dot"></span> LIVE — connexion en cours…</p>
    </div>
    <button type="button" id="healthDialogClose" class="health-close" aria-label="Fermer">×</button>
  </div>
  <div id="healthNoData" class="muted health-no-data" hidden>Aucune mesure enregistrée pour ce TRAK durant la dernière heure.</div>
  <div class="health-chart-controls" aria-label="Valeurs à afficher">
    <label><input type="checkbox" class="health-series-toggle" data-series="0" checked> Tension U (V)</label>
    <label><input type="checkbox" class="health-series-toggle" data-series="1" checked> Courant I (A)</label>
    <label><input type="checkbox" class="health-series-toggle" data-series="2" checked> Puissance P (W)</label>
  </div>
  <div class="health-chart-card">
    <div class="health-canvas health-canvas-combined"><canvas id="healthCombinedChart"></canvas></div>
  </div>
</dialog>

<style>
.health-view-btn{border:0;border-radius:7px;padding:7px 13px;background:var(--accent,#6d35ff);color:#fff;font:inherit;font-weight:600;cursor:pointer}
.health-view-btn:hover{filter:brightness(.94)}
.health-dialog{width:min(1000px,94vw);max-width:1000px;max-height:90vh;overflow:auto;border:0;border-radius:16px;padding:22px;background:var(--bg,#fff);color:var(--text,#14161d);box-shadow:0 20px 70px #0004}
.health-dialog::backdrop{background:#10131acc;backdrop-filter:blur(3px)}
.health-dialog-header{display:flex;justify-content:space-between;gap:20px;align-items:flex-start;margin-bottom:18px}
.health-dialog-header h2{margin:5px 0}
.health-close{border:0;background:transparent;color:inherit;font-size:30px;line-height:1;cursor:pointer;padding:0 5px}
.health-chart-card{border:1px solid #8883;border-radius:12px;padding:14px;min-width:0}
.health-chart-card h3{margin:0 0 10px;font-size:1rem}
.health-canvas{height:360px;position:relative}
.health-chart-controls{display:flex;flex-wrap:wrap;gap:12px 20px;margin:16px 0;align-items:center}
.health-chart-controls label{display:inline-flex;align-items:center;gap:7px;cursor:pointer;font-size:.95rem}
.health-chart-controls input{accent-color:var(--accent,#6d35ff);width:16px;height:16px}
.health-no-data{padding:12px 0}
.health-live-status{display:flex;align-items:center;gap:7px;margin:8px 0 0;font-size:.85rem;font-weight:600;color:#16803d}
.health-live-dot{width:8px;height:8px;border-radius:50%;background:#20c55e;box-shadow:0 0 0 3px #20c55e22;display:inline-block}
.health-live-status.is-stale{color:#b45309}
.health-live-status.is-stale .health-live-dot{background:#f59e0b;box-shadow:0 0 0 3px #f59e0b22}
@media(max-width:600px){.health-dialog{padding:15px}.health-canvas{height:300px}}
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<script>
(() => {
  const dialog = document.getElementById('healthChartDialog');
  const closeButton = document.getElementById('healthDialogClose');
  const title = document.getElementById('healthDialogTitle');
  const noData = document.getElementById('healthNoData');
  const chartArea = document.querySelector('.health-chart-card');
  const toggles = Array.from(document.querySelectorAll('.health-series-toggle'));
  const liveStatus = document.getElementById('healthLiveStatus');
  let chart = null;
  let liveTimer = null;
  let activeBoxId = null;
  const LIVE_REFRESH_MS = 5000;

  function stopLive() {
    if (liveTimer !== null) {
      window.clearInterval(liveTimer);
      liveTimer = null;
    }
    activeBoxId = null;
  }

  function setLiveStatus(message, stale = false) {
    liveStatus.classList.toggle('is-stale', stale);
    liveStatus.innerHTML = '<span class="health-live-dot"></span>' + message;
  }

  function destroyChart() {
    if (chart) {
      chart.destroy();
      chart = null;
    }
  }

  function renderChart(series) {
    destroyChart();
    const canvas = document.getElementById('healthCombinedChart');

    // Axes adapt to the actual measurements, with a little breathing room.
    // Voltage is kept in a narrow, readable range; current and power start at zero
    // only when that does not flatten the useful variations.
    function paddedRange(values, minimumSpan, paddingRatio = 0.18, floor = null) {
      const valid = values.filter(Number.isFinite);
      if (!valid.length) return { min: 0, max: minimumSpan };
      let low = Math.min(...valid);
      let high = Math.max(...valid);
      if (high - low < minimumSpan) {
        const middle = (high + low) / 2;
        low = middle - minimumSpan / 2;
        high = middle + minimumSpan / 2;
      }
      const pad = (high - low) * paddingRatio;
      low -= pad;
      high += pad;
      if (floor !== null) low = Math.max(floor, low);
      return { min: low, max: high };
    }

    const voltageRange = paddedRange(series.map(p => p.volt), 0.20, 0.10);
    const currentRange = paddedRange(series.map(p => p.amp), 0.10, 0.10, 0);
    const powerRange = paddedRange(series.map(p => p.power), 0.50, 0.10, 0);
    chart = new Chart(canvas, {
      type: 'line',
      data: {
        labels: series.map(point => point.time),
        datasets: [
          {
            label: 'Tension U (V)',
            data: series.map(point => point.volt),
            borderColor: '#6d35ff',
            backgroundColor: 'rgba(109,53,255,.08)',
            yAxisID: 'yVoltage',
            hidden: !toggles[0].checked,
            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .38,
            spanGaps: false
          },
          {
            label: 'Courant I (A)',
            data: series.map(point => point.amp),
            borderColor: '#0891b2',
            backgroundColor: 'rgba(8,145,178,.08)',
            yAxisID: 'yCurrent',
            hidden: !toggles[1].checked,
            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .32,
            spanGaps: false
          },
          {
            label: 'Puissance P (W)',
            data: series.map(point => point.power),
            borderColor: '#e58b19',
            backgroundColor: 'rgba(229,139,25,.08)',
            yAxisID: 'yPower',
            hidden: !toggles[2].checked,
            pointRadius: 0,
            pointHoverRadius: 0,
            pointHitRadius: 8,
            borderWidth: 2,
            tension: .18,
            spanGaps: false
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: false,
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { display: true, position: 'top' },
          tooltip: { mode: 'index', intersect: false }
        },
        scales: {
          x: { title: { display: true, text: 'Heure locale' }, ticks: { maxTicksLimit: 10, maxRotation: 0 } },
          yVoltage: {
            type: 'linear', position: 'left',
            min: voltageRange.min, max: voltageRange.max,
            title: { display: true, text: 'U (V)' },
            ticks: { maxTicksLimit: 7, precision: 2 },
            grid: { drawOnChartArea: true }
          },
          yCurrent: {
            type: 'linear', position: 'right',
            min: currentRange.min, max: currentRange.max,
            title: { display: true, text: 'I (A)' },
            ticks: { maxTicksLimit: 7, precision: 2 },
            grid: { drawOnChartArea: false }
          },
          yPower: {
            type: 'linear', position: 'right',
            min: powerRange.min, max: powerRange.max,
            title: { display: true, text: 'P (W)' },
            ticks: { maxTicksLimit: 7, precision: 2 },
            grid: { drawOnChartArea: false },
            offset: true,
            display: true
          }
        }
      }
    });
    syncAxisVisibility();
  }

  function syncAxisVisibility() {
    if (!chart) return;
    const visibleByAxis = { yVoltage: false, yCurrent: false, yPower: false };
    chart.data.datasets.forEach((dataset, index) => {
      if (dataset.yAxisID && Object.prototype.hasOwnProperty.call(visibleByAxis, dataset.yAxisID)) {
        visibleByAxis[dataset.yAxisID] = chart.isDatasetVisible(index);
      }
    });
    Object.entries(visibleByAxis).forEach(([axisId, visible]) => {
      chart.options.scales[axisId].display = visible;
    });
    chart.update('none');
  }

  toggles.forEach((toggle, index) => {
    toggle.addEventListener('change', () => {
      if (!chart) return;
      chart.setDatasetVisibility(index, toggle.checked);
      syncAxisVisibility();
    });
  });

  function paddedRangeForLive(values, minimumSpan, paddingRatio = 0.10, floor = null) {
    const valid = values.filter(Number.isFinite);
    if (!valid.length) return { min: 0, max: minimumSpan };
    let low = Math.min(...valid);
    let high = Math.max(...valid);
    if (high - low < minimumSpan) {
      const middle = (high + low) / 2;
      low = middle - minimumSpan / 2;
      high = middle + minimumSpan / 2;
    }
    const pad = (high - low) * paddingRatio;
    low -= pad;
    high += pad;
    if (floor !== null) low = Math.max(floor, low);
    return { min: low, max: high };
  }

  async function refreshLive() {
    if (!activeBoxId) return;
    try {
      const response = await fetch('health.php?live=1&trak_box_id=' + encodeURIComponent(activeBoxId), {
        cache: 'no-store',
        headers: { 'Accept': 'application/json' }
      });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const payload = await response.json();
      if (!payload.ok) throw new Error(payload.error || 'Réponse invalide');
      const series = Array.isArray(payload.series) ? payload.series : [];
      if (series.length) {
        if (!chart && typeof Chart !== 'undefined') renderChart(series);
        if (chart) {
          chart.data.labels = series.map(point => point.time);
          chart.data.datasets[0].data = series.map(point => point.volt);
          chart.data.datasets[1].data = series.map(point => point.amp);
          chart.data.datasets[2].data = series.map(point => point.power);
          const ranges = [
            paddedRangeForLive(series.map(point => point.volt), 0.20, 0.10),
            paddedRangeForLive(series.map(point => point.amp), 0.10, 0.10, 0),
            paddedRangeForLive(series.map(point => point.power), 0.50, 0.10, 0)
          ];
          chart.options.scales.yVoltage.min = ranges[0].min;
          chart.options.scales.yVoltage.max = ranges[0].max;
          chart.options.scales.yCurrent.min = ranges[1].min;
          chart.options.scales.yCurrent.max = ranges[1].max;
          chart.options.scales.yPower.min = ranges[2].min;
          chart.options.scales.yPower.max = ranges[2].max;
          syncAxisVisibility();
          noData.hidden = true;
          chartArea.hidden = false;
          document.querySelector('.health-chart-controls').hidden = false;
        }
      } else if (!chart) {
        noData.hidden = false;
        chartArea.hidden = true;
        document.querySelector('.health-chart-controls').hidden = true;
      }
      setLiveStatus('LIVE — actualisé à ' + new Date().toLocaleTimeString('fr-FR'));
    } catch (error) {
      setLiveStatus('LIVE — connexion en attente, nouvelle tentative…', true);
    }
  }

  document.querySelectorAll('.health-view-btn').forEach(button => {
    button.addEventListener('click', () => {
      stopLive();
      activeBoxId = button.dataset.trakBoxId;
      let series = [];
      try { series = JSON.parse(button.dataset.series || '[]'); } catch (e) {}
      title.textContent = 'Santé — ' + button.dataset.trak;
      noData.hidden = series.length > 0;
      chartArea.hidden = series.length === 0;
      document.querySelector('.health-chart-controls').hidden = series.length === 0;
      if (series.length > 0 && typeof Chart !== 'undefined') renderChart(series);
      else destroyChart();
      if (typeof dialog.showModal === 'function') dialog.showModal();
      setLiveStatus('LIVE — connexion en cours…');
      refreshLive();
      liveTimer = window.setInterval(refreshLive, LIVE_REFRESH_MS);
    });
  });

  closeButton.addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => {
    if (event.target === dialog) dialog.close();
  });
  dialog.addEventListener('close', () => {
    stopLive();
    destroyChart();
  });
})();
</script>
<?php page_footer(); ?>
