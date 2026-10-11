<?php
declare(strict_types=1);
require_once __DIR__ . '/../app/auth.php';

if (user_count() === 0) {
    header('Location: register.php');
    exit;
}
if (!current_user()) {
    header('Location: login.php');
    exit;
}
header('Location: home.php');
exit;
