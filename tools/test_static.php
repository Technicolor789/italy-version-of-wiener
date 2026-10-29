<?php
// Checks gtfs_static.php on the zip written by make_test_gtfs.py.
require __DIR__ . '/../gtfs_static.php';

$db_path = sys_get_temp_dir() . '/gtfs_test_' . getmypid() . '.sqlite';
gtfs_static_build('test', $argv[1], $db_path);
$db = new PDO("sqlite:$db_path");
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
date_default_timezone_set('Europe/Rome');

function deps($db, $when) {
    $out = [];
    foreach (gtfs_static_departures($db, ['S_MI'], strtotime($when)) as $d) {
        $out[] = $d['route'] . ' ' . $d['headsign'] . ' ' . date('H:i', $d['time']);
    }
    return implode(' | ', $out);
}

$checks = [
    // weekday: S5 via the platform child stop, holiday-only service added by calendar_dates
    ['2026-12-01 08:35', 'S5 Treviglio 08:42 | Milano Cadorna - Malpensa Malpensa Aeroporto 08:50 | S5 Treviglio 09:12'],
    // yesterday's night train at 24:35
    ['2026-12-02 00:20', 'S5 Varese 00:35'],
    // FER removed on 8 December
    ['2026-12-08 08:35', ''],
];
$fail = 0;
foreach ($checks as [$when, $want]) {
    $got = deps($db, $when);
    if ($got !== $want) {
        echo "FAIL $when: got '$got', want '$want'\n";
        $fail++;
    }
}
$found = array_column(gtfs_static_search($db, 'garib'), 'id');
if ($found !== ['S_MI', 'S_MI_1']) {
    echo "FAIL search: " . implode(',', $found) . "\n";
    $fail++;
}
file_put_contents($argv[2], gtfs_static_feed(gtfs_static_departures($db, ['S_MI'], strtotime('2026-12-01 08:35')), 1796110500));
unlink($db_path);
echo $fail ? "FAILED\n" : "PASSED\n";
exit($fail ? 1 : 0);
