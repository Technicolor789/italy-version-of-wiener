<?php
// Proxy GTFS-RT autonomo: scarica un feed .pb, mette in cache, filtra per fermata.
// ?feed=NOME                       feed completo (.pb)
// ?feed=NOME&stops=ID1,ID2         solo le corse che passano da quelle fermate (.pb)
// ?feed=NOME&stops=ID&format=json  come sopra ma in JSON leggibile (per debug)
// ?feed=NOME&sample=1              elenco di stop_id presenti nel feed
// ?feed=NOME&stops=PLT&like=1      cerca gli stop_id che contengono il testo

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
header('Access-Control-Max-Age: 86400');

if (($_SERVER['REQUEST_METHOD'] ?? '') === 'OPTIONS') {
    http_response_code(204);
    exit;
}

$FEEDS = [
    'toscana'                => 'https://regionetoscana.smartregion.toscana.it/mobility/artifacts/gtfs-rt/trip-updates',
    'atac_roma'              => 'https://romamobilita.it/sites/default/files/rome_rtgtfs_trip_updates_feed.pb',
    'actv_veneto'            => 'https://tpl.actv.it/aut/GTFSRT/tripUpdates',
    'busitalia_veneto'       => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV/start-gtfs-rt-trip-updates-fc.pb',
    'busitalia_tram'         => 'https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV-TRAM/gtfs-rt-trip-updates.pb',
    'autoguidovie_lombardia' => 'https://proxy.busone.app/autoguidovie/gtfs-rt/trip-updates.pb',
    'trenitalia_nazionale'   => 'https://proxy.busone.app/trenitalia/gtfs-rt/trip-updates.pb',
];

const CACHE_TTL = 15;

function fail(int $code, array $data): void
{
    header('Content-Type: application/json; charset=utf-8');
    http_response_code($code);
    echo json_encode($data, JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
    exit;
}

// ---------- Lettura protobuf (wire format) ----------

function pb_varint(string $b, int &$p): int
{
    $r = 0;
    $s = 0;
    $n = strlen($b);
    while ($p < $n) {
        $c = ord($b[$p++]);
        $r |= ($c & 0x7f) << $s;
        if (!($c & 0x80)) {
            return $r;
        }
        $s += 7;
        if ($s > 63) {
            break;
        }
    }
    return $r;
}

function pb_fields(string $b): array
{
    $out = [];
    $p = 0;
    $n = strlen($b);
    while ($p < $n) {
        $start = $p;
        $tag = pb_varint($b, $p);
        $f = $tag >> 3;
        $w = $tag & 7;
        $v = null;
        $d = null;
        if ($w === 0) {
            $v = pb_varint($b, $p);
        } elseif ($w === 1) {
            $d = substr($b, $p, 8);
            $p += 8;
        } elseif ($w === 2) {
            $len = pb_varint($b, $p);
            $d = substr($b, $p, $len);
            $p += $len;
        } elseif ($w === 5) {
            $d = substr($b, $p, 4);
            $p += 4;
        } else {
            throw new RuntimeException('Wire type non valido: ' . $w);
        }
        if ($p > $n || $f < 1) {
            throw new RuntimeException('Dati protobuf troncati');
        }
        $out[] = ['f' => $f, 'w' => $w, 'v' => $v, 'd' => $d, 'raw' => substr($b, $start, $p - $start)];
    }
    return $out;
}

function pb_first(array $fields, int $f)
{
    foreach ($fields as $x) {
        if ($x['f'] === $f) {
            return $x;
        }
    }
    return null;
}

// Restituisce [header_raw, [entity ...]]; ogni entity ha raw + trip_update decodificato
function parse_feed(string $body): array
{
    $header = '';
    $entities = [];
    foreach (pb_fields($body) as $top) {
        if ($top['f'] === 1 && $top['w'] === 2) {
            $header = $top['raw'];
        } elseif ($top['f'] === 2 && $top['w'] === 2) {
            $ef = pb_fields($top['d']);
            $id = '';
            $tu = null;
            foreach ($ef as $x) {
                if ($x['f'] === 1 && $x['w'] === 2) {
                    $id = $x['d'];
                } elseif ($x['f'] === 3 && $x['w'] === 2) {
                    $tu = pb_fields($x['d']);
                }
            }
            $entities[] = ['raw' => $top['raw'], 'id' => $id, 'tu' => $tu];
        }
    }
    return [$header, $entities];
}

function entity_stop_ids(?array $tu): array
{
    $ids = [];
    if ($tu === null) {
        return $ids;
    }
    foreach ($tu as $x) {
        if ($x['f'] === 2 && $x['w'] === 2) {
            foreach (pb_fields($x['d']) as $s) {
                if ($s['f'] === 4 && $s['w'] === 2) {
                    $ids[] = $s['d'];
                }
            }
        }
    }
    return $ids;
}

function stop_matches(string $stopId, array $wanted, bool $like): bool
{
    foreach ($wanted as $w) {
        if ($like ? stripos($stopId, $w) !== false : $stopId === $w) {
            return true;
        }
    }
    return false;
}

function filter_feed(string $body, array $wanted, bool $like): string
{
    [$header, $entities] = parse_feed($body);
    $out = $header;
    foreach ($entities as $e) {
        foreach (entity_stop_ids($e['tu']) as $sid) {
            if (stop_matches($sid, $wanted, $like)) {
                $out .= $e['raw'];
                break;
            }
        }
    }
    return $out;
}

function decode_event(string $data): array
{
    $r = ['delay' => null, 'time' => null];
    foreach (pb_fields($data) as $x) {
        if ($x['f'] === 1 && $x['w'] === 0) {
            $r['delay'] = $x['v'];
        } elseif ($x['f'] === 2 && $x['w'] === 0) {
            $r['time'] = $x['v'];
        }
    }
    return $r;
}

function feed_to_json(string $body, array $wanted, bool $like): array
{
    [, $entities] = parse_feed($body);
    $res = [];
    foreach ($entities as $e) {
        if ($e['tu'] === null) {
            continue;
        }
        $trip = ['trip_id' => null, 'route_id' => null, 'start_time' => null];
        $stops = [];
        foreach ($e['tu'] as $x) {
            if ($x['f'] === 1 && $x['w'] === 2) {
                foreach (pb_fields($x['d']) as $t) {
                    if ($t['w'] !== 2) {
                        continue;
                    }
                    if ($t['f'] === 1) {
                        $trip['trip_id'] = $t['d'];
                    } elseif ($t['f'] === 2) {
                        $trip['start_time'] = $t['d'];
                    } elseif ($t['f'] === 5) {
                        $trip['route_id'] = $t['d'];
                    }
                }
            } elseif ($x['f'] === 2 && $x['w'] === 2) {
                $sf = pb_fields($x['d']);
                $sid = pb_first($sf, 4);
                if (!$sid || !stop_matches($sid['d'], $wanted, $like)) {
                    continue;
                }
                $arr = pb_first($sf, 2);
                $dep = pb_first($sf, 3);
                $a = $arr ? decode_event($arr['d']) : ['delay' => null, 'time' => null];
                $d = $dep ? decode_event($dep['d']) : ['delay' => null, 'time' => null];
                $when = $a['time'] ?? $d['time'];
                $stops[] = [
                    'stop_id' => $sid['d'],
                    'arrival_time' => $a['time'],
                    'departure_time' => $d['time'],
                    'delay' => $a['delay'] ?? $d['delay'],
                    'ora' => $when ? date('H:i', $when) : null,
                    'minuti' => $when ? (int) ceil(($when - time()) / 60) : null,
                ];
            }
        }
        if ($stops) {
            $res[] = ['trip' => $trip, 'stops' => $stops];
        }
    }
    return $res;
}

// ---------- Scelta del feed ----------

$feed = strtolower($_GET['feed'] ?? '');
if ($feed === '' && !empty($_SERVER['PATH_INFO'])) {
    $segments = explode('/', trim($_SERVER['PATH_INFO'], '/'));
    $feed = strtolower(end($segments));
}

if ($feed === '') {
    fail(400, [
        'error' => 'Missing feed parameter',
        'usage' => '?feed=autoguidovie_lombardia&stops=ID',
        'available' => array_keys($FEEDS),
    ]);
}

$remote_url = $FEEDS[$feed] ?? null;
if (!$remote_url) {
    fail(404, ['error' => 'Unknown feed', 'requested' => $feed, 'available' => array_keys($FEEDS)]);
}

// ---------- Download con cache ----------

$cache_file = sys_get_temp_dir() . '/gtfsrt_' . preg_replace('/[^a-z0-9_]/', '', $feed) . '.pb';
$body = false;
$http_code = 200;
$curl_error = '';

if (is_file($cache_file) && time() - filemtime($cache_file) < CACHE_TTL) {
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
        CURLOPT_ENCODING => '',
        CURLOPT_USERAGENT => 'GTFS-RT-Proxy/2.0',
    ]);
    $auth = getenv('FEED_AUTH_' . strtoupper($feed));
    if ($auth) {
        curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_setopt($ch, CURLOPT_USERPWD, $auth);
    }
    $body = curl_exec($ch);
    $http_code = curl_getinfo($ch, CURLINFO_HTTP_CODE);
    $curl_error = curl_error($ch);
    curl_close($ch);

    if ($body !== false && $http_code === 200 && $body !== '') {
        file_put_contents($cache_file, $body, LOCK_EX);
    } elseif (is_file($cache_file)) {
        $body = file_get_contents($cache_file); // se il feed è giù, uso la copia vecchia
        $http_code = 200;
    } else {
        $body = false;
    }
}

if ($body === false || $http_code !== 200) {
    fail(502, [
        'error' => 'Failed to fetch feed',
        'http_code' => $http_code,
        'curl_error' => $curl_error,
        'remote_url' => $remote_url,
    ]);
}

// ---------- Elaborazione ----------

$wanted = array_values(array_filter(array_map('trim', explode(',', $_GET['stops'] ?? '')), 'strlen'));
$like = isset($_GET['like']);

try {
    if (isset($_GET['sample'])) {
        [, $entities] = parse_feed($body);
        $ids = [];
        foreach ($entities as $e) {
            foreach (entity_stop_ids($e['tu']) as $sid) {
                $ids[$sid] = true;
                if (count($ids) >= 40) {
                    break 2;
                }
            }
        }
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode([
            'feed' => $feed,
            'entities' => count($entities),
            'stop_ids_esempio' => array_keys($ids),
        ], JSON_PRETTY_PRINT);
        exit;
    }

    if ($wanted && ($_GET['format'] ?? '') === 'json') {
        header('Content-Type: application/json; charset=utf-8');
        header('Cache-Control: no-cache');
        echo json_encode([
            'feed' => $feed,
            'aggiornato' => date('H:i:s'),
            'corse' => feed_to_json($body, $wanted, $like),
        ], JSON_UNESCAPED_UNICODE | JSON_PRETTY_PRINT);
        exit;
    }

    if ($wanted) {
        $body = filter_feed($body, $wanted, $like);
    }
} catch (Throwable $e) {
    fail(502, ['error' => 'Invalid feed: ' . $e->getMessage()]);
}

header('Content-Type: application/x-protobuf');
header('Cache-Control: no-cache, no-store, must-revalidate');
header('Pragma: no-cache');
header('Expires: 0');
header('Content-Length: ' . strlen($body));
echo $body;
