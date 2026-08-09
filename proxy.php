<?php
// CORS must be sent before any other output
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Access-Control-Max-Age: 86400');

// Handle preflight immediately
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}

$FEEDS = [
    'toscana' => 'https://regionetoscana.smartregion.toscana.it/mobility/artifacts/gtfs-rt/trip-updates',
    'atac_roma' => 'https://romamobilita.it/sites/default/files/rome_rtgtfs_trip_updates_feed.pb',
    'actv_veneto' => 'https://tpl.actv.it/aut/GTFSRT/tripUpdates',
    'busitalia_veneto' => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV/start-gtfs-rt-trip-updates-fc.pb',
    'busitalia_tram' => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV-TRAM/gtfs-rt-trip-updates.pb',
];

// Accept feed from query param ?feed= or from URL path
$feed = strtolower($_GET['feed'] ?? '');
if (empty($feed) && isset($_SERVER['PATH_INFO'])) {
    $path = trim($_SERVER['PATH_INFO'], '/');
    $segments = explode('/', $path);
    $feed = strtolower(end($segments));
}

if (empty($feed)) {
    echo json_encode([
        'error' => 'Missing feed parameter',
        'usage' => '?feed=toscana',
        'available' => array_keys($FEEDS)
    ]);
    exit;
}

$remote_url = $FEEDS[$feed] ?? null;
if (!$remote_url) {
    echo json_encode([
        'error' => 'Unknown feed',
        'requested' => $feed,
        'available' => array_keys($FEEDS)
    ]);
    exit;
}

$ch = curl_init($remote_url);
curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_FOLLOWLOCATION => true,
    CURLOPT_MAXREDIRS => 5,
    CURLOPT_CONNECTTIMEOUT => 10,
    CURLOPT_TIMEOUT => 30,
    CURLOPT_SSL_VERIFYPEER => true,
    CURLOPT_USERAGENT => 'GTFS-RT-Proxy/1.0',
]);

$body = curl_exec($ch);
$http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
$curl_error = curl_error($ch);
curl_close($ch);

if ($body === false || $http_code !== 200) {
    echo json_encode([
        'error' => 'Failed to fetch feed',
        'http_code' => $http_code,
        'curl_error' => $curl_error,
        'remote_url' => $remote_url
    ]);
    exit;
}

header('Content-Type: application/x-protobuf');
header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');
header('Expires: 0');
header('Content-Length: ' . strlen($body));
echo $body;
