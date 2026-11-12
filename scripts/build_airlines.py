#!/usr/bin/env python3
"""Regenerate flight_airlines.h from Virtual Radar Server's airline list.

--logos: refresh each airline's logo source and color.
"""

import colorsys
import csv
import io
import json
import math
import re
import struct
import sys
import time
import unicodedata
import urllib.error
import urllib.request
import zlib
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "components" / "gameday" / "flight_airlines.h"
RAW = "https://raw.githubusercontent.com/vradarserver/standing-data/main"
TREE = "https://api.github.com/repos/vradarserver/standing-data/git/trees/main?recursive=1"
TOP = 300
NAME_MAX = 16

GSTATIC = "https://www.gstatic.com/flights/airline_logos/32px/{iata}.png"
FLIGHTAWARE = "https://www.flightaware.com/images/airline_logos/90p/{icao}.png"
# The panel's hints: which source it asks first.
LOGO_UNKNOWN, LOGO_GOOGLE, LOGO_FLIGHTAWARE, LOGO_NONE = 0, 1, 2, 3
LOGO_PX = 16
DECODE_PX = 32
# Google's 405-byte gray tail for an unknown airline.
GSTATIC_BLANK_KEY = 0x8CF55AA4

# Colors the logo rule gets wrong, as (r, g, b).
COLOR_OVERRIDES = {}

# Operators with no published routes that the panel sees often.
EXTRAS = [
    "EJA", "LXJ", "EJM", "VJT", "CNS", "DPJ", "EDG", "XSR", "JTZ", "TWY", "GPD", "KFS",
    "FDX", "UPS", "GTI", "ABX", "ATN", "CKS", "PAC", "SOO", "WGN", "NCR", "CAP", "RCH",
    "SAM", "CMB", "CNV", "PAT", "CGX", "BOE", "AMF", "JUS", "MDS", "FFL",
]

# Names the rules below get wrong or leave too long.
OVERRIDES = {
    "AAL": "American",
    "ACA": "Air Canada",
    "AFR": "Air France",
    "AMX": "Aeromexico",
    "ANA": "ANA",
    "ASA": "Alaska",
    "AUA": "Austrian",
    "BAW": "British Airways",
    "BEL": "Brussels",
    "CAL": "China Airlines",
    "CCA": "Air China",
    "CES": "China Eastern",
    "CPA": "Cathay Pacific",
    "CSN": "China Southern",
    "DLH": "Lufthansa",
    "EIN": "Aer Lingus",
    "EZY": "easyJet",
    "EJU": "easyJet Europe",
    "EZS": "easyJet Swiss",
    "FDX": "FedEx",
    "GTI": "Atlas Air",
    "IBE": "Iberia",
    "JAL": "Japan Airlines",
    "JZA": "Jazz",
    "KAL": "Korean Air",
    "KLM": "KLM",
    "LOT": "LOT",
    "LPE": "LATAM Peru",
    "NAX": "Norwegian",
    "NAY": "Naysa",
    "QFA": "Qantas",
    "RCH": "US Air Force",
    "SAM": "US Air Force",
    "CMB": "US Transcom",
    "CNV": "US Navy",
    "PAT": "US Army",
    "CGX": "Coast Guard",
    "SAS": "SAS",
    "SWR": "Swiss",
    "TAP": "TAP Portugal",
    "THY": "Turkish",
    "UAE": "Emirates",
    "UPS": "UPS",
    "VIR": "Virgin Atlantic",
    "VJT": "VistaJet",
    "WZZ": "Wizz Air",
    "AWE": "US Airways",
    "AXB": "AI Express",
    "AZU": "Azul",
    "GLO": "Gol",
    "TAM": "LATAM Brasil",
    "VIV": "Viva Aerobus",
    "TIB": "TRIP",
    "TAI": "Avianca",
    "ELY": "El Al",
    "AIZ": "Arkia",
    "BCS": "DHL",
    "DHK": "DHL",
    "CSA": "Czech Airlines",
    "AWQ": "AirAsia ID",
    "APG": "AirAsia PH",
    "AIQ": "Thai AirAsia",
    "BKP": "Bangkok Airways",
    "CSS": "SF Airlines",
    "SKU": "Sky Airline",
    "OKA": "Okay Airways",
    "EXS": "Jet2",
    "ARE": "LATAM Colombia",
    "LAN": "LATAM Chile",
    "GIA": "Garuda",
    "SLI": "Aeromexico Conn",
    "CPZ": "Compass",
    "BGA": "Airbus Beluga",
    "BLF": "Blue1",
    "CND": "Corendon",
    "CXI": "Corendon",
    "CAI": "Corendon",
    "IBB": "Binter",
}

DROP = {
    "airlines", "air lines", "airline", "airways", "aviation", "international", "inc", "ltd",
    "limited", "llc", "s.a.", "sa", "company", "co", "corporation", "corp", "group", "plc",
}


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def fetch(url):
    """(status, body); status 0 when nothing answered."""
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.0", "Accept": "image/png,*/*;q=0.8"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, b""
    except (urllib.error.URLError, OSError):
        return 0, b""


# ---- PNG, as the panel decodes it ------------------------------------------------


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def png_color_type(data):
    if len(data) < 26 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return -1
    return data[25]


def read_png(data):
    """Decoded (w, h, pixels) for 8-bit and paletted PNGs."""
    if png_color_type(data) < 0:
        return None
    w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if interlace or ctype not in (0, 2, 3, 4, 6) or w < 1 or h < 1 or w * h > 1 << 20:
        return None
    if depth != 8 and not (ctype in (0, 3) and depth in (1, 2, 4)):
        return None
    at, idat, plte, trns = 8, b"", b"", b""
    while at + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b"IDAT":
            idat += body
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        at += 12 + n
    try:
        raw = zlib.decompress(idat)
    except zlib.error:
        return None
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)
    row_bytes = (w * chans * depth + 7) // 8
    if len(raw) < h * (row_bytes + 1):
        return None
    out, prev = [], bytearray(row_bytes)
    for y in range(h):
        f = raw[y * (row_bytes + 1)]
        line = bytearray(raw[y * (row_bytes + 1) + 1:(y + 1) * (row_bytes + 1)])
        for i in range(row_bytes):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
            elif f != 0:
                return None
        prev = line
        for x in range(w):
            if depth < 8:
                per = 8 // depth
                v = (line[x // per] >> (8 - depth * (x % per + 1))) & ((1 << depth) - 1)
            else:
                v = line[x * chans]
            if ctype == 3:
                if 3 * v + 2 >= len(plte):
                    return None
                r, g, b = plte[3 * v:3 * v + 3]
                out.append((r, g, b, trns[v] if v < len(trns) else 255))
            elif ctype == 0:
                gray = v * 255 // ((1 << depth) - 1)
                clear = len(trns) >= 2 and struct.unpack(">H", trns[:2])[0] == v
                out.append((gray, gray, gray, 0 if clear else 255))
            elif ctype == 4:
                out.append((v, v, v, line[x * 2 + 1]))
            elif ctype == 2:
                r, g, b = line[x * 3:x * 3 + 3]
                clear = len(trns) >= 6 and struct.unpack(">HHH", trns[:6]) == (r, g, b)
                out.append((r, g, b, 0 if clear else 255))
            else:
                out.append(tuple(line[x * 4:x * 4 + 4]))
    return w, h, out


def q565(r, g, b):
    return (r * 31 // 255) * 255 // 31, (g * 63 // 255) * 255 // 63, (b * 31 // 255) * 255 // 31


def decode_fit(w, h, px, box=DECODE_PX):
    """ESPHome's runtime_image: aspect kept, the last source pixel wins."""
    s = min(box / w, box / h)
    tw, th = int(w * s), int(h * s)
    xs, ys = tw / w, th / h
    out = [(0, 0, 0, 0)] * (tw * th)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[y * w + x]
            r, g, b = q565(r, g, b)
            for j in range(int(y * ys), min(th, math.ceil((y + 1) * ys))):
                for i in range(int(x * xs), min(tw, math.ceil((x + 1) * xs))):
                    out[j * tw + i] = (r, g, b, a)
    return tw, th, out


def square(w, h, px):
    s = max(w, h)
    out = [(0, 0, 0, 0)] * (s * s)
    ox, oy = (s - w) // 2, (s - h) // 2
    for y in range(h):
        for x in range(w):
            out[(y + oy) * s + x + ox] = px[y * w + x]
    return s, out


def knockout(s, px):
    """White joined to the edge goes clear; returns how many."""
    def white(p):
        return p[3] >= 128 and min(p[:3]) >= 0xE0
    stack, cleared = [], 0
    for i in [x for x in range(s)] + [(s - 1) * s + x for x in range(s)] + [y * s for y in range(s)] + \
            [y * s + s - 1 for y in range(s)]:
        stack.append(i)
    while stack:
        i = stack.pop()
        if not white(px[i]):
            continue
        px[i] = px[i][:3] + (0,)
        cleared += 1
        x, y = i % s, i // s
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < s and 0 <= ny < s:
                stack.append(ny * s + nx)
    return cleared


def shrink(s, px, out_px=LOGO_PX):
    out = [(0, 0, 0, 0)] * (out_px * out_px)
    for dy in range(out_px):
        for dx in range(out_px):
            x0 = dx * s // out_px
            x1 = max(x0 + 1, (dx + 1) * s // out_px)
            y0 = dy * s // out_px
            y1 = max(y0 + 1, (dy + 1) * s // out_px)
            sr = sg = sb = sa = cnt = 0
            for y in range(y0, min(y1, s)):
                for x in range(x0, min(x1, s)):
                    r, g, b, a = px[y * s + x]
                    sr += r * a
                    sg += g * a
                    sb += b * a
                    sa += a
                    cnt += 1
            if sa and cnt:
                out[dy * out_px + dx] = q565(sr // sa, sg // sa, sb // sa) + (sa // cnt,)
    return out


def lift(n_px, px):
    """logo_icon_lift: dark marks flipped light, dim tiles brightened."""
    n = len(px)
    bright = total = 0
    for r, g, b, a in px:
        total += a
        if max(r, g, b) >= 96:
            bright += a
    if total == 0:
        return px
    opaque = sorted(max(p[:3]) for p in px if p[3] >= 128)
    top = opaque[min(len(opaque) - 1, len(opaque) * 3 // 4)] if opaque else 0
    mark = total * 100 < n * 255 * 85
    flip = mark and bright * 100 < total * 12
    if not flip and (top >= 160 or top < 8):
        return px
    out = []
    for r, g, b, a in px:
        if flip:
            y = (r * 77 + g * 150 + b * 29) >> 8
            up = 255 - 2 * y if y < 128 else 0
            r, g, b = min(255, r + up), min(255, g + up), min(255, b + up)
        else:
            r, g, b = min(255, r * 230 // top), min(255, g * 230 // top), min(255, b * 230 // top)
        out.append(q565(r, g, b) + (a,))
    return out


def bright(px):
    return sum(1 for r, g, b, a in px or () if a >= 128 and max(r, g, b) >= 0xA0)


def usable(px, n_px=LOGO_PX):
    lit = [(i % n_px, i // n_px, max(r, g, b)) for i, (r, g, b, a) in enumerate(px)
           if a >= 128 and max(r, g, b) >= 0x40]
    if len(lit) < n_px * n_px * 5 // 64:
        return False
    xs, ys = [p[0] for p in lit], [p[1] for p in lit]
    mid = sum(1 for p in lit if p[2] >= 0x80)
    light = bright(px) >= n_px * n_px // 16 or len(lit) - mid <= len(lit) // 2
    return max(xs) - min(xs) + 1 >= n_px * 3 // 8 and max(ys) - min(ys) + 1 >= n_px // 4 and light


def accent(px):
    """The commonest saturated hue, at least 0xA0 bright."""
    bins = {}
    for r, g, b, a in px:
        if a < 128 or max(r, g, b) < 40 or max(r, g, b) - min(r, g, b) < 48:
            continue
        k = int(colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)[0] * 12) % 12
        e = bins.setdefault(k, [0, 0, 0, 0])
        e[0] += r * a
        e[1] += g * a
        e[2] += b * a
        e[3] += a
    if not bins:
        return None
    e = bins[max(bins, key=lambda k: bins[k][3])]
    r, g, b = e[0] // e[3], e[1] // e[3], e[2] // e[3]
    m = max(r, g, b)
    if m < 0xA0:
        r, g, b = (min(255, v * 0xA0 // m) for v in (r, g, b))
    return r, g, b


def placeholder(url, body):
    return url.startswith(GSTATIC.split("{")[0]) and (fnv1a(body) == GSTATIC_BLANK_KEY or png_color_type(body) == 4)


def panel_logo(url, status, body):
    """The panel's 16-px logo: (verdict, pixels, color before lifting)."""
    if status == 404 or (status == 200 and placeholder(url, body)):
        return "missing", None, None
    if status != 200:
        return "unknown", None, None
    png = read_png(body)
    if png is None:
        return "unknown", None, None
    w, h, px = decode_fit(*png)
    s, px = square(w, h, px) if w != h else (w, list(px))
    ko = knockout(s, px)
    small = shrink(s, px)
    icon = lift(LOGO_PX, small)
    if not usable(icon):
        return "thin", icon, accent(small)
    return ("tile" if ko * 10 > s * s * 4 else "ok"), icon, accent(small)


def logo_hint(gs, fa):
    """The source to ask first: the clearly brighter readable one."""
    good = ("ok", "tile")
    if gs[0] in good and fa[0] in good:
        if gs[0] == "tile" and fa[0] == "ok":
            return LOGO_FLIGHTAWARE
        return LOGO_FLIGHTAWARE if bright(fa[1]) * 4 > bright(gs[1]) * 5 + 16 else LOGO_GOOGLE
    if gs[0] in good:
        return LOGO_GOOGLE
    if fa[0] in good:
        return LOGO_FLIGHTAWARE
    if "unknown" in (gs[0], fa[0]):
        return LOGO_UNKNOWN
    return LOGO_NONE


def logo_color(hint, gs, fa):
    first = fa if hint == LOGO_FLIGHTAWARE else gs
    for v in (first, fa, gs):
        if v[2] is not None:
            return v[2]
    return 0, 0, 0


def short_name(name):
    s = re.sub(r"\s*\([^)]*\)", "", name).strip()
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().strip()
    words = s.replace(",", " ").split()
    while len(words) > 1:
        two = " ".join(words[-2:]).lower()
        if len(words) > 2 and two in DROP:
            words = words[:-2]
        elif words[-1].lower() in DROP or words[-1].lower().rstrip(".") in DROP:
            words = words[:-1]
        else:
            break
    while len(words) > 1 and len(" ".join(words)) > NAME_MAX:
        words = words[:-1]
    return " ".join(words)[:NAME_MAX].rstrip()


def ranked_codes(tree):
    size = defaultdict(int)
    for e in tree["tree"]:
        m = re.match(r"routes/schema-01/[A-Z0-9]/([A-Z]{3})-[^/]*\.csv$", e["path"])
        if m and e.get("type") == "blob":
            size[m.group(1)] += e.get("size", 0)
    return [c for c, _ in sorted(size.items(), key=lambda kv: (-kv[1], kv[0]))]


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


ROW = re.compile(r'\{"([A-Z]{3})", "([A-Z0-9]{0,2})", "((?:[^"\\]|\\.)*)"(?:, (\d), \{(\d+), (\d+), (\d+)\})?\}')


def read_header():
    """Each airline's row in the current header, by ICAO."""
    out = {}
    if not OUT.exists():
        return out
    for m in ROW.finditer(OUT.read_text(encoding="utf-8")):
        icao, iata, name = m.group(1), m.group(2), m.group(3).replace('\\"', '"').replace("\\\\", "\\")
        hint = int(m.group(4) or 0)
        rgb = tuple(int(m.group(i) or 0) for i in (5, 6, 7))
        out[icao] = (iata, name, hint, rgb)
    return out


def write_header(rows):
    """rows: [(icao, iata, name, hint, (r, g, b))]."""
    lines = [
        "// Generated by scripts/build_airlines.py. Do not edit.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace flights {",
        "",
        "struct AirlineName {",
        "  char icao[4];",
        "  char iata[3];",
        f"  char name[{NAME_MAX + 1}];",
        "  uint8_t logo;    // 0 unknown, 1 Google, 2 FlightAware, 3 none",
        "  uint8_t rgb[3];  // its color; 0, 0, 0 unknown",
        "};",
        "",
        "// Sorted by ICAO prefix, for a binary search.",
        "static const AirlineName kAirlines[] = {",
    ]
    for icao, iata, name, hint, rgb in sorted(rows):
        lines.append(f"    {{{c_str(icao)}, {c_str(iata)}, {c_str(name)}, {hint}, {{{rgb[0]}, {rgb[1]}, {rgb[2]}}}}},")
    lines += ["};", "", "}", ""]
    OUT.write_text("\n".join(lines), encoding="utf-8")


def logos():
    table = read_header()
    if not table:
        raise SystemExit(f"{OUT.relative_to(ROOT)} has no airlines; regenerate the names first")
    rows, counts = [], defaultdict(int)
    for icao in sorted(table):
        iata, name = table[icao][0], table[icao][1]
        gs = ("missing", None, None)
        if iata:
            url = GSTATIC.format(iata=iata)
            gs = panel_logo(url, *fetch(url))
            time.sleep(0.25)
        url = FLIGHTAWARE.format(icao=icao)
        fa = panel_logo(url, *fetch(url))
        time.sleep(0.25)
        hint = logo_hint(gs, fa)
        rgb = COLOR_OVERRIDES.get(icao) or logo_color(hint, gs, fa)
        counts[hint] += 1
        print(f"{icao} {iata or '--'} hint {hint} google {gs[0]} flightaware {fa[0]} color #{rgb[0]:02X}{rgb[1]:02X}{rgb[2]:02X}")
        rows.append((icao, iata, name, hint, rgb))
    write_header(rows)
    print(f"wrote {OUT.relative_to(ROOT)}: {len(rows)} airlines; Google {counts[1]}, FlightAware {counts[2]}, "
          f"none {counts[3]}, unknown {counts[0]}")


def main():
    text = get(f"{RAW}/airlines/schema-01/airlines.csv").decode("utf-8-sig")
    by_icao = {}
    for r in csv.DictReader(io.StringIO(text)):
        icao = r["ICAO"].strip()
        if re.fullmatch(r"[A-Z]{3}", icao) and icao not in by_icao:
            by_icao[icao] = r
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("the repository's file list came back truncated")
    keep = []
    for code in ranked_codes(tree):
        if code in by_icao and code not in keep:
            keep.append(code)
        if len(keep) >= TOP:
            break
    if len(keep) < TOP:
        raise SystemExit(f"only {len(keep)} airlines ranked; the route list looks wrong")
    for code in EXTRAS:
        if code in by_icao and code not in keep:
            keep.append(code)
    old = read_header()
    rows = []
    for code in keep:
        r = by_icao[code]
        iata = r["IATA"].strip()
        if not re.fullmatch(r"[A-Z0-9]{2}", iata):
            iata = ""
        name = OVERRIDES.get(code) or short_name(r["Name"])
        hint, rgb = (old[code][2], old[code][3]) if code in old else (LOGO_UNKNOWN, (0, 0, 0))
        rows.append((code, iata, name, hint, COLOR_OVERRIDES.get(code) or rgb))
    write_header(rows)
    print(f"wrote {OUT.relative_to(ROOT)}: {len(keep)} airlines")


def selftest():
    cases = {
        "Delta Air Lines": "Delta",
        "Southwest Airlines": "Southwest",
        "JetBlue Airways": "JetBlue",
        "Republic Airways": "Republic",
        "Executive Jet Management": "Executive Jet",
        "Air Canada": "Air Canada",
        "Korean Air": "Korean Air",
        "China Eastern Airlines": "China Eastern",
        "Kalitta Air": "Kalitta Air",
        "Air Transport International": "Air Transport",
        "SkyWest Airlines": "SkyWest",
        "Avianca (Aerovias del Continente Americano)": "Avianca",
        "Emirates": "Emirates",
        "Aerolineas Argentinas S.A.": "Aerolineas",
        "Air Alg\u00e9rie": "Air Algerie",
    }
    bad = 0
    for name, want in cases.items():
        got = short_name(name)
        if got != want:
            print(f"short_name({name!r}) = {got!r}, want {want!r}")
            bad += 1
    bad += logo_selftest()
    print("selftest:", "ok" if not bad else f"{bad} failures")
    return 1 if bad else 0


def png_bytes(w, h, px, ctype=6):
    """A test PNG, color type 6 or 4."""
    raw = b""
    for y in range(h):
        raw += b"\x00"
        for r, g, b, a in px[y * w:(y + 1) * w]:
            raw += bytes((r, g, b, a)) if ctype == 6 else bytes((r, a))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def logo_selftest():
    bad = 0

    def check(cond, what):
        nonlocal bad
        if not cond:
            print("FAIL", what)
            bad += 1

    clear, white, red, navy = (0, 0, 0, 0), (255, 255, 255, 255), (220, 20, 40, 255), (0, 0, 90, 255)
    tile = [red if 10 <= x < 22 and 10 <= y < 22 else white for y in range(32) for x in range(32)]
    tile[16 * 32 + 16] = white
    w, h, px = read_png(png_bytes(32, 32, tile))
    check((w, h) == (32, 32) and px == tile, "PNG round trip")
    check(read_png(b"not a png") is None, "not a PNG")
    px = list(tile)
    ko = knockout(32, px)
    check(ko == 32 * 32 - 144 and px[16 * 32 + 16][3] == 255, f"knockout keeps inner white: {ko}")
    v = panel_logo("https://x/a.png", 200, png_bytes(32, 32, tile))
    check(v[0] == "tile" and v[2] is not None, f"a white tile: {v[0]}")
    r, g, b = v[2]
    check(r > g and r > b and max(r, g, b) >= 0xA0, f"its red: {(r, g, b)}")
    mark = [navy if 8 <= x < 24 and 8 <= y < 24 else clear for y in range(32) for x in range(32)]
    v = panel_logo("https://x/a.png", 200, png_bytes(32, 32, mark))
    check(v[0] == "ok" and max(v[1][8 * 16 + 8][:3]) >= 0x80, "a navy mark lifted")
    bar = [red if y in (15, 16) else clear for y in range(32) for x in range(32)]
    check(panel_logo("https://x/a.png", 200, png_bytes(32, 32, bar))[0] == "thin", "a thin bar")
    wide = [red if 2 <= x < 30 and 6 <= y < 14 else clear for y in range(20) for x in range(32)]
    check(panel_logo("https://x/a.png", 200, png_bytes(32, 20, wide))[0] == "ok", "a wide logo squared")
    navy_tile = [white if 14 <= x < 18 and 14 <= y < 18 else navy for y in range(32) for x in range(32)]
    v = panel_logo("https://x/a.png", 200, png_bytes(32, 32, navy_tile))
    check(v[0] == "ok" and v[1][0][2] >= 0xC0 and v[1][7 * 16 + 7][:3] == (255, 255, 255),
          f"a navy tile lifted despite its white: {v[1][0]}")
    dim = [((170,) * 3 if i < 12 else (100,) * 3) + (255,) for i in range(40)]
    icon = [clear] * 256
    for i, p in enumerate(dim):
        icon[(6 + i // 12) * 16 + 2 + i % 12] = p
    check(lift(16, icon) == icon and not usable(icon), "mostly dim, a few bright: unreadable")
    check(usable([(180, 180, 180, 255) if p[3] else p for p in icon]), "the same shape, bright")
    gray = png_bytes(4, 4, [(128, 128, 128, 255)] * 16, ctype=4)
    check(png_color_type(gray) == 4 and read_png(gray) is not None, "gray and alpha")
    check(placeholder(GSTATIC.format(iata="OO"), gray), "Google's placeholder")
    check(not placeholder(FLIGHTAWARE.format(icao="SKW"), gray), "only from Google")
    check(panel_logo("https://x", 404, b"")[0] == "missing" and panel_logo("https://x", 503, b"")[0] == "unknown",
          "statuses")
    ok, thin, miss, unk, tile_v = (("ok", None, None), ("thin", None, None), ("missing", None, None),
                                   ("unknown", None, None), ("tile", None, None))
    check(logo_hint(ok, ok) == LOGO_GOOGLE and logo_hint(tile_v, ok) == LOGO_FLIGHTAWARE, "Google first")
    check(logo_hint(miss, ok) == LOGO_FLIGHTAWARE and logo_hint(thin, miss) == LOGO_NONE, "FlightAware, none")
    check(logo_hint(unk, thin) == LOGO_UNKNOWN and logo_hint(tile_v, thin) == LOGO_GOOGLE, "unknown, tile")
    lit = lambda k: ("ok", [(255, 255, 255, 255)] * k + [clear] * (256 - k), None)
    check(logo_hint(lit(40), lit(60)) == LOGO_FLIGHTAWARE and logo_hint(lit(40), lit(50)) == LOGO_GOOGLE,
          "FlightAware only when clearly brighter")
    check(logo_color(LOGO_FLIGHTAWARE, ("ok", None, (1, 2, 3)), ("ok", None, (4, 5, 6))) == (4, 5, 6), "its color")
    return bad


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    if "--logos" in sys.argv:
        logos()
    else:
        main()
