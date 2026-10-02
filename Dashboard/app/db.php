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
    $pdo->exec('CREATE TABLE IF NOT EXISTS trak_boxes (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        user_id INTEGER,
        trak_id TEXT NOT NULL UNIQUE,
        phone TEXT NOT NULL,
        api_key TEXT NOT NULL,
        trakserver_url TEXT NOT NULL,
        created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
        updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE SET NULL
    )');
    $trakColumns = $pdo->query('PRAGMA table_info(trak_boxes)')->fetchAll(PDO::FETCH_COLUMN, 1);
    if (!in_array('user_id', $trakColumns, true)) {
        $pdo->exec('ALTER TABLE trak_boxes ADD COLUMN user_id INTEGER');
    }
    if (!in_array('trakserver_url', $trakColumns, true) && in_array('osmand_url', $trakColumns, true)) {
        $pdo->exec('ALTER TABLE trak_boxes RENAME COLUMN osmand_url TO trakserver_url');
        $trakColumns = $pdo->query('PRAGMA table_info(trak_boxes)')->fetchAll(PDO::FETCH_COLUMN, 1);
    }
    if (!in_array('trakserver_url', $trakColumns, true)) {
        $pdo->exec("ALTER TABLE trak_boxes ADD COLUMN trakserver_url TEXT NOT NULL DEFAULT ''");
    }
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_trak_boxes_phone ON trak_boxes(phone)');
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_trak_boxes_user ON trak_boxes(user_id)');

    // Dernière position connue de chaque TRAK.
    $pdo->exec('CREATE TABLE IF NOT EXISTS trak_positions (
        trak_box_id INTEGER PRIMARY KEY,
        latitude REAL NOT NULL,
        longitude REAL NOT NULL,
        gps_timestamp TEXT,
        received_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
        FOREIGN KEY(trak_box_id) REFERENCES trak_boxes(id) ON DELETE CASCADE
    )');
    $pdo->exec('CREATE INDEX IF NOT EXISTS idx_trak_positions_received ON trak_positions(received_at)');

    // Repair legacy TRAK rows created before user_id existed.
    // If there is exactly one user, orphan TRAK boxes belong to that user.
    $userCount = (int)$pdo->query('SELECT COUNT(*) FROM users')->fetchColumn();
    if ($userCount === 1) {
        $onlyUserId = (int)$pdo->query('SELECT id FROM users LIMIT 1')->fetchColumn();
        $stmt = $pdo->prepare('UPDATE trak_boxes SET user_id = ? WHERE user_id IS NULL');
        $stmt->execute([$onlyUserId]);
    }
    return $pdo;
}
