<?php
// Checks gtfs_filter.php against the feed written by make_test_feed.py.
require __DIR__ . '/../gtfs_filter.php';

$feed = file_get_contents($argv[1]);
$filtered = gtfs_filter_stops($feed, ['70017', ' 70018']);
$tmp = tempnam(sys_get_temp_dir(), 'pb');
file_put_contents($tmp, $filtered);
$py = <<<'PY'
import sys
from google.transit import gtfs_realtime_pb2 as g
f = g.FeedMessage()
f.ParseFromString(open(sys.argv[1], 'rb').read())
print(f.header.timestamp, sorted(e.trip_update.trip.route_id for e in f.entity))
PY;
$got = trim(shell_exec('python3 -c ' . escapeshellarg($py) . ' ' . escapeshellarg($tmp)));
unlink($tmp);
$want = "1764590400 ['19', '40', '64', '8', '8', 'C3', 'H', 'MEA']";
printf("proxy filter: %d -> %d bytes, %s\n", strlen($feed), strlen($filtered), $got);
if ($got !== $want) {
    echo "FAILED, expected $want\n";
    exit(1);
}
echo "PASSED\n";
