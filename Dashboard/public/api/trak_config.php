<?php
header('Content-Type: application/json; charset=utf-8');
if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') {
    http_response_code(405);
    echo '{"ok":false,"error":"method_not_allowed"}';
    exit;
}
echo '{"ok":true,"stage":"php_reached"}';
