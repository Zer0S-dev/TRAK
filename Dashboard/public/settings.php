<?php
declare(strict_types=1);

require_once __DIR__ . '/../app/auth.php';
require_once __DIR__ . '/partials.php';

$user = require_admin();
$error = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST' && ($_POST['action'] ?? '') === 'reset') {
    verify_csrf();

    try {
        $pdo = db();
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
                <span class="section-kicker">INSTALLATION</span>
                <h3>Réinitialiser le Dashboard</h3>
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
</div>
<?php page_footer(); ?>