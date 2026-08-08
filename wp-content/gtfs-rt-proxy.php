<?php
/**
 * GTFS-RT Proxy for Altervista WordPress
 *
 * Standalone proxy: does not require WordPress to be loaded.
 *
 * Usage:
 *   https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?url=<ENCODED_FEED_URL>
 *
 * Preset feeds:
 *   toscana
 *   atac_roma
 *   actv_veneto
 *   busitalia_veneto
 *   busitalia_tram
 *
 * Example:
 *   https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?feed=toscana
 *
 * The proxy fetches the remote GTFS-RT feed and returns it with the
 * application/x-protobuf Content-Type so it can be consumed by ESP32
 * or other clients without running into WordPress/Altervista .pb issues.
 */

// Known feeds
$FEEDS = [
    'toscana'        => 'https://regionetoscana.smartregion.toscana.it/mobility/artifacts/gtfs-rt/trip-updates',
    'atac_roma'      => 'https://romamobilita.it/sites/default/files/rome_rtgtfs_trip_updates_feed.pb',
    'actv_veneto'    => 'https://tpl.actv.it/aut/GTFSRT/tripUpdates',
    'busitalia_veneto' => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV/start-gtfs-rt-trip-updates-fc.pb',
    'busitalia_tram' => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV-TRAM/gtfs-rt-trip-updates.pb',
];

$remote_url = null;

if (isset($_GET['feed']) && $remote_url === null) {
    $feed = strtolower((string) $_GET['feed']);
    if (isset($FEEDS[$feed])) {
        $remote_url = $FEEDS[$feed];
    }
}

if (isset($_GET['url']) && $remote_url === null) {
    $remote_url = (string) $_GET['url'];
}

if ($remote_url === null) {
    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode([
        'error' => 'Missing feed or url parameter',
        'feeds' => array_keys($FEEDS),
    ]);
    exit;
}

// Basic validation: only allow http(s) schemes
$parsed = parse_url($remote_url);
if ($parsed === false || empty($parsed['scheme']) || !in_array(strtolower($parsed['scheme']), ['http', 'https'], true)) {
    http_response_code(400);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Invalid URL scheme']);
    exit;
}

// If the requested URL is not one of the preset feeds, require explicit allow
$allowed_hosts = array_values(array_unique(array_filter(array_map(function ($u) {
    $p = parse_url($u);
    return $p['host'] ?? null;
}, $FEEDS))));

if (!in_array($parsed['host'], $allowed_hosts, true)) {
    http_response_code(403);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Host not allowed', 'host' => $parsed['host']]);
    exit;
}

// Check cURL availability
if (!function_exists('curl_init')) {
    http_response_code(500);
    header('Content-Type: application/json');
    echo json_encode(['error' => 'cURL is not available on this server']);
    exit;
}

// Fetch remote content
$ch = curl_init($remote_url);
curl_setopt_array($ch, [
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_FOLLOWLOCATION => true,
    CURLOPT_MAXREDIRS      => 5,
    CURLOPT_CONNECTTIMEOUT => 10,
    CURLOPT_TIMEOUT        => 30,
    CURLOPT_SSL_VERIFYPEER => true,
    CURLOPT_USERAGENT      => 'GTFS-RT-Proxy/1.0',
]);

$body = curl_exec($ch);
$http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
$curl_error = curl_error($ch);
$errno = curl_errno($ch);
curl_close($ch);

if ($body === false || $http_code !== 200) {
    http_response_code(502);
    header('Content-Type: application/json');
    echo json_encode([
        'error'      => 'Failed to fetch remote feed',
        'http_code'  => $http_code,
        'errno'      => $errno,
        'curl_error' => $curl_error ?: null,
        'remote_url' => $remote_url,
    ]);
    exit;
}

// Serve the raw protobuf with the correct MIME type
header('Content-Type: application/x-protobuf');
header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');
header('Expires: 0');
header('Content-Length: ' . strlen($body));
echo $body;
