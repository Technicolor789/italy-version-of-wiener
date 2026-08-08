<?php
/**
 * Plugin Name: GTFS-RT Proxy
 * Description: Proxy GTFS-RT .pb feeds for Altervista WordPress.
 * Version: 1.0
 */

add_action('init', function () {
    add_rewrite_rule('^gtfs-rt-proxy/([^/]+)/?', 'index.php?gtfs_rt_proxy=1&feed=$matches[1]', 'top');
});

add_filter('query_vars', function ($vars) {
    $vars[] = 'gtfs_rt_proxy';
    $vars[] = 'feed';
    return $vars;
});

add_action('template_redirect', function () {
    if (!get_query_var('gtfs_rt_proxy')) return;

    $feed = get_query_var('feed');
    if (empty($feed)) {
        status_header(400);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Missing feed']);
        exit;
    }

    $FEEDS = [
        'toscana'         => 'https://regionetoscana.smartregion.toscana.it/mobility/artifacts/gtfs-rt/trip-updates',
        'atac_roma'       => 'https://romamobilita.it/sites/default/files/rome_rtgtfs_trip_updates_feed.pb',
        'actv_veneto'     => 'https://tpl.actv.it/aut/GTFSRT/tripUpdates',
        'busitalia_veneto'=> 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV/start-gtfs-rt-trip-updates-fc.pb',
        'busitalia_tram'  => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV-TRAM/gtfs-rt-trip-updates.pb',
    ];

    $remote_url = $FEEDS[strtolower($feed)] ?? null;
    if (!$remote_url) {
        status_header(404);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Unknown feed', 'feed' => $feed]);
        exit;
    }

    if (!function_exists('curl_init')) {
        status_header(500);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'cURL not available']);
        exit;
    }

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
    curl_close($ch);

    if ($body === false || $http_code !== 200) {
        status_header(502);
        header('Content-Type: application/json');
        echo json_encode([
            'error'     => 'Failed to fetch remote feed',
            'http_code' => $http_code,
            'feed'      => $feed,
        ]);
        exit;
    }

    header('Content-Type: application/x-protobuf');
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
    header('Content-Length: ' . strlen($body));
    echo $body;
    exit;
});

register_activation_hook(__FILE__, function () {
    flush_rewrite_rules();
});

register_deactivation_hook(__FILE__, function () {
    flush_rewrite_rules();
});
