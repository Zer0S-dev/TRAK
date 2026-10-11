<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
$pdo = db();

$boxes = $pdo->query(
    'SELECT tb.trak_id, tb.phone, u.username,
            th.volt, th.amp, th.health_timestamp, th.received_at
     FROM trak_boxes tb
     LEFT JOIN users u ON u.id = tb.user_id
     LEFT JOIN trak_health th ON th.trak_box_id = tb.id
     ORDER BY tb.trak_id COLLATE NOCASE'
)->fetchAll();

page_header('Santé', $user);
?>
<div class="section-heading">
  <div>
    <span class="section-kicker">SURVEILLANCE</span>
    <h2>Santé</h2>
    <p class="muted">Dernières mesures électriques reçues pour chaque TRAK enregistré.</p>
  </div>
</div>

<div class="card">
  <div class="table-wrap">
    <table class="data-table">
      <thead>
        <tr>
          <th>TRAK ID</th>
          <th>Utilisateur</th>
          <th>Volt (V)</th>
          <th>Amp (A)</th>
          <th>Dernière réception</th>
        </tr>
      </thead>
      <tbody>
      <?php if (!$boxes): ?>
        <tr><td colspan="5">Aucun TRAK enregistré.</td></tr>
      <?php else: ?>
        <?php foreach ($boxes as $box): ?>
          <tr>
            <td><strong><?=htmlspecialchars((string)$box['trak_id'])?></strong></td>
            <td><?=htmlspecialchars((string)($box['username'] ?? '—'))?></td>
            <td><?= $box['volt'] !== null ? number_format((float)$box['volt'], 3, ',', ' ') . ' V' : '—' ?></td>
            <td><?= $box['amp'] !== null ? number_format((float)$box['amp'], 4, ',', ' ') . ' A' : '—' ?></td>
            <td><?= $box['received_at'] !== null ? htmlspecialchars((string)$box['received_at']) : 'Aucune mesure' ?></td>
          </tr>
        <?php endforeach; ?>
      <?php endif; ?>
      </tbody>
    </table>
  </div>
</div>
<?php page_footer(); ?>
