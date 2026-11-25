#!/usr/bin/env python3
"""Testbron: speelt NMEA 0183 af over TCP op de Mac, voor de simulator (localhost).

Gebruik: scripts/nmea-testbron.py [poort ...]   (standaard 10110 en 10111)

Elke poort is een aparte bron met een eigen talker-ID (GP, GN, II, ...) en een iets andere
positie, zodat OpenCPN ze als verschillende bronnen ziet (Comm prioriteiten). Zendt per
seconde RMC, GGA, HDT en VTG. De positie is een vaste testwaarde op open water, geen
vaargebied of boot. Alleen voor ontwikkelen en testen, niet aan boord.
"""
import datetime
import math
import socket
import sys
import threading
import time

TALKERS = ["GP", "GN", "II", "EC"]


def checksum(body):
    c = 0
    for ch in body:
        c ^= ord(ch)
    return f"{c:02X}"


def sentence(body):
    return f"${body}*{checksum(body)}\r\n"


def latlon(lat, lon):
    la, lo = abs(lat), abs(lon)
    lat_s = f"{int(la):02d}{(la - int(la)) * 60:07.4f},{'N' if lat >= 0 else 'S'}"
    lon_s = f"{int(lo):03d}{(lo - int(lo)) * 60:07.4f},{'E' if lon >= 0 else 'W'}"
    return lat_s, lon_s


def frames(talker, offset, t):
    now = datetime.datetime.now(datetime.timezone.utc)
    hms = now.strftime("%H%M%S.00")
    dmy = now.strftime("%d%m%y")
    # Langzaam rondje: koers draait, snelheid 5 kn.
    lat = 50.0 + offset + 0.001 * math.sin(t / 60)
    lon = 0.0 + offset + 0.001 * math.cos(t / 60)
    cog = (t * 3) % 360
    la, lo = latlon(lat, lon)
    return [
        sentence(f"{talker}RMC,{hms},A,{la},{lo},5.0,{cog:.1f},{dmy},,,A"),
        sentence(f"{talker}GGA,{hms},{la},{lo},1,08,0.9,2.0,M,47.0,M,,"),
        sentence(f"{talker}HDT,{cog:.1f},T"),
        sentence(f"{talker}VTG,{cog:.1f},T,,M,5.0,N,9.3,K,A"),
    ]


def serve(port, talker, offset):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(4)
    print(f"bron {talker} op tcp://127.0.0.1:{port}", flush=True)
    while True:
        conn, addr = srv.accept()
        print(f"{port}: verbonden {addr}", flush=True)
        threading.Thread(target=feed, args=(conn, port, talker, offset), daemon=True).start()


def feed(conn, port, talker, offset):
    t0 = time.time()
    try:
        while True:
            for s in frames(talker, offset, time.time() - t0):
                conn.sendall(s.encode("ascii"))
            time.sleep(1)
    except OSError:
        print(f"{port}: verbinding weg", flush=True)
    finally:
        conn.close()


def main():
    ports = [int(p) for p in sys.argv[1:]] or [10110, 10111]
    for i, port in enumerate(ports):
        threading.Thread(target=serve, args=(port, TALKERS[i % len(TALKERS)], i * 0.0005),
                         daemon=True).start()
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
