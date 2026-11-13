#!/usr/bin/env python3
"""NMEA 0183-testbron over TCP, om OpenCPN op iOS te testen zonder boot.

Stuurt elke seconde GPRMC en GPGGA voor een verzonnen schip met instelbare koers en snelheid
(standaard 5 kn naar het oosten; met --keer vaart het heen en weer),
plus af en toe een AIS-bericht (!AIVDM, positierapport klasse A van een verzonnen MMSI).
Niets hiervan komt van of gaat naar een echte boot; de beginpositie is een parameter.

Gebruik:
  python3 -I scripts/tools/nmea-testbron.py --lat 42.81 --lon -70.85 [--poort 10110]
  [--koers 270 --snelheid 3 --keer 600]

De NOAA-testkaart US5MA1AM beslaat lat 42.741..42.853, lon -71.122..-70.856 (Merrimack
River). Een startpunt daarbinnen, bv. --lat 42.818 --lon -70.866 --koers 270, houdt het
schip op de kaart.

OpenCPN: verbinding "Netwerk, TCP", adres van de Mac (simulator: 127.0.0.1), poort 10110.
"""
import argparse
import datetime as dt
import math
import socket
import threading
import time


def checksum(body: str) -> str:
    c = 0
    for ch in body:
        c ^= ord(ch)
    return f"{c:02X}"


def sentence(body: str, start: str = "$") -> bytes:
    return f"{start}{body}*{checksum(body)}\r\n".encode("ascii")


def nmea_lat(lat: float):
    hemi = "N" if lat >= 0 else "S"
    lat = abs(lat)
    d = int(lat)
    return f"{d:02d}{(lat - d) * 60:07.4f}", hemi


def nmea_lon(lon: float):
    hemi = "E" if lon >= 0 else "W"
    lon = abs(lon)
    d = int(lon)
    return f"{d:03d}{(lon - d) * 60:07.4f}", hemi


def ais_payload_class_a(mmsi: int, lat: float, lon: float, sog_kn: float, cog: float) -> str:
    """Positierapport type 1, 168 bits, als 6-bits AIS-ASCII."""
    bits = ""

    def put(value: int, n: int):
        nonlocal bits
        value &= (1 << n) - 1
        bits += format(value, f"0{n}b")

    put(1, 6)          # message type
    put(0, 2)          # repeat
    put(mmsi, 30)
    put(0, 4)          # nav status: under way using engine
    put(-128, 8)       # rate of turn: not available
    put(int(round(sog_kn * 10)), 10)
    put(1, 1)          # position accuracy
    put(int(round(lon * 600000)), 28)
    put(int(round(lat * 600000)), 27)
    put(int(round(cog * 10)), 12)
    put(int(round(cog)) % 360, 9)  # true heading
    put(dt.datetime.now(dt.timezone.utc).second, 6)
    put(0, 2)          # maneuver
    put(0, 3)          # spare
    put(0, 1)          # RAIM
    put(0, 19)         # radio status
    out = ""
    for i in range(0, len(bits), 6):
        v = int(bits[i:i + 6], 2)
        out += chr(v + 48 if v < 40 else v + 56)
    return out


def run_client(conn, addr, args):
    print(f"verbonden: {addr}")
    lat, lon = args.lat, args.lon
    sog, cog = args.snelheid, args.koers
    t = 0
    try:
        while True:
            now = dt.datetime.now(dt.timezone.utc)
            hhmmss = now.strftime("%H%M%S")
            date = now.strftime("%d%m%y")
            la, ns = nmea_lat(lat)
            lo, ew = nmea_lon(lon)
            conn.sendall(sentence(f"GPRMC,{hhmmss}.00,A,{la},{ns},{lo},{ew},{sog:.1f},{cog:.1f},{date},,,A"))
            conn.sendall(sentence(f"GPGGA,{hhmmss}.00,{la},{ns},{lo},{ew},1,08,0.9,5.0,M,-33.0,M,,"))
            if t % 5 == 0:
                # verzonnen AIS-doel 0,5 nm noordelijk, varend naar het zuidwesten
                payload = ais_payload_class_a(244000001, lat + 0.008, lon + 0.004, 7.5, 225.0)
                conn.sendall(sentence(f"AIVDM,1,1,,A,{payload},0", start="!"))
            # verplaatsen: sog in kn, 1 s per stap
            afstand_nm = sog / 3600.0
            lat += afstand_nm * math.cos(math.radians(cog)) / 60.0
            lon += afstand_nm * math.sin(math.radians(cog)) / 60.0 / math.cos(math.radians(lat))
            t += 1
            if args.keer and t % args.keer == 0:
                cog = (cog + 180.0) % 360.0
            time.sleep(1)
    except (BrokenPipeError, ConnectionResetError):
        print(f"verbroken: {addr}")
    finally:
        conn.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--poort", type=int, default=10110)
    ap.add_argument("--koers", type=float, default=90.0, help="graden waar")
    ap.add_argument("--snelheid", type=float, default=5.0, help="knopen")
    ap.add_argument("--keer", type=int, default=0,
                    help="na zoveel seconden 180 graden keren (0 = nooit)")
    ap.add_argument("--adres", default="0.0.0.0")
    args = ap.parse_args()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.adres, args.poort))
    srv.listen(4)
    print(f"NMEA-testbron op {args.adres}:{args.poort}, start {args.lat},{args.lon}")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=run_client, args=(conn, addr, args), daemon=True).start()


if __name__ == "__main__":
    main()
