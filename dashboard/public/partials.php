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
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<div class="layout">
<aside class="sidebar">
  <div class="brand"><img src="img/logo_dark_web.png"></div>
  <nav>
    <?php $activePage = basename($_SERVER['PHP_SELF']); ?>
    <a class="<?= $activePage === 'home.php' ? 'active' : '' ?>" href="home.php"><span>Home</span></a>
    <a class="<?= $activePage === 'settings.php' ? 'active' : '' ?>" href="settings.php"><span>Settings</span></a>
    <a class="<?= $activePage === 'account.php' ? 'active' : '' ?>" href="account.php"><span>Account</span></a>
    <a class="<?= $activePage === 'trak_boxes.php' ? 'active' : '' ?>" href="trak_boxes.php"><span>TRAK Box</span></a>
    <a class="<?= $activePage === 'api.php' ? 'active' : '' ?>" href="api.php"><span>API</span></a>
    <?php if ($user['role']==='admin'): ?>
      <a class="<?= $activePage === 'register.php' ? 'active' : '' ?>" href="register.php"><span>Users</span></a>
    <?php endif; ?>
  </nav>
  <div class="side-bottom">
    <div class="user-pill"><?=htmlspecialchars($user['username'])?><small><?=htmlspecialchars($user['role'])?></small></div>
    <a href="logout.php">Logout</a>
  </div>
</aside>
<main class="content">
<header class="topbar"><h1><?=htmlspecialchars($title)?></h1></header>
<section class="page">
<?php }

function page_footer(): void { ?>
</section>
</main>
</div>

<nav class="mobile-tabbar" aria-label="Navigation">
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'home.php' ? 'active' : '' ?>" href="home.php">
    <span class="tab-icon">⌂</span><span>Home</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'trak_boxes.php' ? 'active' : '' ?>" href="trak_boxes.php">
    <span class="tab-icon">▣</span><span>TRAK</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'api.php' ? 'active' : '' ?>" href="api.php">
    <span class="tab-icon">⌘</span><span>API</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'settings.php' ? 'active' : '' ?>" href="settings.php">
    <span class="tab-icon">⚙</span><span>Settings</span>
  </a>
  <a class="<?= basename($_SERVER['PHP_SELF']) === 'account.php' ? 'active' : '' ?>" href="account.php">
    <span class="tab-icon">●</span><span>Account</span>
  </a>
</nav>
</body>
</html>
<?php }
