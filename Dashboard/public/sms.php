<?php
declare(strict_types=1);

require_once __DIR__ . '/../app/auth.php';

$user = require_login();
$wizardUserId = (int)($_SESSION['wizard_user_id'] ?? 0);
$trakBoxId = (int)($_SESSION['wizard_trak_box_id'] ?? 0);

if ($wizardUserId <= 0 || $wizardUserId !== (int)$user['id'] || $trakBoxId <= 0) {
    header('Location: home.php');
    exit;
}

$pdo = db();

$stmt = $pdo->prepare('
    SELECT tb.id, tb.trak_id, tb.phone, tb.api_key, tb.trakserver_url, tb.dashboard_url,
           u.phone AS user_phone
    FROM trak_boxes tb
    LEFT JOIN users u ON u.id = tb.user_id
    WHERE tb.id = ? AND tb.user_id = ?
');
$stmt->execute([$trakBoxId, $wizardUserId]);
$trak = $stmt->fetch();

if (!$trak) {
    unset($_SESSION['wizard_trak_box_id']);
    header('Location: home.php');
    exit;
}

$userPhone = trim((string)($trak['user_phone'] ?? ''));

if ($userPhone === '') {
    $error = 'Le numéro de téléphone utilisateur est obligatoire pour envoyer la configuration SMS.';
} else {
    $error = '';
}

if ($_SERVER['REQUEST_METHOD'] === 'POST' && ($_POST['action'] ?? '') === 'finish') {
    verify_csrf();
    unset($_SESSION['wizard_user_id'], $_SESSION['wizard_trak_box_id'], $_SESSION['wizard_sms_config_id'], $_SESSION['wizard_sms_nonce']);
    header('Location: home.php?wizard=complete');
    exit;
}

if (!isset($_SESSION['wizard_sms_config_id'])) {
    $_SESSION['wizard_sms_config_id'] = 'CFG' . strtoupper(bin2hex(random_bytes(5)));
}
if (!isset($_SESSION['wizard_sms_nonce'])) {
    $_SESSION['wizard_sms_nonce'] = strtoupper(bin2hex(random_bytes(8)));
}

$configId = (string)$_SESSION['wizard_sms_config_id'];
$nonce = (string)$_SESSION['wizard_sms_nonce'];

function sms_parts(string $value): array {
    if (strlen($value) <= 120) return [$value];
    return [substr($value, 0, 120), substr($value, 120, 120)];
}

function sms_escape(string $value): string {
    return str_replace(["\r", "\n", "|"], ['', '', ''], $value);
}

$trakId = sms_escape((string)$trak['trak_id']);
$trakPhone = sms_escape((string)$trak['phone']);
$phone = sms_escape($userPhone);
$apiKey = sms_escape((string)$trak['api_key']);
$trackserver = sms_escape((string)$trak['trakserver_url']);
$dashboard = sms_escape((string)$trak['dashboard_url']);

$trackserverParts = sms_parts($trackserver);
$dashboardParts = sms_parts($dashboard);

$smsMessages = [];

// 1 — identité et numéros
$smsMessages[] = [
    'title' => 'SMS 1 — Identité',
    'text' => 'TRAKCFG1|1|' . $configId . '|' . $trakId . '|' . $trakPhone . '|' . $phone,
];

// 2 — URL Trackserver
foreach ($trackserverParts as $i => $part) {
    $partNo = $i + 1;
    $final = $partNo === count($trackserverParts) ? '1' : '0';
    $smsMessages[] = [
        'title' => 'SMS ' . (count($smsMessages) + 1) . ' — Trackserver' . (count($trackserverParts) > 1 ? ' (' . $partNo . '/' . count($trackserverParts) . ')' : ''),
        'text' => 'TRAKCFG2|' . $partNo . '|' . $configId . '|' . $part . '|' . $final,
    ];
}

// 3 — clé API + nonce
$smsMessages[] = [
    'title' => 'SMS ' . (count($smsMessages) + 1) . ' — Clé API',
    'text' => 'TRAKCFG3|1|' . $configId . '|' . $apiKey . '|' . $nonce,
];

// 4 — URL Dashboard
foreach ($dashboardParts as $i => $part) {
    $partNo = $i + 1;
    $final = $partNo === count($dashboardParts) ? '1' : '0';
    $smsMessages[] = [
        'title' => 'SMS ' . (count($smsMessages) + 1) . ' — Dashboard' . (count($dashboardParts) > 1 ? ' (' . $partNo . '/' . count($dashboardParts) . ')' : ''),
        'text' => 'TRAKCFG4|' . $partNo . '|' . $configId . '|' . $part . '|' . $final,
    ];
}
?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wizard — SMS — TRAK Connect</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.7.2/css/all.min.css">
<link rel="stylesheet" href="assets/style.css">
</head>
<body class="auth-page">
<main class="auth-card" style="max-width:760px;">
    <div class="brand"><img src="img/logo_dark_web.png"></div>

    <div class="section-heading compact">
        <div>
            <h1>🧙‍♂️ WIZARD - Etape 3/3</h1>
        </div>
    </div>

    <p class="muted">
        La TRAK Box <strong><?=htmlspecialchars((string)$trak['trak_id'])?></strong> est enregistrée.
        Envoyez tous les SMS ci-dessous, dans l'ordre, au numéro :
        <strong><?=htmlspecialchars((string)$trak['phone'])?></strong>
    </p>

    <?php if ($error): ?>
        <div class="alert error"><?=htmlspecialchars($error)?></div>
    <?php endif; ?>
    <?php foreach ($smsMessages as $index => $sms): ?>
        <div class="card" style="margin-top:16px;">
            <div class="section-heading compact">
                <div>
                    <span class="section-kicker"><?=htmlspecialchars($sms['title'])?></span>
                    <h3>Message à envoyer</h3>
                </div>
                <button type="button" onclick="copySms(<?= $index ?>)">
                    <i class="fa-solid fa-copy"></i> Copier
                </button>
            </div>
            <textarea id="sms-<?= $index ?>" class="sms-config" rows="3" readonly><?=htmlspecialchars($sms['text'])?></textarea>
        </div>
    <?php endforeach; ?>


        <p class="muted" style="margin-top:20px;">
            Envoyez tous les SMS <strong>dans l'ordre</strong>. Le TRAK répondra lorsque
            la configuration complète sera validée.
        </p>

        <form method="post" style="margin-top:16px;">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <input type="hidden" name="action" value="finish">
            <button type="submit">
                <i class="fa-solid fa-check"></i> Terminer
            </button>
        </form>

</main>

<script>
function copySms(index) {
    const field = document.getElementById('sms-' + index);
    field.select();
    field.setSelectionRange(0, field.value.length);

    if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(field.value).then(() => {
            alert('SMS copié.');
        }).catch(() => {
            document.execCommand('copy');
            alert('SMS copié.');
        });
    } else {
        document.execCommand('copy');
        alert('SMS copié.');
    }
}
</script>
</body>
</html>
