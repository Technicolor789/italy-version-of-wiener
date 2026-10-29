<?php
// Scheduled departures from a static GTFS (es. Trenord), served in the same
// GTFS-Realtime format as the live feeds so the display needs no changes.
//
// The GTFS zip is imported once into SQLite (data/gtfs_<feed>.sqlite, built
// by the Dockerfile) and refreshed in the temp dir when older than a week.
//
// CLI: php gtfs_static.php build trenord   (re)builds the database
//      php gtfs_static.php build trenord /path/to/gtfs.zip

// feed => URL of the GTFS zip, or "ckan:<portal>|<dataset>" to take the
// newest zip of a CKAN open data dataset ('' = only gtfs/<feed>.zip).
const GTFS_STATIC_FEEDS = [
    'trenord' => 'https://www.dati.lombardia.it/download/3z4k-mxz9/application%2Fzip',
    'gest' => '',  // Tramvia di Firenze: gtfs/gest.zip
    'atm' => 'ckan:https://dati.comune.milano.it|ds929-orari-del-trasporto-pubblico-locale-nel-comune-di-milano-in-formato-gtfs',
];
const GTFS_STATIC_MAX_AGE = 7 * 86400;
const GTFS_STATIC_TZ = 'Europe/Rome';

function gtfs_static_paths(string $feed): array
{
    return [
        sys_get_temp_dir() . "/gtfs_$feed.sqlite",  // refreshed at runtime
        __DIR__ . "/data/gtfs_$feed.sqlite",         // baked into the image
    ];
}

// Newest existing database, or null.
function gtfs_static_find(string $feed): ?string
{
    $best = null;
    foreach (gtfs_static_paths($feed) as $p) {
        if (is_file($p) && filesize($p) > 0 && (!$best || filemtime($p) > filemtime($best))) {
            $best = $p;
        }
    }
    return $best;
}

// Returns a PDO for the feed, rebuilding it when missing or old. Throws if
// there is no usable database (es. first build still running).
function gtfs_static_open(string $feed): PDO
{
    $db = gtfs_static_find($feed);
    if (!$db && GTFS_STATIC_FEEDS[$feed] === '') {
        throw new RuntimeException("Orari $feed non disponibili");
    }
    if (GTFS_STATIC_FEEDS[$feed] !== '' && (!$db || time() - filemtime($db) > GTFS_STATIC_MAX_AGE)) {
        $target = gtfs_static_paths($feed)[0];
        $lock = fopen("$target.lock", 'c');
        if ($lock && flock($lock, LOCK_EX | LOCK_NB)) {
            // Importing ATM takes a while: finish even if this client gives up.
            ignore_user_abort(true);
            set_time_limit(0);
            try {
                gtfs_static_build($feed, GTFS_STATIC_FEEDS[$feed], $target);
                $db = $target;
            } catch (Throwable $e) {
                if (!$db) {
                    throw $e;
                }
                error_log("gtfs_static: refresh of $feed failed: " . $e->getMessage());
            } finally {
                flock($lock, LOCK_UN);
            }
        } elseif (!$db) {
            throw new RuntimeException('Orari in preparazione, riprova tra un minuto');
        }
    }
    $pdo = new PDO("sqlite:$db");
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    return $pdo;
}

// ATM writes everything in lower case: "sesto 1 maggio fs" -> "Sesto 1 Maggio Fs".
function gtfs_static_title(string $s): string
{
    $s = trim($s);
    if ($s === '' || strtolower($s) !== $s) {
        return $s;
    }
    return function_exists('mb_convert_case') ? mb_convert_case($s, MB_CASE_TITLE, 'UTF-8') : ucwords($s);
}

function gtfs_static_secs(string $hms): ?int
{
    $p = explode(':', trim($hms));
    if (count($p) < 2 || $p[0] === '') {
        return null;
    }
    return (int)$p[0] * 3600 + (int)$p[1] * 60 + (int)($p[2] ?? 0);
}

// Streams one CSV file of the zip, calling $fn(array $row) with named columns.
function gtfs_static_rows(ZipArchive $zip, string $name, callable $fn, bool $required = true): void
{
    $idx = $zip->locateName($name, ZipArchive::FL_NODIR | ZipArchive::FL_NOCASE);
    if ($idx === false) {
        if ($required) {
            throw new RuntimeException("$name mancante nel GTFS");
        }
        return;
    }
    $fh = $zip->getStream($zip->getNameIndex($idx));
    $header = fgetcsv($fh, 0, ',', '"', '');
    $header[0] = preg_replace('/^\xEF\xBB\xBF/', '', $header[0]);
    $header = array_map('trim', $header);
    $n = count($header);
    while (($row = fgetcsv($fh, 0, ',', '"', '')) !== false) {
        if (count($row) < $n) {
            if ($row === [null]) {
                continue;  // empty line
            }
            $row = array_pad($row, $n, '');
        }
        $fn(array_combine($header, array_slice($row, 0, $n)));
    }
    fclose($fh);
}

// Newest zip resource of a CKAN package_show result, or null.
function gtfs_static_pick_resource(array $package): ?string
{
    $best = null;
    $best_date = '';
    foreach ($package['result']['resources'] ?? [] as $res) {
        $url = $res['url'] ?? '';
        $format = strtolower($res['format'] ?? '');
        if ($url === '' || ($format !== 'zip' && !preg_match('/\.zip($|\?)/i', $url))) {
            continue;
        }
        $date = $res['last_modified'] ?? $res['metadata_modified'] ?? $res['created'] ?? '';
        if ($best === null || strcmp((string)$date, $best_date) >= 0) {
            $best = $url;
            $best_date = (string)$date;
        }
    }
    return $best;
}

function gtfs_static_resolve(string $source): string
{
    if (!str_starts_with($source, 'ckan:')) {
        return $source;
    }
    [$portal, $dataset] = explode('|', substr($source, 5), 2);
    $api = rtrim($portal, '/') . '/api/3/action/package_show?id=' . urlencode($dataset);
    $ch = curl_init($api);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_FOLLOWLOCATION => true,
        CURLOPT_CONNECTTIMEOUT => 20,
        CURLOPT_TIMEOUT => 60,
        CURLOPT_USERAGENT => 'GTFS-RT-Proxy/1.0',
    ]);
    $json = curl_exec($ch);
    curl_close($ch);
    $url = $json ? gtfs_static_pick_resource(json_decode($json, true) ?: []) : null;
    if (!$url) {
        throw new RuntimeException("Nessuno zip trovato nel dataset $dataset");
    }
    return $url;
}

function gtfs_static_build(string $feed, string $source, string $target): void
{
    $source = gtfs_static_resolve($source);
    $zip_path = $source;
    if (preg_match('#^https?://#', $source)) {
        $zip_path = tempnam(sys_get_temp_dir(), "gtfs_$feed");
        $out = fopen($zip_path, 'w');
        $ch = curl_init($source);
        curl_setopt_array($ch, [
            CURLOPT_FILE => $out,
            CURLOPT_FOLLOWLOCATION => true,
            CURLOPT_CONNECTTIMEOUT => 20,
            CURLOPT_TIMEOUT => 600,
            CURLOPT_USERAGENT => 'GTFS-RT-Proxy/1.0',
        ]);
        $ok = curl_exec($ch);
        $code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
        fclose($out);
        if (!$ok || $code !== 200) {
            @unlink($zip_path);
            throw new RuntimeException("Download del GTFS fallito (HTTP $code)");
        }
    }

    $zip = new ZipArchive();
    if ($zip->open($zip_path) !== true) {
        throw new RuntimeException('Lo zip del GTFS non si apre');
    }

    $tmp = "$target.building";
    @unlink($tmp);
    $db = new PDO("sqlite:$tmp");
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $db->exec('PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;');
    $db->exec('CREATE TABLE routes (route_id TEXT PRIMARY KEY, name TEXT);
               CREATE TABLE trips (trip_id TEXT PRIMARY KEY, route_id TEXT, service_id TEXT, headsign TEXT);
               CREATE TABLE stops (stop_id TEXT PRIMARY KEY, name TEXT, parent TEXT);
               CREATE TABLE st (trip_id TEXT, stop_id TEXT, seq INTEGER, dep INTEGER);
               CREATE TABLE calendar (service_id TEXT, days TEXT, start TEXT, end TEXT);
               CREATE TABLE calendar_dates (service_id TEXT, date TEXT, type INTEGER);');
    $db->beginTransaction();

    $ins = $db->prepare('INSERT OR REPLACE INTO routes VALUES (?, ?)');
    gtfs_static_rows($zip, 'routes.txt', function ($r) use ($ins) {
        $name = trim($r['route_short_name'] ?? '') ?: trim($r['route_long_name'] ?? '') ?: $r['route_id'];
        // GEST calls tram line T1 "T1.3".
        $name = preg_replace('/^(T\d+)\.\d+$/', '$1', $name);
        // Metro lines are "1", "2"... like the trams: call them M1, M2.
        if (trim($r['route_type'] ?? '') === '1' && ctype_digit($name)) {
            $name = "M$name";
        }
        $ins->execute([$r['route_id'], $name]);
    });
    $ins = $db->prepare('INSERT OR REPLACE INTO trips VALUES (?, ?, ?, ?)');
    gtfs_static_rows($zip, 'trips.txt', function ($r) use ($ins) {
        $ins->execute([$r['trip_id'], $r['route_id'], $r['service_id'], gtfs_static_title($r['trip_headsign'] ?? '')]);
    });
    $ins = $db->prepare('INSERT OR REPLACE INTO stops VALUES (?, ?, ?)');
    gtfs_static_rows($zip, 'stops.txt', function ($r) use ($ins) {
        $ins->execute([$r['stop_id'], gtfs_static_title($r['stop_name'] ?? ''), trim($r['parent_station'] ?? '')]);
    });
    $ins = $db->prepare('INSERT INTO st VALUES (?, ?, ?, ?)');
    gtfs_static_rows($zip, 'stop_times.txt', function ($r) use ($ins) {
        $dep = gtfs_static_secs($r['departure_time'] ?? '') ?? gtfs_static_secs($r['arrival_time'] ?? '');
        if ($dep !== null) {
            $ins->execute([$r['trip_id'], $r['stop_id'], (int)$r['stop_sequence'], $dep]);
        }
    });
    $ins = $db->prepare('INSERT INTO calendar VALUES (?, ?, ?, ?)');
    gtfs_static_rows($zip, 'calendar.txt', function ($r) use ($ins) {
        $days = '';
        foreach (['monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday'] as $d) {
            $days .= trim($r[$d] ?? '0') === '1' ? '1' : '0';
        }
        $ins->execute([$r['service_id'], $days, $r['start_date'], $r['end_date']]);
    }, false);
    $ins = $db->prepare('INSERT INTO calendar_dates VALUES (?, ?, ?)');
    gtfs_static_rows($zip, 'calendar_dates.txt', function ($r) use ($ins) {
        $ins->execute([$r['service_id'], $r['date'], (int)$r['exception_type']]);
    }, false);
    $db->commit();
    $zip->close();
    if ($zip_path !== $source) {
        @unlink($zip_path);
    }

    // Trips without a headsign show their last stop; nobody boards at the
    // last stop, so those rows are dropped.
    $db->exec('CREATE TABLE last AS SELECT trip_id, MAX(seq) AS seq FROM st GROUP BY trip_id;
               CREATE INDEX last_trip ON last (trip_id);
               UPDATE trips SET headsign = (
                 SELECT stops.name FROM last JOIN st ON st.trip_id = last.trip_id AND st.seq = last.seq
                 JOIN stops ON stops.stop_id = st.stop_id WHERE last.trip_id = trips.trip_id)
               WHERE headsign = \'\';
               DELETE FROM st WHERE (trip_id, seq) IN (SELECT trip_id, seq FROM last);
               DROP TABLE last;
               CREATE INDEX st_stop ON st (stop_id, dep);
               CREATE INDEX stops_parent ON stops (parent);
               CREATE INDEX cd_date ON calendar_dates (date);
               VACUUM;');
    $db = null;
    rename($tmp, $target);
}

// Next departures from the given stops (or their child stops).
function gtfs_static_departures(PDO $db, array $stops, int $now, int $horizon = 3 * 3600, int $limit = 16): array
{
    $marks = implode(',', array_fill(0, count($stops), '?'));
    $q = $db->prepare("SELECT stop_id FROM stops WHERE stop_id IN ($marks) OR parent IN ($marks)");
    $q->execute(array_merge($stops, $stops));
    $ids = $q->fetchAll(PDO::FETCH_COLUMN) ?: $stops;
    $id_marks = implode(',', array_fill(0, count($ids), '?'));

    $tz = new DateTimeZone(GTFS_STATIC_TZ);
    $today = (new DateTime('@' . $now))->setTimezone($tz)->setTime(0, 0);
    $out = [];
    // Yesterday's services can run past midnight (times like 25:10:00).
    foreach ([-1, 0] as $offset) {
        $day = (clone $today)->modify("$offset day");
        $midnight = $day->getTimestamp();
        $date = $day->format('Ymd');
        $q = $db->prepare("SELECT st.trip_id, st.stop_id, st.dep, r.name AS route, t.headsign
            FROM st JOIN trips t ON t.trip_id = st.trip_id JOIN routes r ON r.route_id = t.route_id
            WHERE st.stop_id IN ($id_marks) AND st.dep BETWEEN ? AND ?
              AND t.service_id IN (
                SELECT service_id FROM calendar
                  WHERE start <= ? AND end >= ? AND substr(days, ?, 1) = '1'
                UNION SELECT service_id FROM calendar_dates WHERE date = ? AND type = 1
                EXCEPT SELECT service_id FROM calendar_dates WHERE date = ? AND type = 2)
            ORDER BY st.dep LIMIT $limit");
        $q->execute(array_merge($ids, [
            $now - 60 - $midnight, $now + $horizon - $midnight,
            $date, $date, (int)$day->format('N'), $date, $date,
        ]));
        foreach ($q->fetchAll(PDO::FETCH_ASSOC) as $row) {
            $row['time'] = $midnight + (int)$row['dep'];
            $out[] = $row;
        }
    }
    usort($out, fn($a, $b) => $a['time'] <=> $b['time']);
    return array_slice($out, 0, $limit);
}

function gtfs_static_search(PDO $db, string $text, int $limit = 25): array
{
    $q = $db->prepare("SELECT stop_id AS id, name, parent FROM stops WHERE name LIKE ? ORDER BY name LIMIT $limit");
    $q->execute(['%' . $text . '%']);
    return $q->fetchAll(PDO::FETCH_ASSOC);
}

function pb_varint(int $v): string
{
    $out = '';
    while ($v > 0x7f) {
        $out .= chr(($v & 0x7f) | 0x80);
        $v >>= 7;
    }
    return $out . chr($v);
}

function pb_bytes(int $field, string $s): string
{
    return pb_varint(($field << 3) | 2) . pb_varint(strlen($s)) . $s;
}

function pb_uint(int $field, int $v): string
{
    return pb_varint($field << 3) . pb_varint($v);
}

// A GTFS-Realtime FeedMessage with one TripUpdate per departure.
function gtfs_static_feed(array $departures, int $now): string
{
    $out = pb_bytes(1, pb_bytes(1, '2.0') . pb_uint(3, $now));
    foreach ($departures as $i => $d) {
        $trip = pb_bytes(1, $d['trip_id']) . pb_bytes(5, $d['route']);
        $stu = pb_bytes(4, $d['stop_id']) . pb_bytes(3, pb_uint(2, $d['time']));
        $tu = pb_bytes(1, $trip) . pb_bytes(2, $stu);
        if ($d['headsign'] !== '' && $d['headsign'] !== null) {
            $tu .= pb_bytes(6, pb_bytes(5, $d['headsign']));
        }
        $out .= pb_bytes(2, pb_bytes(1, "s$i") . pb_bytes(3, $tu));
    }
    return $out;
}

if (PHP_SAPI === 'cli' && realpath($argv[0] ?? '') === __FILE__ && ($argv[1] ?? '') === 'build') {
    $feed = $argv[2] ?? '';
    if (!isset(GTFS_STATIC_FEEDS[$feed])) {
        fwrite(STDERR, "Feed sconosciuto. Disponibili: " . implode(', ', array_keys(GTFS_STATIC_FEEDS)) . "\n");
        exit(1);
    }
    @mkdir(__DIR__ . '/data');
    $t = microtime(true);
    gtfs_static_build($feed, $argv[3] ?? GTFS_STATIC_FEEDS[$feed], gtfs_static_paths($feed)[1]);
    printf("%s: %.1f MB in %.0f s\n", $feed, filesize(gtfs_static_paths($feed)[1]) / 1e6, microtime(true) - $t);
}
