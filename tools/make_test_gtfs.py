#!/usr/bin/env python3
"""Writes a tiny static GTFS zip for test_static.php. Usage: make_test_gtfs.py OUT.zip"""
import sys, zipfile
files = {
'routes.txt': '﻿route_id,agency_id,route_short_name,route_long_name,route_type\nR1,TN,S5,Varese - Treviglio,2\nR2,TN,,Milano Cadorna - Malpensa,2\n',
'trips.txt': 'route_id,service_id,trip_id,trip_headsign\nR1,FER,T1,Treviglio\nR1,FER,T2,\nR2,FEST,T3,Malpensa Aeroporto\nR1,NIGHT,T4,Varese\n',
'stops.txt': 'stop_id,stop_name,stop_lat,stop_lon,parent_station\nS_MI,Milano Porta Garibaldi,45,9,\nS_MI_1,Milano Porta Garibaldi binario 1,45,9,S_MI\nS_VA,Varese,45,8,\nS_TR,Treviglio,45,9,\nS_MXP,Malpensa Aeroporto,45,8,\n',
'stop_times.txt': 'trip_id,arrival_time,departure_time,stop_id,stop_sequence\n'
  'T1,08:00:00,08:00:00,S_VA,1\nT1,08:40:00,08:42:00,S_MI_1,2\nT1,09:20:00,09:20:00,S_TR,3\n'
  'T2,08:30:00,08:30:00,S_VA,1\nT2,09:10:00,09:12:00,S_MI_1,2\nT2,09:50:00,09:50:00,S_TR,3\n'
  'T3,08:50:00,08:50:00,S_MI,1\nT3,09:30:00,09:30:00,S_MXP,2\n'
  'T4,23:50:00,23:50:00,S_TR,1\nT4,24:35:00,24:35:00,S_MI_1,2\nT4,25:20:00,25:20:00,S_VA,3\n',
'calendar.txt': 'service_id,monday,tuesday,wednesday,thursday,friday,saturday,sunday,start_date,end_date\nFER,1,1,1,1,1,0,0,20260101,20271231\nNIGHT,1,1,1,1,1,1,1,20260101,20271231\n',
'calendar_dates.txt': 'service_id,date,exception_type\nFEST,20261201,1\nFER,20261208,2\n',
}
with zipfile.ZipFile(sys.argv[1],'w') as z:
    for n,c in files.items(): z.writestr('gtfs/'+n, c)
