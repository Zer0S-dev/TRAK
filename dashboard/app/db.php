<?php
declare(strict_types=1);
require_once __DIR__ . '/config.php';

function db(): PDO {
    static $pdo = null;
    if ($pdo instanceof PDO) return $pdo;

    $dir = dirname(DB_PATH);
    if (!is_dir($dir)) mkdir($dir, 0700, true);

    $pdo = new PDO('sqlite:' . DB_PATH, null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        PDO::ATTR_EMULATE_PREPARES => false,
    ]);
    $pdo->exec('PRAGMA foreign_keys = ON');
    $pdo->exec('PRAGMA journal_mode = WAL');
    $pdo->exec('CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL COLLATE NOCASE UNIQUE,
        password_hash TEXT NOT NULL,
        email TEXT,
        phone TEXT,
        role TEXT NOT NULL CHECK(role IN ('admin', 'user')),
        created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )');

    $columns = $pdo->query('PRAGMA table_info(users)')->fetchAll(PDO::FETCH_COLUMN, 1);
    if (!in_array('email', $columns, true)) $pdo->exec('ALTER TABLE users ADD COLUMN email TEXT');
    if (!in_array('phone', $columns, true)) $pdo->exec('ALTER TABLE users ADD COLUMN phone TEXT');

    return $pdo;
}
