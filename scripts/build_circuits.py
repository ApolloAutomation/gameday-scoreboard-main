#!/usr/bin/env python3
"""Regenerate components/gameday/circuits.h from F1DB, Jolpica and ESPN."""

import argparse
import datetime
import json
import math
import re
import sys
import unicodedata
import urllib.request
from pathlib import Path

DESCRIPTION = """\
Regenerate components/gameday/circuits.h, the F1 display's circuits.

For every circuit on the seasons' calendars (F1DB's race files), it keeps
the outline of the layout raced there last (F1DB's white SVG, flattened,
simplified to at most 64 points and scaled into a byte box with its aspect
kept), F1DB's length, turns, laps and coordinates, and the ids the panel
finds it by: Jolpica's circuitId (the nearest Jolpica circuit), ESPN's
circuit id (ESPN's calendar, by race date) and name keys (circuit_key() of
ESPN's, F1DB's and Jolpica's names). It reads the GitHub API for F1DB's race
lists, raw.githubusercontent.com, api.jolpi.ca and ESPN. Run it when the
calendar changes and commit the header. With no --season, last season and
this one.
"""

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "components" / "gameday" / "circuits.h"
F1DB_API = "https://api.github.com/repos/f1db/f1db/contents/src/data/seasons/{season}/races"
F1DB_RAW = "https://raw.githubusercontent.com/f1db/f1db/main/src"
JOLPICA = "https://api.jolpi.ca/ergast/f1/{season}/circuits.json"
ESPN_CAL = "https://site.api.espn.com/apis/site/v2/sports/racing/f1/scoreboard?dates={season}0204"
ESPN_CORE = "https://sports.core.api.espn.com/v2/sports/racing/leagues/f1"
MAX_POINTS = 64
BOX = 255
# Dropped words; kCircuitStop in f1.cpp matches.
STOP = {
    "and", "autodrome", "autodromo", "circuit", "circuito", "course", "da", "de", "del", "di", "do", "e",
    "grand", "internacional", "international", "internazionale", "nazionale", "of", "prix", "race",
    "racing", "street", "the", "y",
}


def get(url, raw=False):
    # ESPN answers curl-like agents; the others take any agent.
    agent = "curl/8.0 gameday-scoreboard" if "espn.com" in url else "GameDayScoreboard/1.0"
    req = urllib.request.Request(url, headers={"User-Agent": agent})
    with urllib.request.urlopen(req, timeout=30) as r:
        body = r.read()
    return body.decode("utf-8") if raw else json.loads(body)


def fold(s):
    return "".join(c for c in unicodedata.normalize("NFKD", s) if not unicodedata.combining(c))


def circuit_key(name):
    words = re.split(r"[^a-z0-9]+", fold(name).lower())
    return " ".join(w for w in words if w and w not in STOP)


def flat_yaml(text):
    """Top-level scalar keys, all F1DB's files need here."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([A-Za-z0-9_]+):\s*(.*?)\s*(#.*)?$", line)
        if m and m.group(2):
            out[m.group(1)] = m.group(2).strip("'\"")
    return out


def yaml_list(text, key):
    """The items of a top-level list of plain strings ("previousNames")."""
    out, inside = [], False
    for line in text.splitlines():
        if re.match(rf"^{key}:\s*$", line):
            inside = True
            continue
        if inside:
            m = re.match(r"^\s+-\s+(.*)$", line)
            if not m:
                break
            out.append(m.group(1).strip("'\""))
    return out


TOKEN = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")


def arc_points(x0, y0, rx, ry, phi, large, sweep, x1, y1, steps=12):
    """An SVG elliptical arc as line ends, via its center."""
    if rx == 0 or ry == 0 or (x0 == x1 and y0 == y1):
        return [(x1, y1)]
    rx, ry = abs(rx), abs(ry)
    c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    xp, yp = c * dx + s * dy, -s * dx + c * dy
    lam = (xp * xp) / (rx * rx) + (yp * yp) / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * yp * yp - ry * ry * xp * xp
    den = rx * rx * yp * yp + ry * ry * xp * xp
    k = math.sqrt(max(0.0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = k * rx * yp / ry, -k * ry * xp / rx
    cx, cy = c * cxp - s * cyp + (x0 + x1) / 2, s * cxp + c * cyp + (y0 + y1) / 2

    def angle(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    t0 = angle(1, 0, (xp - cxp) / rx, (yp - cyp) / ry)
    dt = angle((xp - cxp) / rx, (yp - cyp) / ry, (-xp - cxp) / rx, (-yp - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    pts = []
    for i in range(1, steps + 1):
        t = t0 + dt * i / steps
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        pts.append((c * ex - s * ey + cx, s * ex + c * ey + cy))
    return pts


def path_subpaths(d, steps=10):
    """Each subpath of an SVG path's d attribute, curves flattened."""
    tokens = TOKEN.findall(d)
    i, cmd = 0, None
    x = y = sx = sy = 0.0
    last_ctrl = None
    subs, cur = [], []

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]
            i += 1
            if cmd in "Zz":
                if cur:
                    cur.append((sx, sy))
                    subs.append(cur)
                    cur = []
                x, y = sx, sy
                last_ctrl = None
                continue
        if cmd is None:
            raise ValueError("path data starts without a command")
        rel = cmd.islower()
        c = cmd.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if c == "M":
            if cur:
                subs.append(cur)
            x, y = num() + ox, num() + oy
            sx, sy = x, y
            cur = [(x, y)]
            cmd = "l" if rel else "L"  # later pairs are line ends
            last_ctrl = None
        elif c == "L":
            x, y = num() + ox, num() + oy
            cur.append((x, y))
            last_ctrl = None
        elif c == "H":
            x = num() + ox
            cur.append((x, y))
            last_ctrl = None
        elif c == "V":
            y = num() + oy
            cur.append((x, y))
            last_ctrl = None
        elif c in "CS":
            if c == "C":
                x1, y1 = num() + ox, num() + oy
            else:
                x1, y1 = (2 * x - last_ctrl[0], 2 * y - last_ctrl[1]) if last_ctrl else (x, y)
            x2, y2 = num() + ox, num() + oy
            ex, ey = num() + ox, num() + oy
            for k in range(1, steps + 1):
                t = k / steps
                u = 1 - t
                cur.append((u**3 * x + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t**3 * ex,
                            u**3 * y + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t**3 * ey))
            last_ctrl = (x2, y2)
            x, y = ex, ey
        elif c in "QT":
            if c == "Q":
                x1, y1 = num() + ox, num() + oy
            else:
                x1, y1 = (2 * x - last_ctrl[0], 2 * y - last_ctrl[1]) if last_ctrl else (x, y)
            ex, ey = num() + ox, num() + oy
            for k in range(1, steps + 1):
                t = k / steps
                u = 1 - t
                cur.append((u * u * x + 2 * u * t * x1 + t * t * ex, u * u * y + 2 * u * t * y1 + t * t * ey))
            last_ctrl = (x1, y1)
            x, y = ex, ey
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = int(num()), int(num())
            ex, ey = num() + ox, num() + oy
            cur.extend(arc_points(x, y, rx, ry, phi, large, sweep, ex, ey))
            x, y = ex, ey
            last_ctrl = None
        else:
            raise ValueError(f"path command {cmd}")
    if cur:
        subs.append(cur)
    return subs


def length(pts):
    return sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))


def rdp(pts, eps):
    """Ramer-Douglas-Peucker simplification, done iteratively."""
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        (ax, ay), (bx, by) = pts[a], pts[b]
        seg = math.hypot(bx - ax, by - ay)
        best, at = -1.0, -1
        for k in range(a + 1, b):
            px, py = pts[k]
            if seg == 0:
                dist = math.hypot(px - ax, py - ay)
            else:
                dist = abs((bx - ax) * (ay - py) - (ax - px) * (by - ay)) / seg
            if dist > best:
                best, at = dist, k
        if best > eps and at > 0:
            keep[at] = True
            stack.append((a, at))
            stack.append((at, b))
    return [p for p, k in zip(pts, keep) if k]


def outline(svg):
    """Byte points, whether it closes, and its box."""
    m = re.search(r'<path[^>]*\sd="([^"]+)"', svg)
    if not m:
        raise ValueError("no path")
    pts = max(path_subpaths(m.group(1)), key=length)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    size = max(max(xs) - min(xs), max(ys) - min(ys))
    closed = math.dist(pts[0], pts[-1]) < size * 0.03
    ring = pts[:-1] if closed and len(pts) > 2 else pts
    # Two halves, so a ring never folds flat
    if closed:
        far = max(range(len(ring)), key=lambda k: math.dist(ring[0], ring[k]))
        halves = [ring[: far + 1], ring[far:] + ring[:1]]
    else:
        halves = [ring]
    eps = size * 0.004
    while True:
        simple = []
        for h in halves:
            s = rdp(h, eps)
            simple.extend(s[:-1] if closed else s)
        if len(simple) <= MAX_POINTS:
            break
        eps *= 1.15
    minx, miny = min(xs), min(ys)
    scale = BOX / size
    out = []
    for px, py in simple:
        q = (round((px - minx) * scale), round((py - miny) * scale))
        if not out or q != out[-1]:
            out.append(q)
    if closed and len(out) > 1 and out[0] == out[-1]:
        out.pop()
    w = round((max(xs) - minx) * scale)
    h = round((max(ys) - miny) * scale)
    return out, closed, w, h


def f1db_races(season):
    races = []
    for entry in get(F1DB_API.format(season=season)):
        if entry["type"] != "dir":
            continue
        races.append(flat_yaml(get(f"{F1DB_RAW}/data/seasons/{season}/races/{entry['name']}/race.yml", raw=True)))
    return races


def haversine(a, b):
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 6371 * 2 * math.asin(math.sqrt(h))


def espn_calendar(season):
    """Race days to ESPN circuit ids, and their names."""
    by_day, names = {}, {}
    try:
        cal = get(ESPN_CAL.format(season=season))["leagues"][0]["calendar"]
    except Exception as ex:
        print(f"ESPN {season} calendar: {ex}", file=sys.stderr)
        return by_day, names
    for entry in cal:
        m = re.search(r"/events/(\d+)", entry["event"]["$ref"])
        if not m:
            continue
        # The calendar's times run three hours late against the header's.
        end = datetime.datetime.strptime(entry["endDate"], "%Y-%m-%dT%H:%MZ") - datetime.timedelta(hours=3)
        try:
            ev = get(f"{ESPN_CORE}/events/{m.group(1)}")
        except Exception as ex:
            print(f"ESPN event {m.group(1)}: {ex}", file=sys.stderr)
            continue
        c = re.search(r"/circuits/(\d+)", ev.get("circuit", {}).get("$ref", ""))
        if not c:
            continue
        cid = int(c.group(1))
        by_day[end.date()] = cid
        if cid not in names:
            try:
                names[cid] = get(f"{ESPN_CORE}/circuits/{cid}").get("fullName", "")
            except Exception as ex:
                print(f"ESPN circuit {cid}: {ex}", file=sys.stderr)
                names[cid] = ""
    return by_day, names


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--season", type=int, action="append")
    args = ap.parse_args()
    year = datetime.date.today().year
    seasons = sorted(set(args.season or [year - 1, year]))

    last = {}  # circuit: its last race in the seasons (layout, laps)
    espn_ids, espn_names = {}, {}
    jolpica = []
    for season in seasons:
        races = f1db_races(season)
        by_day, names = espn_calendar(season)
        espn_names.update(names)
        for race in races:
            cid = race["circuitId"]
            day = datetime.date.fromisoformat(race["date"])
            if cid not in last or day > last[cid][0]:
                last[cid] = (day, race)
            for off in (0, -1, 1):
                hit = by_day.get(day + datetime.timedelta(days=off))
                if hit is not None:
                    espn_ids.setdefault(cid, {})[day] = hit
                    break
        try:
            jolpica.extend(get(JOLPICA.format(season=season))["MRData"]["CircuitTable"]["Circuits"])
        except Exception as ex:
            print(f"Jolpica {season}: {ex}", file=sys.stderr)

    def jol_at(c):
        return float(c["Location"]["lat"]), float(c["Location"]["long"])

    entries, pool = [], []
    for cid in sorted(last):
        day, race = last[cid]
        info_text = get(f"{F1DB_RAW}/data/circuits/{cid}.yml", raw=True)
        info = flat_yaml(info_text)
        layout = race.get("circuitLayoutId", f"{cid}-1")
        pts, closed, w, h = outline(get(f"{F1DB_RAW}/assets/circuits/white/{layout}.svg", raw=True))
        lat, lon = float(info["latitude"]), float(info["longitude"])
        near = min(jolpica, key=lambda c: haversine((lat, lon), jol_at(c)), default=None)
        jol = ""
        if near is not None and haversine((lat, lon), jol_at(near)) < 25:
            jol = near["circuitId"]
        else:
            print(f"{cid}: no Jolpica circuit near", file=sys.stderr)
        espn = espn_ids[cid][max(espn_ids[cid])] if cid in espn_ids else 0
        if not espn:
            print(f"{cid}: not on ESPN's calendar", file=sys.stderr)
        names = [info.get("name", ""), info.get("fullName", ""), info.get("placeName", "")]
        names += yaml_list(info_text, "previousNames")
        if espn:
            names.append(espn_names.get(espn, ""))
        if jol:
            names.append(near["circuitName"])
        keys = []
        for n in names:
            k = circuit_key(n)
            if k and k not in keys:
                keys.append(k)
        entries.append({
            "key": cid, "jolpica": jol, "espn": espn, "names": "|".join(keys),
            "name": fold(info.get("name", cid)), "lat": round(lat * 10000), "lon": round(lon * 10000),
            "length": round(float(race.get("courseLength") or info.get("length") or 0) * 1000),
            "turns": int(race.get("turns") or info.get("turns") or 0), "laps": int(race.get("laps") or 0),
            "at": len(pool), "n": len(pts), "w": w, "h": h, "closed": closed, "layout": layout,
        })
        pool.extend(pts)
        print(f"{cid}: {layout}, {len(pts)} points, {w}x{h}, jolpica {jol or '-'}, espn {espn or '-'}", file=sys.stderr)

    lines = [
        "// Generated by scripts/build_circuits.py. Do not edit by hand.",
        "// Outlines and facts from F1DB, github.com/f1db/f1db.",
        f"// Seasons: {', '.join(str(s) for s in seasons)}.",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace espn {",
        "",
        "struct CircuitInfo {",
        "  const char *key;         // F1DB's circuit id: \"las-vegas\"",
        "  const char *jolpica;     // Jolpica's circuitId: \"vegas\"",
        "  uint32_t espn_id;        // ESPN's circuit id, 0 when its calendar lacks it",
        "  const char *names;       // circuit_key()s of its names, '|' between",
        "  const char *name;        // F1DB's short name: \"Las Vegas\"",
        "  int32_t lat_e4, lon_e4;  // degrees times 10000",
        "  uint16_t length_m;",
        "  uint8_t turns, laps;     // laps of its last race, 0 unknown",
        "  uint16_t at;             // first point in kCircuitPoints",
        "  uint8_t n;               // points, each x then y",
        "  uint8_t w, h;            // the outline's box; the long side is 255",
        "  bool closed;             // the last point joins the first",
        "};",
        "",
        "inline constexpr uint8_t kCircuitPoints[] = {",
    ]
    flat = [v for p in pool for v in p]
    for k in range(0, len(flat), 24):
        lines.append("    " + ", ".join(str(v) for v in flat[k:k + 24]) + ",")
    lines += ["};", "", "inline constexpr CircuitInfo kCircuits[] = {"]
    for e in entries:
        lines.append(
            f"    {{{c_str(e['key'])}, {c_str(e['jolpica'])}, {e['espn']}, {c_str(e['names'])}, {c_str(e['name'])}, "
            f"{e['lat']}, {e['lon']}, {e['length']}, {e['turns']}, {e['laps']}, {e['at']}, {e['n']}, {e['w']}, {e['h']}, "
            f"{'true' if e['closed'] else 'false'}}},  // F1DB layout {e['layout']}")
    lines += [
        "};",
        "inline constexpr size_t kCircuitCount = sizeof(kCircuits) / sizeof(kCircuits[0]);",
        "",
        "}",
    ]
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(entries)} circuits, {len(pool)} points", file=sys.stderr)


if __name__ == "__main__":
    main()
