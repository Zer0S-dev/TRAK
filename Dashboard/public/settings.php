<?php
declare(strict_types=1);

require_once __DIR__ . '/../app/auth.php';
require_once __DIR__ . '/../app/mail.php';
require_once __DIR__ . '/partials.php';

$user = require_admin();
$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST' && ($_POST['action'] ?? '') === 'reset') {
    verify_csrf();

    try {
        $pdo = db();

        // Prévenir les utilisateurs avant la suppression de leurs comptes.
        $usersToNotify = $pdo->query("SELECT username, email FROM users WHERE email IS NOT NULL AND email != ''")->fetchAll();
        foreach ($usersToNotify as $userToNotify) {
            send_user_deleted_email((string)$userToNotify['email'], (string)$userToNotify['username']);
        }

        $pdo->beginTransaction();

        $pdo->exec('DELETE FROM trak_positions');
        $pdo->exec('DELETE FROM password_resets');
        $pdo->exec('DELETE FROM trak_boxes');
        $pdo->exec('DELETE FROM users');
        $pdo->exec("DELETE FROM sqlite_sequence WHERE name IN ('users', 'password_resets', 'trak_boxes')");

        $pdo->commit();

        logout_user();
        header('Location: register.php');
        exit;
    } catch (Throwable $e) {
        if (isset($pdo) && $pdo->inTransaction()) {
            $pdo->rollBack();
        }
        $error = 'Impossible de réinitialiser le dashboard.';
    }
}

page_header('Settings', $user);
?>
<div class="card">
    <div class="section-heading">
        <div>
            <span class="section-kicker">ADMINISTRATION</span>
            <h2>Settings</h2>
        </div>
    </div>

    <?php if ($error): ?>
        <div class="alert error"><?=htmlspecialchars($error)?></div>
    <?php endif; ?>
<div class="card" style="margin-top:16px;">
    <div class="section-heading compact">
        <div>
            <span class="section-kicker">A PROPOS</span>
            <h3>TRAK — OpenSource GPS Tracker</h3>
        </div>
    </div>

    <p class="muted">
        TRAK est un projet OpenSource de localisation GPS conçu autour d'un tracker
        autonome connecté au Dashboard TRAK. Le système permet de suivre les positions,
        gérer les TRAK Box, configurer leur connexion et transmettre les données GPS
        vers un serveur Trackserver.
    </p>

    <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-top:20px;">

        <div class="card" style="margin:0;">
            <span class="section-kicker">FIRMWARE</span>
            <h4 style="margin:6px 0 0;">TRAK v3.n</h4>
            <p class="muted" style="margin:6px 0 0;">
                Version du firmware installé sur le tracker TRAK.
            </p>
        </div>

        <div class="card" style="margin:0;">
            <span class="section-kicker">DASHBOARD</span>
            <h4 style="margin:6px 0 0;">Dashboard v1.0</h4>
            <p class="muted" style="margin:6px 0 0;">
                Interface Web de gestion des utilisateurs, TRAK Box et configurations.
            </p>
        </div>

        <div class="card" style="margin:0;">
            <span class="section-kicker">LICENCE</span>
            <h4 style="margin:6px 0 0;">OpenSource</h4>
            <p class="muted" style="margin:6px 0 0;">
                Projet développé et distribué sous licence OpenSource.
            </p>
        </div>

    </div>

    <div style="margin-top:20px;">
        <span class="section-kicker">RESSOURCES</span>

        <div style="display:flex;flex-wrap:wrap;gap:10px;margin-top:10px;">

            <a
                href="https://github.com/Zer0S-dev/TRAK"
                target="_blank"
                rel="noopener noreferrer"
                class="table-action modify"
                style="text-decoration:none;"
            >
                <i class="fa-brands fa-github"></i>
                Projet TRAK sur GitHub
            </a>

            <a
                href="https://github.com/tinuzz/wp-plugin-trackserver"
                target="_blank"
                rel="noopener noreferrer"
                class="table-action modify"
                style="text-decoration:none;"
            >
                <i class="fa-brands fa-wordpress"></i>
                Trackserver pour WordPress
            </a>
        </div>
    </div>
    <p class="muted" style="margin-top:20px;margin-bottom:0;">
        Depuis l'application Android TRAK, vous pouvez effacer l'URL actuellement
        enregistrée et revenir directement à l'écran de saisie afin de configurer
        une nouvelle adresse.
    </p>
</div>
 <div class="card" style="margin-top:16px;">
        <div class="section-heading compact">
            <div>
                <span class="section-kicker">INSTALLATION</span>
                <h3>Reset du Dashboard</h3>
            </div>
        </div>

        <p class="muted">
            Le reset efface toutes les données du Dashboard : compte(s), TRAK Box,
            positions et données de configuration. Le wizard de première installation sera relancé.
        </p>

        <form method="post" onsubmit="return confirm('ATTENTION : cette action va effacer définitivement toute la base du Dashboard et relancer le wizard. Tous les comptes, TRAK Box et positions seront supprimés.\n\nVoulez-vous vraiment continuer ?');">
            <input type="hidden" name="csrf" value="<?=htmlspecialchars(csrf_token())?>">
            <input type="hidden" name="action" value="reset">
            <button type="submit" class="danger">
                <i class="fa-solid fa-rotate-right"></i> Reset
            </button>
        </form>
    </div>
    <div class="card" style="margin-top:16px;">
        <div class="section-heading compact">
            <div>
                <span class="section-kicker">SMS</span>
                <h3>Reset NVS du TRAK</h3>
            </div>
        </div>

        <p class="muted">
            Copiez ce SMS et envoyez-le au numéro du TRAK. Le TRAK demandera une confirmation
            par SMS avant d'effacer sa configuration NVS et ses profils Wi-Fi.
        </p>

        <div style="display:flex;gap:10px;align-items:stretch;flex-wrap:wrap;">
            <textarea id="resetTrakSms" class="sms-config" rows="2" readonly>RESET TRAK</textarea>
            <button type="button" onclick="copyResetTrakSms()">
                <i class="fa-solid fa-copy"></i> Copier
            </button>
        </div>

        <p class="muted" style="margin-top:10px;">
            <strong>Confirmation :</strong> après réception du SMS, répondez exactement <code>YES</code>
            dans les 2 minutes.
        </p>
    </div>

    <div class="card" style="margin-top:16px;">
        <div class="section-heading compact">
            <div>
                <span class="section-kicker"><i class="fa-brands fa-android" style="margin-right:5px;"></i>Android App</span>
                <h3>Reset URL Dashboard</h3>
            </div>
        </div>

        <p class="muted">
            Depuis l'application Android TRAK, vous pouvez effacer l'URL actuellement enregistrée
            et revenir directement à l'écran de saisie pour configurer une nouvelle adresse.
        </p>

        <button type="button" class="danger" onclick="changeDashboardUrl()">
            <i class="fa-solid fa-link-slash"></i> Changer App URL
        </button>
    </div>

   
</div>

<script>
function copyResetTrakSms() {
    const field = document.getElementById('resetTrakSms');
    field.select();
    field.setSelectionRange(0, field.value.length);
    navigator.clipboard?.writeText(field.value).then(() => {
        alert('SMS copié : RESET TRAK');
    }).catch(() => {
        document.execCommand('copy');
        alert('SMS copié : RESET TRAK');
    });
}

function changeDashboardUrl() {
    if (typeof Android === 'undefined' || typeof Android.changeDashboardUrl !== 'function') {
        alert('Cette fonction est disponible uniquement dans l’application Android TRAK.');
        return;
    }

    if (!confirm('L’URL du Dashboard enregistrée dans l’application sera supprimée. Vous serez renvoyé vers l’écran de configuration. Continuer ?')) {
        return;
    }

    Android.changeDashboardUrl();
}
</script>

<?php page_footer(); ?>