<?php
require __DIR__ . '/gtfs_filter.php';
require __DIR__ . '/gtfs_static.php';

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
    http_response_code(400);
    echo json_encode([
        'error' => 'Missing feed parameter',
        'usage' => '?feed=toscana',
        'available' => array_merge(array_keys($FEEDS), array_keys(GTFS_STATIC_FEEDS))
    ]);
    exit;
}

// Timetable-only feeds (es. Trenord): ?search=NAME finds stop ids,
// ?stops=ID,ID returns the next scheduled departures as GTFS-RT.
if (isset(GTFS_STATIC_FEEDS[$feed])) {
    try {
        $db = gtfs_static_open($feed);
        if (isset($_GET['search'])) {
            header('Content-Type: application/json; charset=utf-8');
            echo json_encode(gtfs_static_search($db, trim($_GET['search'])), JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
            exit;
        }
        $stops = array_values(array_filter(array_map('trim', explode(',', $_GET['stops'] ?? '')), 'strlen'));
        if (!$stops) {
            http_response_code(400);
            echo json_encode(['error' => 'Missing stops parameter', 'usage' => "?feed=$feed&stops=ID or ?feed=$feed&search=Milano"]);
            exit;
        }
        $body = gtfs_static_feed(gtfs_static_departures($db, $stops, time()), time());
    } catch (Throwable $e) {
        http_response_code(503);
        echo json_encode(['error' => $e->getMessage()]);
        exit;
    }
    header('Content-Type: application/x-protobuf');
    header('Cache-Control: no-cache');
    header('Content-Length: ' . strlen($body));
    echo $body;
    exit;
}

$remote_url = $FEEDS[$feed] ?? null;
if (!$remote_url) {
    http_response_code(404);
    echo json_encode([
        'error' => 'Unknown feed',
        'requested' => $feed,
        'available' => array_keys($FEEDS)
    ]);
    exit;
}

// Optional ?stops=ID1,ID2: return only the trips serving these stops.
$stops = array_filter(array_map('trim', explode(',', $_GET['stops'] ?? '')), 'strlen');

// Several displays (or a browser and a display) share one download.
$cache_file = sys_get_temp_dir() . '/gtfsrt_' . $feed . '.pb';
$cache_ttl = 15;
$body = false;
$http_code = 200;
$curl_error = '';
if (is_file($cache_file) && time() - filemtime($cache_file) < $cache_ttl) {
    $body = file_get_contents($cache_file);
}

if ($body === false) {
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
    // Feeds behind HTTP Basic auth: set FEED_AUTH_<FEED>=user:password,
    // es. FEED_AUTH_BUSITALIA_VENETO, in the server environment.
    $auth = getenv('FEED_AUTH_' . strtoupper($feed));
    if ($auth) {
        curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_setopt($ch, CURLOPT_USERPWD, $auth);
    }

    $body = curl_exec($ch);
    $http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $curl_error = curl_error($ch);
    curl_close($ch);
    if ($body !== false && $http_code === 200) {
        file_put_contents($cache_file, $body, LOCK_EX);
    }
}

if ($body === false || $http_code !== 200) {
    http_response_code(502);
    echo json_encode([
        'error' => 'Failed to fetch feed',
        'http_code' => $http_code,
        'curl_error' => $curl_error,
        'remote_url' => $remote_url
    ]);
    exit;
}

if ($stops) {
    try {
        $body = gtfs_filter_stops($body, $stops);
    } catch (RuntimeException $e) {
        http_response_code(502);
        echo json_encode(['error' => 'Invalid feed: ' . $e->getMessage()]);
        exit;
    }
}

header('Content-Type: application/x-protobuf');
header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');
header('Expires: 0');
header('Content-Length: ' . strlen($body));
echo $body;
