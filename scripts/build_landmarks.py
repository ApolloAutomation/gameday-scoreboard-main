#!/usr/bin/env python3
"""Regenerate docs/site/land/1/, the map landmark files the device page reads."""

import argparse
import base64
import datetime
import hashlib
import http.server
import io
import json
import math
import os
import re
import struct
import sys
import tempfile
import urllib.request
import zipfile
from functools import partial
from pathlib import Path

DESCRIPTION = """\
Regenerate docs/site/land/1/, the map landmark files under the radars.

The device page reads index.json and the 4-degree squares around the home
from the project site, builds the panel's landmark set and posts it. This
script downloads Natural Earth 10m (public domain: coastlines, large lakes,
country and state or province lines, motorways) from one pinned tag and
GeoNames cities500 (CC BY 4.0: places of 500 people and more), and writes
one file per square that holds something, index.json and LICENSE.txt. It
reads raw.githubusercontent.com and download.geonames.org. Run it when
Natural Earth tags a release or yearly for GeoNames, and commit the folder.
A new file format is a new folder (land/2/), never an edit of land/1/.

  --selftest   offline checks on synthetic inputs
  --check      offline checks of the committed files (with --cache, also
               rebuilds from the cached downloads and compares)
  --serve      serves docs/site with CORS for a local device page build
"""

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "site" / "land" / "1"
NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/"
GEONAMES = "https://download.geonames.org/export/dump/cities500.zip"
# Natural Earth never changes at a tag
SOURCES = {
    "ne_10m_coastline": ("10m_physical", "459a4a97c09db19aadf5244026612de9d43748be27f83a360242b99f7fabb3c1",
                         "9ccc214342fe400bf8c7d91d7a5b276b0457b0ada03e8d4be16ac5ba13037f3b"),
    "ne_10m_lakes": ("10m_physical", "11064fa55ed91b56a0d86733643c1a5bfcc137fdf044e83f471978a5b86617e1",
                     "0edaa87f76c2221c986f0796ce0b54b1c418e9032afba46f8e302edc41193495"),
    "ne_10m_admin_0_boundary_lines_land": (
        "10m_cultural", "b6811b9e2f6ed40b31622e6d261210c5e6afded9b7ad71977d3658293f56373f",
        "103d9b00b684b0380d8ded830d24a1406ed28cbb6c1f4dc3df21c50cac615403"),
    "ne_10m_admin_0_boundary_lines_map_units": (
        "10m_cultural", "359129ecf056ced507b231f417b5e2414d85fc891400bea00aa7ad9609018dcb",
        "e44f3476e94836d4b29bf3440aaac9bce009844f431ab081fa7d1e3b406d0a95"),
    "ne_10m_admin_1_states_provinces_lines": (
        "10m_cultural", "c89b8545fdb03a764df9de2efeadf0d6735169d94b48a315b5e233780e073ab5",
        "b524fd1baf59386cd4bca0d6df642ddb02be9cdc4d69f1f1d109885acf21d9ed"),
    "ne_10m_roads": ("10m_cultural", "b609e6b86218bd0822c0bb251749eed7ac6bc01675381a35ae3bab7a916544b2",
                     "b7b8ca994d4f9edd91baa307b242e6ce6e3353d8f706e872be0138c223d70513"),
    "ne_10m_roads_north_america": (
        "10m_cultural", "bb015273c42bf6cd384a7b168c5c3160f5ad5826b87472f6de9e9e798baf50a4",
        "a1e93745ee57cc02787d5220206af5ae1012be9a6c175202952bc995e6f64793"),
}
CREDIT = "Natural Earth; GeoNames (CC BY 4.0)"
Q = 4000  # grid units per degree, the set's own
GRID = 4
GU = GRID * Q
ROW0, ROWS, COL0, COLS = -88, 44, -180, 90
MIN_KM, DP_KM, STEP_KM = 3.0, 0.1, 5.0
BORDER, COAST, ROAD = 0, 1, 2
PLACE_CODES = {"PPL", "PPLA", "PPLA2", "PPLA3", "PPLA4", "PPLC", "PPLG", "PPLS", "PPLF", "PPLL"}
# GeoNames rows known wrong: id, name, lat, lon, why
DROP = {
    4300488: ("Meads", 38.41, -82.71, "Meads KY: 288,649 people, a CDP of about 2,000"),
}
NAME_MAX = 48
NORTH_AMERICA = {"USA", "CAN", "MEX", "BLZ"}
MAX_FOLDER, MAX_TILE, MAX_HOME = 6_000_000, 65_536, 160_000
SPOTS = [("Versailles KY", 38.05, -84.73), ("Denver", 39.74, -104.99), ("Miami", 25.77, -80.19),
         ("Seattle", 47.61, -122.33), ("Toronto", 43.65, -79.38), ("London", 51.51, -0.13),
         ("Sydney", -33.87, 151.21), ("Anchorage", 61.22, -149.9), ("Honolulu", 21.31, -157.86),
         ("Reykjavik", 64.15, -21.94)]
REACH_KM = 250


# ---- Sources ---------------------------------------------------------------

def fetch(url, path):
    req = urllib.request.Request(url, headers={"User-Agent": "GameDayScoreboard/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r, open(str(path) + ".part", "wb") as fh:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            fh.write(chunk)
    os.replace(str(path) + ".part", path)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(cache):
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    for name, (folder, *hashes) in SOURCES.items():
        for ext, want in zip(("shp", "dbf"), hashes):
            path = cache / f"{name}.{ext}"
            if not path.exists():
                print(f"downloading {name}.{ext}")
                fetch(f"{NE}{folder}/{name}.{ext}", path)
            if sha256(path) != want:
                sys.exit(f"{path} is not Natural Earth v5.1.2's file (sha256 differs); delete it and run again")
    path = cache / "cities500.zip"
    if not path.exists():
        print("downloading cities500.zip")
        fetch(GEONAMES, path)
    return cache


def read_shp(data):
    """PolyLine (3) and Polygon (5) records: [(record index, [[(lon, lat)]])]."""
    out, at = [], 100
    while at + 8 <= len(data):
        num, words = struct.unpack(">ii", data[at:at + 8])
        at += 8
        rec = data[at:at + words * 2]
        at += words * 2
        if len(rec) < 4 or struct.unpack("<i", rec[:4])[0] not in (3, 5):
            continue
        nparts, npts = struct.unpack("<ii", rec[36:44])
        parts = list(struct.unpack(f"<{nparts}i", rec[44:44 + 4 * nparts])) + [npts]
        xy = struct.unpack(f"<{2 * npts}d", rec[44 + 4 * nparts:44 + 4 * nparts + 16 * npts])
        rings = [[(xy[2 * i], xy[2 * i + 1]) for i in range(parts[k], parts[k + 1])] for k in range(nparts)]
        out.append((num - 1, rings))
    return out


def read_dbf(data):
    """Rows as dicts with lowercase field names."""
    n, head, size = struct.unpack("<IHH", data[4:12])
    fields, at = [], 32
    while data[at] != 0x0D:
        fields.append((data[at:at + 11].split(b"\0")[0].decode("ascii").lower(), data[at + 16]))
        at += 32
    rows = []
    for r in range(n):
        o = head + r * size + 1
        row = {}
        for name, width in fields:
            row[name] = data[o:o + width].decode("latin-1").strip()
            o += width
        rows.append(row)
    return rows


def num(v, empty):
    try:
        return float(v)
    except ValueError:
        return empty


def keep_road_world(r):
    return (r.get("type") in ("Major Highway", "Beltway", "Bypass") or num(r.get("expressway", ""), 0) == 1) \
        and r.get("sov_a3") not in NORTH_AMERICA


def keep_road_na(r):
    return r.get("type") in ("Freeway", "Tollway") or r.get("class") == "Interstate"


# kind, source, the rows kept
LAYERS = [
    (BORDER, "ne_10m_admin_0_boundary_lines_land", lambda r: True),
    (BORDER, "ne_10m_admin_0_boundary_lines_map_units", lambda r: True),
    (BORDER, "ne_10m_admin_1_states_provinces_lines", lambda r: num(r.get("min_zoom", ""), 99) <= 7),
    (COAST, "ne_10m_coastline", lambda r: True),
    (COAST, "ne_10m_lakes", lambda r: num(r.get("scalerank", ""), 9) <= 4),
    (ROAD, "ne_10m_roads_north_america", keep_road_na),
    (ROAD, "ne_10m_roads", keep_road_world),
]


def load_lines(cache):
    lines = []
    for kind, name, keep in LAYERS:
        rows = read_dbf((Path(cache) / f"{name}.dbf").read_bytes())
        n = 0
        for i, rings in read_shp((Path(cache) / f"{name}.shp").read_bytes()):
            if i < len(rows) and keep(rows[i]):
                for ring in rings:
                    lines.append((kind, ring))
                    n += 1
        print(f"{name}: {n} lines")
    return lines


def load_places(cache):
    """GeoNames rows: (id, asciiname, lat, lon, feature code, population), and the dump's date and hash."""
    path = Path(cache) / "cities500.zip"
    with zipfile.ZipFile(path) as z:
        info = z.getinfo("cities500.txt")
        text = z.read(info).decode("utf-8")
    date = "%04d-%02d-%02d" % info.date_time[:3]
    rows = []
    for line in text.splitlines():
        c = line.split("\t")
        if len(c) < 15:
            continue
        rows.append((int(c[0]), c[2], float(c[4]), float(c[5]), c[7], int(c[14] or 0)))
    return rows, date, sha256(path)


# ---- Lines -----------------------------------------------------------------

def km_frame(pts):
    lat = math.radians(sum(p[1] for p in pts) / len(pts))
    kx = 111.32 * math.cos(lat)
    return [(p[0] * kx, p[1] * 111.32) for p in pts]


def length_km(pts):
    xy = km_frame(pts)
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(xy, xy[1:]))


def simplify(pts, tol=DP_KM):
    """Douglas-Peucker in a local km frame."""
    if len(pts) < 3:
        return list(pts)
    xy = km_frame(pts)
    keep = bytearray(len(pts))
    keep[0] = keep[-1] = 1
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        (ax, ay), (bx, by) = xy[a], xy[b]
        dx, dy = bx - ax, by - ay
        ln = math.hypot(dx, dy)
        far, at = tol, -1
        for i in range(a + 1, b):
            px, py = xy[i]
            d = abs(dx * (ay - py) - (ax - px) * dy) / ln if ln else math.hypot(px - ax, py - ay)
            if d > far:
                far, at = d, i
        if at > 0:
            keep[at] = 1
            stack += [(a, at), (at, b)]
    return [p for p, k in zip(pts, keep) if k]


def densify(pts, step=STEP_KM):
    """Points every step km at most, so a reach cut keeps long straight lines."""
    xy = km_frame(pts)
    out = [pts[0]]
    for (a, p), (axy, pxy) in zip(zip(pts, pts[1:]), zip(xy, xy[1:])):
        n = math.ceil(math.hypot(pxy[0] - axy[0], pxy[1] - axy[1]) / step)
        out += [(a[0] + (p[0] - a[0]) * k / n, a[1] + (p[1] - a[1]) * k / n) for k in range(1, n)]
        out.append(p)
    return out


def quantize(pts):
    out = []
    for lon, lat in pts:
        p = (round(lon * Q), round(lat * Q))
        if not out or out[-1] != p:
            out.append(p)
    return out


def tile_of(p):
    return (math.floor(p[1] / GU), math.floor(p[0] / GU))


def split(q):
    """Grid points cut at every tile edge: [((row, col), points)], the crossing in both pieces."""
    out, cur, ct = [], [q[0]], tile_of(q[0])
    for a, p in zip(q, q[1:]):
        ts = set()
        for axis in (0, 1):
            if a[axis] == p[axis]:
                continue
            lo, hi = sorted((a[axis], p[axis]))
            for e in range(math.floor(lo / GU) + 1, math.floor(hi / GU) + 1):
                ts.add((e * GU - a[axis]) / (p[axis] - a[axis]))
        for t in sorted(ts):
            c = (round(a[0] + (p[0] - a[0]) * t), round(a[1] + (p[1] - a[1]) * t))
            if cur[-1] != c:
                cur.append(c)
            out.append((ct, cur))
            tm = min(1.0, t + 1e-6)
            ct = tile_of((a[0] + (p[0] - a[0]) * tm, a[1] + (p[1] - a[1]) * tm))
            cur = [c]
        if cur[-1] != p:
            cur.append(p)
    out.append((ct, cur))
    return [(k, pts) for k, pts in out if len(pts) > 1]


def in_grid(key):
    r, c = key
    return ROW0 // GRID <= r < ROW0 // GRID + ROWS and COL0 // GRID <= c < COL0 // GRID + COLS


def build_lines(lines, tiles):
    for kind, pts in lines:
        if len(pts) < 2 or length_km(pts) < MIN_KM:
            continue
        q = quantize(densify(simplify(pts)))
        if len(q) < 2:
            continue
        for key, piece in split(q):
            if in_grid(key):
                tiles.setdefault(key, ([], []))[1].append((kind, piece))


# ---- Places ----------------------------------------------------------------

def place_name(ascii_name):
    n = re.sub(r"^City of ", "", ascii_name.strip())
    n = "".join(ch for ch in n if " " <= ch <= "~").strip()
    return n[:NAME_MAX].strip()


def place_class(pop, code):
    if pop >= 250_000:
        return 4
    if pop >= 50_000 or (code in ("PPLC", "PPLA") and pop >= 10_000):
        return 3
    return 2 if pop >= 5_000 else 1


def pop_code(pop):
    return min(255, round(8 * math.log2(max(1, pop))))


def km_between(a, b):
    kx = 111.32 * math.cos(math.radians((a[0] + b[0]) / 2))
    return math.hypot((a[1] - b[1]) * kx, (a[0] - b[0]) * 111.32)


def pick_places(rows):
    """Kept places: (lat, lon, class, population code, name)."""
    cand = sorted((r for r in rows if r[4] in PLACE_CODES and r[0] not in DROP and place_name(r[1])),
                  key=lambda r: (-r[5], r[0]))
    big = [r for r in cand if r[5] >= 1_000_000]
    grid, out = {}, []
    for gid, name, lat, lon, code, pop in cand:
        g = (int(lat * 20), int(lon * 20))
        near = (o for a in (-1, 0, 1) for b in (-1, 0, 1) for o in grid.get((g[0] + a, g[1] + b), ()))
        if any(km_between((lat, lon), o) < 5 for o in near):
            continue
        # A city's districts are not towns of their own
        if any(pop * 10 < b[5] and abs(b[2] - lat) < 0.5
               and km_between((lat, lon), (b[2], b[3])) < 8 * math.sqrt(b[5] / 1e6) for b in big):
            continue
        grid.setdefault(g, []).append((lat, lon))
        out.append((lat, lon, place_class(pop, code), pop_code(pop), place_name(name)))
    return out


def build_places(places, tiles):
    for lat, lon, cls, pc, name in places:
        p = (round(lon * Q), round(lat * Q))
        key = tile_of(p)
        if in_grid(key):
            tiles.setdefault(key, ([], []))[0].append((p, cls, pc, name))


# ---- Files -----------------------------------------------------------------

def varint(v, b):
    while True:
        c = v & 0x7F
        v >>= 7
        if v:
            b.append(c | 0x80)
        else:
            b.append(c)
            return


def zigzag(v):
    return (v << 1) ^ (v >> 63)


def unzigzag(v):
    return (v >> 1) ^ -(v & 1)


def tile_name(key):
    return f"{key[0] * GRID}_{key[1] * GRID}.bin"


def encode_tile(key, places, lines):
    r, c = key
    x0, y0 = c * GU, r * GU
    places = sorted(places, key=lambda p: (-p[2], p[3], p[0]))
    lines = sorted(lines, key=lambda l: (l[0], l[1][0], len(l[1]), l[1]))
    b = bytearray(b"GT\x01\x00")
    b += struct.pack("<hhBBHHH", r * GRID, c * GRID, GRID, 0, len(places), len(lines), 0)
    for (x, y), cls, pc, name in places:
        nb = name.encode("ascii")
        b += struct.pack("<HHBBB", x - x0, y - y0, cls, pc, len(nb)) + nb
    for kind, pts in lines:
        assert len(pts) <= 65535, "a line too long for its count"
        b.append(kind)
        varint(len(pts), b)
        b += struct.pack("<HH", pts[0][0] - x0, pts[0][1] - y0)
        for a, p in zip(pts, pts[1:]):
            varint(zigzag(p[0] - a[0]), b)
            varint(zigzag(p[1] - a[1]), b)
    return bytes(b)


class Bad(Exception):
    pass


def decode_tile(b, name=None):
    """A tile as the device page reads it; Bad on anything the page refuses."""
    if len(b) < 16 or b[:4] != b"GT\x01\x00":
        raise Bad("magic or format")
    lat0, lon0, grid, _, np_, nl, _ = struct.unpack("<hhBBHHH", b[4:16])
    if grid != GRID or (name is not None and name != f"{lat0}_{lon0}.bin"):
        raise Bad("grid or name")
    at, places, lines = 16, [], []

    def need(n):
        if at + n > len(b):
            raise Bad("short")

    def coord(v):
        if v > GU:
            raise Bad("coordinate")
        return v

    def rv():
        nonlocal at
        v = s = 0
        for k in range(3):
            need(1)
            c = b[at]
            at += 1
            v |= (c & 0x7F) << s
            s += 7
            if c < 0x80:
                return v
        raise Bad("varint")

    for _ in range(np_):
        need(7)
        x, y, cls, pc, n = struct.unpack("<HHBBB", b[at:at + 7])
        at += 7
        need(n)
        nm = b[at:at + n]
        at += n
        if not 1 <= cls <= 4 or not 1 <= n <= NAME_MAX or any(ch < 0x20 or ch > 0x7E for ch in nm):
            raise Bad("place")
        places.append((lat0 + coord(y) / Q, lon0 + coord(x) / Q, cls, pc, nm.decode("ascii")))
    for _ in range(nl):
        need(1)
        kind = b[at]
        at += 1
        if kind > 2:
            raise Bad("kind")
        n = rv()
        if n < 2:
            raise Bad("count")
        need(4)
        x, y = struct.unpack("<HH", b[at:at + 4])
        at += 4
        coord(x), coord(y)
        pts = [(x, y)]
        for _ in range(n - 1):
            x += unzigzag(rv())
            y += unzigzag(rv())
            if x < 0 or y < 0:
                raise Bad("coordinate")
            pts.append((coord(x), coord(y)))
        lines.append((kind, [(lat0 + py / Q, lon0 + px / Q) for px, py in pts]))
    if at != len(b):
        raise Bad("bytes left over")
    return lat0, lon0, places, lines


def bitmap(keys):
    bits = bytearray((ROWS * COLS + 7) // 8)
    for r, c in keys:
        i = (r * GRID - ROW0) // GRID * COLS + (c * GRID - COL0) // GRID
        bits[i >> 3] |= 1 << (i & 7)
    return base64.b64encode(bytes(bits)).decode("ascii")


def bitmap_keys(text):
    bits = base64.b64decode(text)
    if len(bits) != (ROWS * COLS + 7) // 8:
        raise Bad("index size")
    keys = set()
    for i in range(ROWS * COLS):
        if bits[i >> 3] >> (i & 7) & 1:
            keys.add(((ROW0 + i // COLS * GRID) // GRID, (COL0 + i % COLS * GRID) // GRID))
    return keys


def home_tiles(lat, lon, km=REACH_KM):
    """The squares meeting the reach box, clamped as the page does."""
    dy = km / 111.32
    dx = km / (111.32 * math.cos(math.radians(lat)))
    s, n = max(-85, lat - dy), min(85, lat + dy)
    w, e = max(-180, lon - dx), min(180, lon + dx)
    return [(r, c) for r in range(math.floor(s / GRID), math.floor(n / GRID) + 1)
            for c in range(math.floor(w / GRID), math.floor(min(e, 179.9999) / GRID) + 1)]


def license_text(date, sha):
    return f"""Map landmark files for the Game Day Scoreboard
==============================================

These files hold the towns, borders, coastlines and highways that the
scoreboard's device page reads when the owner sets the home, to draw them
under Weather's radar and the flight radar. Each .bin file covers one
4 x 4 degree square; index.json lists the squares that have a file.
Built by scripts/build_landmarks.py in the project's repository.

Natural Earth
-------------
Coastlines, large lakes, country and state or province lines, and
motorways from Natural Earth 10m, version 5.1.2 (including the North
America roads layer contributed by XNR Productions).
https://www.naturalearthdata.com
Natural Earth is in the public domain.

GeoNames
--------
Towns and cities from GeoNames' cities500 dump of {date}
(sha256 {sha}).
https://download.geonames.org/export/dump/
(c) GeoNames, licensed under the Creative Commons Attribution 4.0
License, https://creativecommons.org/licenses/by/4.0/
The data is provided "as is" without warranty or any representation of
accuracy, timeliness or completeness.

Changes made: lines were simplified to about 100 m, cut at the squares'
edges, and features shorter than 3 km dropped; places were limited to
populated places (not sections or historical places), their ASCII names
used with "City of " removed, positions rounded to 1/4000 degree, a place
within 5 km of a bigger one and a district of a city of a million or more
dropped, a few rows with known errors dropped, and populations reduced to
a size class and a rounded size.
"""


def build(cache):
    lines = load_lines(cache)
    rows, date, sha = load_places(cache)
    tiles = {}
    build_lines(lines, tiles)
    places = pick_places(rows)
    print(f"places kept: {len(places)} of {len(rows)}")
    build_places(places, tiles)
    files = {tile_name(k): encode_tile(k, *v) for k, v in sorted(tiles.items()) if v[0] or v[1]}
    index = {"format": 1, "grid": GRID, "built": date,
             "sources": {"naturalearth": "v5.1.2", "geonames": f"cities500 {date} sha256:{sha}"},
             "credit": CREDIT, "tiles": bitmap(tile_key(n) for n in files)}
    files["index.json"] = (json.dumps(index, indent=1) + "\n").encode()
    files["LICENSE.txt"] = license_text(date, sha).encode()
    return files


def tile_key(name):
    lat0, lon0 = (int(v) for v in name[:-4].split("_"))
    return (lat0 // GRID, lon0 // GRID)


def sizes(files):
    """Problems with the folder's size, and the lines to print."""
    tiles = {n: len(b) for n, b in files.items() if n.endswith(".bin")}
    total = sum(len(b) for b in files.values())
    big = max(tiles, key=tiles.get)
    bad, say = [], [f"{len(tiles)} tiles, {total:,} B in all, largest {big} {tiles[big]:,} B"]
    if total > MAX_FOLDER:
        bad.append(f"the folder is {total:,} B, over {MAX_FOLDER:,}")
    for n, s in tiles.items():
        if s > MAX_TILE:
            bad.append(f"{n} is {s:,} B, over {MAX_TILE:,}")
    for spot, lat, lon in SPOTS:
        got = [tile_name(k) for k in home_tiles(lat, lon) if tile_name(k) in tiles]
        read = len(files["index.json"]) + sum(tiles[n] for n in got)
        say.append(f"  {spot}: {len(got)} squares, {read:,} B")
        if read > MAX_HOME:
            bad.append(f"{spot} reads {read:,} B, over {MAX_HOME:,}")
    return bad, say


def write(files):
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.iterdir():
        if old.name not in files:
            old.unlink()
    for n, b in files.items():
        p = OUT / n
        if not p.exists() or p.read_bytes() != b:
            p.write_bytes(b)


# ---- Checks ----------------------------------------------------------------

def around(decoded, lat, lon, km):
    places = [p for t in decoded for p in t[2] if km_between((lat, lon), p[:2]) <= km]
    lines = [l for t in decoded for l in t[3] if any(km_between((lat, lon), q) <= km for q in l[1])]
    return places, lines


def check(cache=None):
    bad = []
    files = {p.name: p.read_bytes() for p in OUT.iterdir()} if OUT.is_dir() else {}
    try:
        index = json.loads(files.get("index.json", b"{}"))
        keys = bitmap_keys(index.get("tiles", ""))
    except (ValueError, Bad) as e:
        print(f"index.json is unreadable: {e}", file=sys.stderr)
        return 1
    if index.get("format") != 1 or index.get("grid") != GRID or not index.get("credit") or not index.get("sources"):
        bad.append("index.json lacks format 1, grid 4, credit or sources")
    names = {n for n in files if n.endswith(".bin")}
    if {tile_name(k) for k in keys} != names:
        bad.append("index.json's squares are not the files present")
    if set(files) - names - {"index.json", "LICENSE.txt"}:
        bad.append(f"other files: {sorted(set(files) - names - {'index.json', 'LICENSE.txt'})}")
    lic = files.get("LICENSE.txt", b"").decode("utf-8", "replace")
    if not all(w in lic for w in ("GeoNames", "Creative Commons Attribution 4.0", "Natural Earth")):
        bad.append("LICENSE.txt must name GeoNames, CC BY 4.0 and Natural Earth")
    decoded = {}
    for n in sorted(names):
        try:
            decoded[n] = decode_tile(files[n], n)
        except Bad as e:
            bad.append(f"{n}: {e}")

    def near(lat, lon):
        return [decoded[tile_name(k)] for k in home_tiles(lat, lon) if tile_name(k) in decoded]

    pl, ln = around(near(38.05, -84.73), 38.05, -84.73, 160)
    have = {p[4]: p[2] for p in pl}
    for town in ("Lexington", "Frankfort", "Louisville"):
        if have.get(town, 0) < 3:
            bad.append(f"Versailles: no {town} of class 3 or more")
    if "Versailles" not in have:
        bad.append("Versailles: no Versailles")
    if not any(k == ROAD for k, _ in ln) or not any(k == BORDER for k, _ in ln):
        bad.append("Versailles: no road or no border within 160 km")
    if not any(k == COAST for k, _ in around(near(25.77, -80.19), 25.77, -80.19, 20)[1]):
        bad.append("Miami: no coast within 20 km")
    if not any(k == COAST for k, _ in around(near(43.65, -79.38), 43.65, -79.38, 20)[1]):
        bad.append("Toronto: no lake shore within 20 km")
    _, ln = around(near(51.51, -0.13), 51.51, -0.13, 160)
    if not any(k == ROAD for k, l in ln if any(km_between((51.51, -0.13), q) <= 30 for q in l)):
        bad.append("London: no road within 30 km")
    if sum(k == BORDER for k, _ in ln) > 40:
        bad.append("London: over 40 border lines within 160 km")
    print("dropped rows (DROP):")
    for gid, (name, lat, lon, why) in DROP.items():
        print(f"  {gid}: {why}")
        if any(p[4] == name for p in around(near(lat, lon), lat, lon, 5)[0]):
            bad.append(f"{name} ({gid}) is in the files")
    if "index.json" in files and names:
        more, say = sizes(files)
        bad += more
        print("\n".join(say))
    if cache:
        if build(cache) != files:
            bad.append("the files differ from a rebuild of the cached downloads")
    for b in bad:
        print(f"FAIL: {b}", file=sys.stderr)
    if bad:
        return 1
    print(f"docs/site/land/1 is good: {len(names)} squares")
    return 0


def selftest():
    def pack_shp(recs):
        body = b""
        for i, (typ, parts) in enumerate(recs):
            pts = [p for part in parts for p in part]
            starts, k = [], 0
            for part in parts:
                starts.append(k)
                k += len(part)
            rec = struct.pack("<i4d2i", typ, 0, 0, 0, 0, len(parts), len(pts))
            rec += struct.pack(f"<{len(parts)}i", *starts) + struct.pack(f"<{2 * len(pts)}d", *(v for p in pts for v in p))
            body += struct.pack(">ii", i + 1, len(rec) // 2) + rec
        return b"\0" * 100 + body

    shp = read_shp(pack_shp([(3, [[(0, 0), (1, 1)], [(2, 2), (3, 3), (4, 4)]]), (5, [[(0, 0), (1, 0), (0, 1), (0, 0)]])]))
    assert shp == [(0, [[(0, 0), (1, 1)], [(2, 2), (3, 3), (4, 4)]]), (1, [[(0, 0), (1, 0), (0, 1), (0, 0)]])], shp
    fields = [(b"TYPE", 10), (b"MIN_ZOOM", 4)]
    head = 32 + 32 * len(fields) + 1
    dbf = bytearray(struct.pack("<4xIHH", 2, head, 15)) + b"\0" * 20
    for nm, w in fields:
        dbf += nm.ljust(11, b"\0") + b"N" + b"\0" * 4 + bytes([w]) + b"\0" * 15
    dbf += b"\x0d" + b" " + b"Freeway".ljust(10) + b"   2" + b" " + b"Tollway".ljust(10) + b" 7.0"
    rows = read_dbf(bytes(dbf))
    assert rows == [{"type": "Freeway", "min_zoom": "2"}, {"type": "Tollway", "min_zoom": "7.0"}], rows

    # A corner over 0.1 km stays, a 50 m wiggle goes
    d = 1 / 111.32
    s = simplify([(0, 0), (5 * d, 0.0004), (10 * d, 0), (10 * d, 5 * d)])
    assert s == [(0, 0), (10 * d, 0), (10 * d, 5 * d)], s

    d = densify([(0, 0), (0, 0.2), (0, 0.21)])
    assert len(d) == 7 and d[-1] == (0, 0.21) and abs(d[1][1] - 0.04) < 1e-9, d

    # Through a tile corner: three pieces, shared ends
    pieces = split([(GU - 100, GU - 100), (GU + 100, GU + 100 + 20)])
    assert len(pieces) == 3, pieces
    for (_, a), (_, b) in zip(pieces, pieces[1:]):
        assert a[-1] == b[0]
    assert [k for k, _ in pieces] == [(0, 0), (1, 0), (1, 1)], pieces
    assert split([(0, 0), (5, 5)]) == [((0, 0), [(0, 0), (5, 5)])]

    for v in (0, 1, -1, 63, -63, 64, -64, 8191, -8191, 8192, -8192, 16000, -16000):
        b = bytearray()
        varint(zigzag(v), b)
        got, at, sh = 0, 0, 0
        while True:
            got |= (b[at] & 0x7F) << sh
            sh += 7
            at += 1
            if b[at - 1] < 0x80:
                break
        assert unzigzag(got) == v and at == len(b) <= 3, (v, b)

    # id, ascii name, lat, lon, feature code, population
    town = [(1, "Big City", 40.0, -80.0, "PPL", 2_000_000), (2, "Twin", 40.0, -80.03, "PPL", 9000),
            (3, "District", 40.05, -80.05, "PPL", 20_000), (4, "Section", 41.0, -80.0, "PPLX", 9000),
            (5, "Wrong", 42.0, -80.0, "PPL", 9000), (6, "City of Farview", 43.0, -80.0, "PPL", 4_999),
            (7, "Seat", 44.0, -80.0, "PPLA", 10_000), (8, "Village", 45.0, -80.0, "PPL", 600)]
    DROP[5] = ("Wrong", 42.0, -80.0, "selftest")
    try:
        got = {p[4]: p for p in pick_places(town)}
    finally:
        del DROP[5]
    assert set(got) == {"Big City", "Farview", "Seat", "Village"}, got
    assert [got[n][2] for n in ("Big City", "Farview", "Seat", "Village")] == [4, 1, 3, 1]
    assert [place_class(p, "PPL") for p in (4_999, 5_000, 49_999, 50_000, 250_000)] == [1, 2, 2, 3, 4]
    assert place_class(10_000, "PPLA") == 3 and place_class(9_999, "PPLC") == 2
    assert pop_code(1) == 0 and pop_code(1 << 20) == 160 and pop_code(10 ** 12) == 255

    # A tile written and read back
    key = (9, -22)
    tiles = {}
    build_places([(38.05, -84.73, 3, 100, "Versailles")], tiles)
    build_lines([(ROAD, [(-84.9, 38.0), (-84.5, 38.1), (-84.0, 38.5)])], tiles)
    assert set(tiles) == {key}, tiles
    b = encode_tile(key, *tiles[key])
    lat0, lon0, pl, ln = decode_tile(b, "36_-88.bin")
    pl = [tuple(round(v, 6) if isinstance(v, float) else v for v in p) for p in pl]
    assert (lat0, lon0) == (36, -88) and pl == [(38.05, -84.73, 3, 100, "Versailles")], pl
    ends = [tuple(round(v, 6) for v in q) for q in (ln[0][1][0], ln[0][1][-1])]
    assert ln[0][0] == ROAD and ends == [(38.0, -84.9), (38.5, -84.0)], ln
    for broken in (b[:-1], b + b"\0", b"GT\x02" + b[3:], b[:16] + b"\xff\xff" + b[18:]):
        try:
            decode_tile(broken, "36_-88.bin")
            raise AssertionError("a broken tile read")
        except Bad:
            pass
    try:
        decode_tile(b, "36_-84.bin")
        raise AssertionError("a misnamed tile read")
    except Bad:
        pass

    corners = [(-88 // GRID, -180 // GRID), (0, 0), (84 // GRID, 176 // GRID)]
    text = bitmap(corners)
    bits = base64.b64decode(text)
    assert len(bits) == 495 and bits[0] == 1 and bits[(22 * 90 + 45) >> 3] == 1 << ((22 * 90 + 45) & 7)
    assert bits[-1] == 0x80 and bitmap_keys(text) == set(corners)
    assert tile_name((9, -22)) == "36_-88.bin" and tile_key("36_-88.bin") == (9, -22)
    assert (9, -22) in home_tiles(38.05, -84.73) and len(home_tiles(38.05, -84.73)) == 6
    assert not in_grid((22, 45)) and not in_grid((-23, 0)) and in_grid((21, -45))
    print("build_landmarks selftest: ok")
    return 0


def serve(port):
    class Cors(http.server.SimpleHTTPRequestHandler):
        def end_headers(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            super().end_headers()

    handler = partial(Cors, directory=str(ROOT / "docs" / "site"))
    with http.server.ThreadingHTTPServer(("0.0.0.0", port), handler) as httpd:
        print(f"Serving docs/site at http://<this computer's address>:{port}/")
        print("In the device page's browser console:")
        print(f'  localStorage.setItem("gd.landBase", "http://<this computer\'s address>:{port}/land/1")')
        httpd.serve_forever()


def main():
    ap = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", help="keep and reuse the downloads in this folder")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--serve", nargs="?", type=int, const=8000, metavar="PORT")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.serve:
        serve(a.serve)
        return 0
    if a.check:
        return check(download(a.cache) if a.cache else None)
    with tempfile.TemporaryDirectory() as tmp:
        files = build(download(a.cache or tmp))
    bad, say = sizes(files)
    print("\n".join(say))
    if bad:
        print("\n".join(f"FAIL: {b}" for b in bad), file=sys.stderr)
        return 1
    write(files)
    print(f"wrote {OUT.relative_to(ROOT)} ({datetime.date.today()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
