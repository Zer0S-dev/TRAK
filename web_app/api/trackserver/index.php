<?php

declare(strict_types=1);

require_once dirname(__DIR__) . '/config.php';
requireDashboardAuth();

header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');

if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    jsonResponse(['ok' => true, 'url' => getTrackserverConfiguredUrl()]);
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    header('Allow: GET, POST');
    jsonResponse(['ok' => false, 'error' => 'method_not_allowed'], 405);
}

requireCsrf();

$raw = file_get_contents('php://input');
$data = json_decode($raw ?: '', true);
if (!is_array($data)) jsonResponse(['ok' => false, 'error' => 'invalid_json'], 400);

$url = trim((string) ($data['url'] ?? ''));
if ($url !== '' && !filter_var($url, FILTER_VALIDATE_URL)) jsonResponse(['ok' => false, 'error' => 'invalid_url'], 422);
if ($url !== '' && !preg_match('#^https?://#i', $url)) jsonResponse(['ok' => false, 'error' => 'url_must_be_http'], 422);
if (strlen($url) > 2048) jsonResponse(['ok' => false, 'error' => 'url_too_long'], 422);

$settings = loadSettings();
$settings['trackserver_url'] = $url;
saveSettings($settings);

jsonResponse(['ok' => true, 'url' => $url]);
