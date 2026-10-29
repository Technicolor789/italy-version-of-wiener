#!/usr/bin/env python3
"""Writes a synthetic GTFS-RT TripUpdates feed used by test_gtfsrt.sh.

Usage: make_test_feed.py OUT.pb NOW_EPOCH
Needs: pip install gtfs-realtime-bindings
"""
import sys
from google.transit import gtfs_realtime_pb2 as g

out, now = sys.argv[1], int(sys.argv[2])
feed = g.FeedMessage()
feed.header.gtfs_realtime_version = "2.0"
feed.header.timestamp = now

def trip(eid, route, stops, headsign=None, label=None, alert=False):
    e = feed.entity.add()
    e.id = eid
    tu = e.trip_update
    tu.trip.trip_id = "T" + eid
    tu.trip.route_id = route
    if headsign:
        tu.trip_properties.trip_headsign = headsign
    if label:
        tu.vehicle.label = label
    for stop_id, kind, value, delay in stops:
        s = tu.stop_time_update.add()
        s.stop_id = stop_id
        ev = s.departure if kind == "dep" else s.arrival
        if value is not None:
            ev.time = now + value
        if delay is not None:
            ev.delay = delay
        if kind == "skip":
            s.schedule_relationship = 1
    if alert:
        feed.entity.add().id = eid + "-noise"

trip("1", "64", [("100", "dep", 300, 120), ("70017", "dep", 420, -60)], headsign="San Pietro")
trip("2", "H", [("70017", "arr", 60, 0)], label="Capolinea Termini")
trip("3", "8", [("70017", "dep", 7200, None)])             # inside the horizon
trip("4", "8", [("70017", "dep", 5 * 3600, None)])          # beyond the horizon
trip("5", "910", [("99999", "dep", 100, 0)])                # other stop
trip("6", "C3", [("70018", "dep", 900, 300)], alert=True)   # second stop id
trip("7", "40", [("70017", "skip", 200, None)])
trip("8", "19", [("70017", "dep", -600, None)])             # already gone
trip("9", "MEA", [("70017", None, None, 30)])               # delay only: dropped

with open(out, "wb") as f:
    f.write(feed.SerializeToString())
