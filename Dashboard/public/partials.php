<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

function page_header(string $title, array $user): void { ?>
<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title><?=htmlspecialchars($title)?> — TRAK Connect</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.7.2/css/all.min.css">
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<div class="layout">
<aside class="sidebar">
  <div class="brand"><img src="img/logo_dark_web.png"></div>
  <nav>
    <?php $activePage = basename($_SERVER['PHP_SELF']); ?>
    <a class="<?= $activePage === 'home.php' ? 'active' : '' ?>" href="home.php"><i class="fa-solid fa-location-dot"></i><span>Map</span></a>
     <a class="<?= $activePage === 'trak_boxes.php' ? 'active' : '' ?>" href="trak_boxes.php"><i class="fa-solid fa-wifi"></i><span>TRAK Box</span></a>
    <a class="<?= $activePage === 'account.php' ? 'active' : '' ?>" href="account.php"><i class="fa-solid fa-user"></i><span>Mon Compte</span></a>
    <?php if ($user['role']==='admin'): ?>
      <a class="<?= $activePage === 'users.php' ? 'active' : '' ?>" href="users.php"><i class="fa-solid fa-users"></i><span>Users</span></a>
    <?php endif; ?>
    <a class="<?= $activePage === 'settings.php' ? 'active' : '' ?>" href="settings.php"><i class="fa-solid fa-gear"></i><span>Parametres</span></a>
  </nav>
  <div class="side-bottom">
    <div class="user-pill">
      <a href="account.php">
      <i class="fa-solid fa-circle-user"></i><?=htmlspecialchars($user['username'])?><small><?=htmlspecialchars($user['role'])?></small>
    </a>
    </div>
    <a href="logout.php" class="logout"><i class="fa-solid fa-right-from-bracket"></i> Logout</a>
  </div>
</aside>
<main class="content">
<section class="page">
<?php }

function page_footer(): void { ?>
</section>
</main>
</div>

<nav class="mobile-tabbar" aria-label="Navigation">
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'home.php' ? 'active' : '' ?>" href="home.php">
    <i class="fa-solid fa-location-dot tab-icon"></i><span>Map</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'trak_boxes.php' ? 'active' : '' ?>" href="trak_boxes.php">
    <i class="fa-solid fa-wifi  tab-icon"></i><span>TRAK box</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'settings.php' ? 'active' : '' ?>" href="settings.php">
    <i class="fa-solid fa-gear tab-icon"></i><span>Parametres</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'account.php' ? 'active' : '' ?>" href="account.php">
    <i class="fa-solid fa-user tab-icon"></i><span>Mon Compte</span>
  </a>
</nav>
</body>
</html>
<?php }
