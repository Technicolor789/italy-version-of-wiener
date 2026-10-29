<?php
// Line numbers and destinations for live feeds whose route_ids are internal
// codes (Autolinee Toscane: route 550 is line 31). Built from routes.txt,
// trips.txt and stops.txt of the static GTFS, kept in gtfs/<feed>/.
//
// CLI: php gtfs_names.php build toscana gtfs/toscana

require_once __DIR__ . '/gtfs_static.php';  // pb_* helpers

function gtfs_names_path(string $feed): string
{
    return __DIR__ . "/data/names_$feed.sqlite";
}

function gtfs_names_open(string $feed): ?PDO
{
    $path = gtfs_names_path($feed);
    if (!is_file($path)) {
        return null;
    }
    $db = new PDO("sqlite:$path");
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    return $db;
}

function gtfs_names_csv(string $file, callable $fn): void
{
    $fh = fopen($file, 'r');
    $header = fgetcsv($fh, 0, ',', '"', '');
    $header[0] = preg_replace('/^\xEF\xBB\xBF/', '', $header[0]);
    $n = count($header);
    while (($row = fgetcsv($fh, 0, ',', '"', '')) !== false) {
        if ($row === [null]) {
            continue;
        }
        $fn(array_combine($header, array_slice(array_pad($row, $n, ''), 0, $n)));
    }
    fclose($fh);
}

function gtfs_names_build(string $dir, string $target): void
{
    $tmp = "$target.building";
    @unlink($tmp);
    $db = new PDO("sqlite:$tmp");
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->exec('PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;
               CREATE TABLE routes (route_id TEXT PRIMARY KEY, name TEXT);
               CREATE TABLE trips (trip_id TEXT PRIMARY KEY, route_id TEXT, headsign TEXT);
               CREATE TABLE stops (stop_id TEXT PRIMARY KEY, name TEXT, code TEXT);');
    $db->beginTransaction();
    $ins = $db->prepare('INSERT OR REPLACE INTO routes VALUES (?, ?)');
    gtfs_names_csv("$dir/routes.txt", function ($r) use ($ins) {
        $ins->execute([$r['route_id'], trim($r['route_short_name'] ?? '') ?: $r['route_id']]);
    });
    $ins = $db->prepare('INSERT OR REPLACE INTO trips VALUES (?, ?, ?)');
    gtfs_names_csv("$dir/trips.txt", function ($r) use ($ins) {
        $ins->execute([$r['trip_id'], $r['route_id'], gtfs_static_title($r['trip_headsign'] ?? '')]);
    });
    if (is_file("$dir/stops.txt")) {
        $ins = $db->prepare('INSERT OR REPLACE INTO stops VALUES (?, ?, ?)');
        gtfs_names_csv("$dir/stops.txt", function ($r) use ($ins) {
            $ins->execute([$r['stop_id'], trim($r['stop_name'] ?? ''), trim($r['stop_code'] ?? '')]);
        });
    }
    $db->commit();
    $db->exec('VACUUM');
    $db = null;
    rename($tmp, $target);
}

// Splits a protobuf message into [field, wire type, value, raw bytes].
function pb_fields(string $buf): array
{
    $out = [];
    $pos = 0;
    $len = strlen($buf);
    while ($pos < $len) {
        $start = $pos;
        $tag = gtfs_read_varint($buf, $pos);
        $wire = $tag & 7;
        if ($wire === 0) {
            $value = gtfs_read_varint($buf, $pos);
        } elseif ($wire === 2) {
            $size = gtfs_read_varint($buf, $pos);
            $value = substr($buf, $pos, $size);
            $pos += $size;
        } elseif ($wire === 1 || $wire === 5) {
            $size = $wire === 1 ? 8 : 4;
            $value = substr($buf, $pos, $size);
            $pos += $size;
        } else {
            throw new RuntimeException('Unexpected wire type');
        }
        if ($pos > $len) {
            throw new RuntimeException('Truncated message');
        }
        $out[] = [$tag >> 3, $wire, $value, substr($buf, $start, $pos - $start)];
    }
    return $out;
}

// Adds the line number (as route_id) and the destination (trip_headsign)
// to every TripUpdate. A repeated `trip` field is merged by protobuf
// parsers, so appending trip{route_id} replaces only the route_id.
function gtfs_names_enrich(PDO $db, string $feed): string
{
    $q_trip = $db->prepare('SELECT t.headsign, r.name FROM trips t LEFT JOIN routes r ON r.route_id = t.route_id WHERE t.trip_id = ?');
    $q_route = $db->prepare('SELECT name FROM routes WHERE route_id = ?');
    $out = '';
    foreach (pb_fields($feed) as [$field, $wire, $value, $raw]) {
        if ($field !== 2 || $wire !== 2) {
            $out .= $raw;
            continue;
        }
        $entity = '';
        foreach (pb_fields($value) as [$f, $w, $v, $r]) {
            if ($f !== 3 || $w !== 2) {
                $entity .= $r;
                continue;
            }
            $trip_id = $route_id = '';
            $has_headsign = false;
            foreach (pb_fields($v) as [$tf, $tw, $tv]) {
                if ($tf === 1 && $tw === 2) {
                    foreach (pb_fields($tv) as [$df, $dw, $dv]) {
                        if ($df === 1 && $dw === 2) $trip_id = $dv;
                        if ($df === 5 && $dw === 2) $route_id = $dv;
                    }
                } elseif ($tf === 6 && $tw === 2) {
                    $has_headsign = true;
                }
            }
            $q_trip->execute([$trip_id]);
            [$headsign, $name] = $q_trip->fetch(PDO::FETCH_NUM) ?: ['', null];
            if (!$name && $route_id !== '') {
                $q_route->execute([$route_id]);
                $name = $q_route->fetchColumn() ?: null;
            }
            if ($name) {
                $v .= pb_bytes(1, pb_bytes(5, $name));
            }
            if ($headsign && !$has_headsign) {
                $v .= pb_bytes(6, pb_bytes(5, $headsign));
            }
            $entity .= pb_bytes(3, $v);
        }
        $out .= pb_bytes(2, $entity);
    }
    return $out;
}

function gtfs_names_search(PDO $db, string $text, int $limit = 25): array
{
    $q = $db->prepare("SELECT stop_id AS id, name, code FROM stops WHERE name LIKE ? OR code = ? ORDER BY name LIMIT $limit");
    $q->execute(['%' . $text . '%', $text]);
    return $q->fetchAll(PDO::FETCH_ASSOC);
}

if (PHP_SAPI === 'cli' && realpath($argv[0] ?? '') === __FILE__ && ($argv[1] ?? '') === 'build') {
    @mkdir(__DIR__ . '/data');
    $t = microtime(true);
    gtfs_names_build($argv[3], gtfs_names_path($argv[2]));
    printf("%s: %.1f MB in %.0f s\n", $argv[2], filesize(gtfs_names_path($argv[2])) / 1e6, microtime(true) - $t);
}
