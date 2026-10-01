<?php
declare(strict_types=1);
require_once __DIR__ . '/partials.php';

$user = require_login();
page_header('API', $user);

$pdo = db();
$stmt = $pdo->prepare('SELECT id, trak_id, phone, api_key, osmand_url FROM trak_boxes ORDER BY trak_id COLLATE NOCASE');
$stmt->execute();
$traks = $stmt->fetchAll();

function api_value(string $value): string {
    return htmlspecialchars($value, ENT_QUOTES, 'UTF-8');
}
?>
<div class="card">
    <h2>Identifiants API</h2>
    <p class="muted">Ces identifiants constituent la base de liaison entre le dashboard, un utilisateur et ses TRAK.</p>
    <div class="table-wrap">
        <table class="data-table">
            <thead><tr><th>Identifiant</th><th>Valeur</th><th>Utilisation</th></tr></thead>
            <tbody>
                <tr><td><strong>User ID</strong></td><td><code><?= (int)$user['id'] ?></code></td><td>Identifie l'utilisateur propriétaire de la session / des données API.</td></tr>
                <tr><td><strong>Username</strong></td><td><code><?= api_value((string)$user['username']) ?></code></td><td>Identifiant humain du compte, non utilisé comme clé technique principale.</td></tr>
                <tr><td><strong>User email</strong></td><td><?= api_value((string)($user['email'] ?? '')) ?: '<span class="muted">non renseigné</span>' ?></td><td>Contact du compte.</td></tr>
                <tr><td><strong>User phone</strong></td><td><?= api_value((string)($user['phone'] ?? '')) ?: '<span class="muted">non renseigné</span>' ?></td><td>Numéro utilisé notamment pour le provisioning SMS.</td></tr>
            </tbody>
        </table>
    </div>
</div>

<div class="card api-card">
    <h2>TRAK disponibles</h2>
    <p class="muted">Chaque TRAK est adressable par son <strong>TRAK ID</strong>. La clé API authentifie le TRAK auprès du serveur.</p>
    <?php if (!$traks): ?>
        <p class="muted">Aucun TRAK Box enregistré.</p>
    <?php else: ?>
        <div class="table-wrap">
            <table class="data-table">
                <thead><tr><th>TRAK ID</th><th>Téléphone</th><th>API key</th><th>OsmAnd / Trakserver</th></tr></thead>
                <tbody>
                <?php foreach ($traks as $trak): ?>
                    <tr>
                        <td><code><?= api_value((string)$trak['trak_id']) ?></code></td>
                        <td><?= api_value((string)$trak['phone']) ?></td>
                        <td><code><?= api_value((string)$trak['api_key']) ?></code></td>
                        <td><code><?= api_value((string)$trak['osmand_url']) ?></code></td>
                    </tr>
                <?php endforeach; ?>
                </tbody>
            </table>
        </div>
    <?php endif; ?>
</div>

<div class="card api-card">
    <h2>Données servies / utilisables par l'API</h2>
    <p class="muted">Contrat de données prévu pour la communication TRAK ↔ Dashboard. Les valeurs seront alimentées au fur et à mesure par le TRAK et le dashboard.</p>
    <div class="table-wrap">
        <table class="data-table">
            <thead><tr><th>Domaine</th><th>Donnée</th><th>Type</th><th>Accès</th></tr></thead>
            <tbody>
                <tr><td>Identité</td><td>User ID</td><td>entier</td><td>lecture</td></tr>
                <tr><td>Identité</td><td>TRAK ID</td><td>texte</td><td>lecture / ciblage</td></tr>
                <tr><td>Identité</td><td>TRAK phone</td><td>texte</td><td>lecture</td></tr>
                <tr><td>Authentification</td><td>API key</td><td>secret</td><td>authentification</td></tr>
                <tr><td>Position</td><td>latitude</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Position</td><td>longitude</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Position</td><td>altitude</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Position</td><td>vitesse</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Position</td><td>cap</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>GNSS</td><td>satellites visibles / utilisés</td><td>entier</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>GNSS</td><td>GPS / GLONASS / Galileo / BeiDou</td><td>entiers</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Réseau</td><td>network</td><td>WiFi / 4G</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Réseau</td><td>SSID / opérateur</td><td>texte</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Réseau</td><td>adresse IP</td><td>texte</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>État</td><td>motion</td><td>texte</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>État</td><td>sentinel</td><td>texte / état</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Télémétrie</td><td>batterie / alimentation</td><td>nombre / état</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Télémétrie</td><td>température</td><td>nombre</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Événements</td><td>événement + horodatage</td><td>texte + date</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Commandes</td><td>commande dashboard → TRAK</td><td>texte</td><td>écriture dashboard / lecture TRAK</td></tr>
                <tr><td>Commandes</td><td>ACK / résultat commande</td><td>texte / état</td><td>lecture / écriture TRAK</td></tr>
                <tr><td>Configuration</td><td>configuration TRAK</td><td>paramètres</td><td>lecture / écriture selon droit</td></tr>
            </tbody>
        </table>
    </div>
</div>

<div class="card api-card">
    <h2>Principe d'accès</h2>
    <p><code>User ID</code> identifie le compte, <code>TRAK ID</code> identifie le boîtier et la <code>API key</code> authentifie le boîtier. Une requête API ne devra jamais permettre à un TRAK d'accéder aux données d'un autre TRAK.</p>
</div>
<?php page_footer(); ?>
