<?php
// Keeps only the GTFS-RT entities that mention one of the given stop_ids.
//
// The feed is not decoded: top-level entities are sliced out of the raw
// protobuf and searched for the encoded stop_id field (tag 0x22 = field 4,
// length-delimited). Concatenating the header with the kept entities is still
// a valid FeedMessage, just a lot smaller, which spares the ESP32 from
// downloading a whole region every 30 seconds.

function gtfs_read_varint(string $buf, int &$pos): int
{
    $result = 0;
    $shift = 0;
    $len = strlen($buf);
    while ($pos < $len) {
        $b = ord($buf[$pos++]);
        $result |= ($b & 0x7f) << $shift;
        if (($b & 0x80) === 0) {
            return $result;
        }
        $shift += 7;
        if ($shift > 63) {
            break;
        }
    }
    throw new RuntimeException('Invalid varint');
}

function gtfs_encode_varint(int $v): string
{
    $out = '';
    while ($v > 0x7f) {
        $out .= chr(($v & 0x7f) | 0x80);
        $v >>= 7;
    }
    return $out . chr($v);
}

function gtfs_filter_stops(string $feed, array $stop_ids): string
{
    $needles = [];
    foreach ($stop_ids as $id) {
        $id = trim($id);
        if ($id !== '') {
            $needles[] = "\x22" . gtfs_encode_varint(strlen($id)) . $id;
        }
    }

    $out = '';
    $pos = 0;
    $len = strlen($feed);
    while ($pos < $len) {
        $start = $pos;
        $tag = gtfs_read_varint($feed, $pos);
        $wire = $tag & 7;
        if ($wire === 2) {
            $size = gtfs_read_varint($feed, $pos);
            $body_start = $pos;
            $pos += $size;
            if ($pos > $len) {
                throw new RuntimeException('Truncated feed');
            }
            $keep = ($tag >> 3) !== 2; // header and unknown fields
            foreach ($needles as $needle) {
                if ($keep) {
                    break;
                }
                $keep = strpos(substr($feed, $body_start, $size), $needle) !== false;
            }
        } elseif ($wire === 0) {
            gtfs_read_varint($feed, $pos);
            $keep = true;
        } else {
            throw new RuntimeException('Unexpected wire type');
        }
        if ($keep) {
            $out .= substr($feed, $start, $pos - $start);
        }
    }
    return $out;
}
