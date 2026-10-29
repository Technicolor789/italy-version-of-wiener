#!/bin/sh
# Builds and runs the host test of the GTFS-RT parser and the proxy filter.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
now=1764590400  # 1 dicembre 2025, 12:00
python3 make_test_feed.py "$tmp/feed.pb" $now
g++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined test_gtfsrt.cpp -o "$tmp/test"
"$tmp/test" "$tmp/feed.pb" $now
if command -v php >/dev/null; then
  php test_proxy.php "$tmp/feed.pb"
fi
rm -rf "$tmp"
