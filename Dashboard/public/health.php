<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();
$localZone = new DateTimeZone('Europe/Paris');
$utcZone = new DateTimeZone('UTC');

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
    </div>
    <button type="button" id="healthDialogClose" class="health-close" aria-label="Fermer">×</button>
  </div>
  <div id="healthNoData" class="muted health-no-data" hidden>Aucune mesure enregistrée pour ce TRAK durant la dernière heure.</div>
  <div class="health-charts">
    <section class="health-chart-card"><h3>Tension U (V)</h3><div class="health-canvas"><canvas id="healthVoltageChart"></canvas></div></section>
    <section class="health-chart-card"><h3>Courant I (A)</h3><div class="health-canvas"><canvas id="healthCurrentChart"></canvas></div></section>
    <section class="health-chart-card"><h3>Puissance P (W)</h3><div class="health-canvas"><canvas id="healthPowerChart"></canvas></div></section>
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
.health-charts{display:grid;grid-template-columns:1fr;gap:14px}
.health-chart-card{border:1px solid #8883;border-radius:12px;padding:14px;min-width:0}
.health-chart-card h3{margin:0 0 10px;font-size:1rem}
.health-canvas{height:190px;position:relative}
.health-no-data{padding:12px 0}
@media(min-width:760px){.health-charts{grid-template-columns:1fr 1fr}.health-chart-card:last-child{grid-column:1/-1}.health-chart-card:last-child .health-canvas{height:220px}}
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<script>
(() => {
  const dialog = document.getElementById('healthChartDialog');
  const closeButton = document.getElementById('healthDialogClose');
  const title = document.getElementById('healthDialogTitle');
  const noData = document.getElementById('healthNoData');
  const chartSpecs = [
    { id: 'healthVoltageChart', label: 'Tension (V)', field: 'volt' },
    { id: 'healthCurrentChart', label: 'Courant (A)', field: 'amp' },
    { id: 'healthPowerChart', label: 'Puissance (W)', field: 'power' }
  ];
  let charts = [];

  function destroyCharts() {
    charts.forEach(chart => chart.destroy());
    charts = [];
  }

  function renderCharts(series) {
    destroyCharts();
    const labels = series.map(point => point.time);
    chartSpecs.forEach(spec => {
      const canvas = document.getElementById(spec.id);
      const values = series.map(point => point[spec.field]);
      charts.push(new Chart(canvas, {
        type: 'line',
        data: {
          labels,
          datasets: [{
            label: spec.label,
            data: values,
            borderColor: '#6d35ff',
            backgroundColor: 'rgba(109,53,255,.12)',
            borderWidth: 2,
            pointRadius: series.length > 90 ? 0 : 2,
            pointHoverRadius: 4,
            tension: .18,
            fill: true,
            spanGaps: false
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          animation: false,
          interaction: { mode: 'index', intersect: false },
          plugins: { legend: { display: false } },
          scales: {
            x: { ticks: { maxTicksLimit: 8, maxRotation: 0 } },
            y: { beginAtZero: false }
          }
        }
      }));
    });
  }

  document.querySelectorAll('.health-view-btn').forEach(button => {
    button.addEventListener('click', () => {
      let series = [];
      try { series = JSON.parse(button.dataset.series || '[]'); } catch (e) {}
      title.textContent = 'Santé — ' + button.dataset.trak;
      noData.hidden = series.length > 0;
      document.querySelector('.health-charts').hidden = series.length === 0;
      if (series.length > 0 && typeof Chart !== 'undefined') renderCharts(series);
      else destroyCharts();
      if (typeof dialog.showModal === 'function') dialog.showModal();
    });
  });

  closeButton.addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', event => {
    if (event.target === dialog) dialog.close();
  });
  dialog.addEventListener('close', destroyCharts);
})();
</script>
<?php page_footer(); ?>
