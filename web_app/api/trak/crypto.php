<?php

declare(strict_types=1);

function trakCryptoDbPath(): string
{
    // Repository root/data: outside the public web_app directory.
    return dirname(__DIR__, 3) . '/data/trak.sqlite';
}

function trakCryptoDb(): PDO
{
    $dir = dirname(trakCryptoDbPath());
    if (!is_dir($dir) && !@mkdir($dir, 0700, true) && !is_dir($dir)) throw new RuntimeException('database_dir_unavailable');
    $pdo = new PDO('sqlite:' . trakCryptoDbPath(), null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    $pdo->exec('PRAGMA foreign_keys = ON');
    $pdo->exec('CREATE TABLE IF NOT EXISTS trak_keys (trak_id TEXT PRIMARY KEY, encryption_key TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)');
    return $pdo;
}
function validateTrakEncryptionKey(string $key): bool { return preg_match('/^[A-Fa-f0-9]{64}$/', $key) === 1; }
function saveTrakEncryptionKey(string $trakId, string $key): void { if ($trakId === '' || !preg_match('/^[A-Za-z0-9._-]{1,64}$/', $trakId)) throw new InvalidArgumentException('invalid_trak_id'); if (!validateTrakEncryptionKey($key)) throw new InvalidArgumentException('invalid_encryption_key'); $pdo = trakCryptoDb(); $now = gmdate('Y-m-d\TH:i:s\Z'); $stmt = $pdo->prepare('INSERT INTO trak_keys (trak_id,encryption_key,created_at,updated_at) VALUES (:id,:key,:created,:updated) ON CONFLICT(trak_id) DO UPDATE SET encryption_key=excluded.encryption_key, updated_at=excluded.updated_at'); $stmt->execute([':id'=>$trakId, ':key'=>strtoupper($key), ':created'=>$now, ':updated'=>$now]); }
function getTrakEncryptionKey(string $trakId): ?string { $pdo = trakCryptoDb(); $stmt = $pdo->prepare('SELECT encryption_key FROM trak_keys WHERE trak_id = :id LIMIT 1'); $stmt->execute([':id'=>$trakId]); $row = $stmt->fetch(); return $row ? (string)$row['encryption_key'] : null; }
function decryptTrakJson(array $envelope, string $expectedTrakId): array { $trakId = trim((string)($envelope['trak_id'] ?? '')); if ($trakId === '' || !hash_equals($expectedTrakId, $trakId)) throw new RuntimeException('trak_id_mismatch'); if (($envelope['alg'] ?? '') !== 'A256GCM') throw new RuntimeException('unsupported_cipher'); $binary = base64_decode((string)($envelope['data'] ?? ''), true); if ($binary === false || strlen($binary) < 28) throw new RuntimeException('invalid_ciphertext'); $keyHex = getTrakEncryptionKey($trakId); if ($keyHex === null || !validateTrakEncryptionKey($keyHex)) throw new RuntimeException('encryption_key_not_found'); $key = hex2bin($keyHex); $nonce = substr($binary, 0, 12); $tag = substr($binary, -16); $ciphertext = substr($binary, 12, -16); $plaintext = openssl_decrypt($ciphertext, 'aes-256-gcm', $key, OPENSSL_RAW_DATA, $nonce, $tag); if ($plaintext === false) throw new RuntimeException('decrypt_failed'); $data = json_decode($plaintext, true); if (!is_array($data)) throw new RuntimeException('decrypted_json_invalid'); return $data; }
