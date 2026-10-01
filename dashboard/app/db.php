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
        email_verified_at TEXT,
        pending_email TEXT,
        email_token_hash TEXT,
        email_token_expires INTEGER,
        role TEXT NOT NULL CHECK(role IN (\'admin\', \'user\')),
        created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )');
    $columns = $pdo->query('PRAGMA table_info(users)')->fetchAll(PDO::FETCH_COLUMN, 1);
    $migrations = [
        'email' => 'ALTER TABLE users ADD COLUMN email TEXT',
        'phone' => 'ALTER TABLE users ADD COLUMN phone TEXT',
        'email_verified_at' => 'ALTER TABLE users ADD COLUMN email_verified_at TEXT',
        'pending_email' => 'ALTER TABLE users ADD COLUMN pending_email TEXT',
        'email_token_hash' => 'ALTER TABLE users ADD COLUMN email_token_hash TEXT',
        'email_token_expires' => 'ALTER TABLE users ADD COLUMN email_token_expires INTEGER',
    ];
    foreach ($migrations as $column => $sql) {
        if (!in_array($column, $columns, true)) $pdo->exec($sql);
    }
    $pdo->exec('CREATE TABLE IF NOT EXISTS password_resets (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER NOT NULL,
        token_hash TEXT NOT NULL UNIQUE,
        expires_at INTEGER NOT NULL,
        used_at INTEGER,
        created_at INTEGER NOT NULL,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE
    )');
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_password_resets_token ON password_resets(token_hash)');
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_password_resets_user ON password_resets(user_id)');
    $pdo->exec('CREATE TABLE IF NOT EXISTS trak_boxes (id INTEGER PRIMARY KEY AUTOINCREMENT, trak_id TEXT NOT NULL UNIQUE, phone TEXT NOT NULL, api_key TEXT NOT NULL, osmand_url TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)');
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_trak_boxes_phone ON trak_boxes(phone)');
    return $pdo;
}
