#!/usr/bin/env python3
"""Scales Apollo Automation's logo dog into components/gameday/apollo_logo.h.

The source is firmware/art/apollo/apollo.png, the dog as Apollo Automation
draws him. Nothing is redrawn; the script only does what scaling needs:

1. Cut him out: a flood fill from the edges clears the background (clear
   pixels, or white on a flattened copy), so the white inside his eyes,
   which the fill can't reach, stays. A light edge left by a flattened copy
   is unmixed into his black outline, so no white halo is left.
2. Crop to the dog and his ledge.
3. Resize to 64 rows with a Lanczos filter (three lobes), on
   premultiplied alpha, in fixed point so every machine gets the same bytes.
4. Lay him on the start-up screen's Navy (kBrandNavy in boot_brand.h) and
   store RGB565 colors (truncated, as LVGL does) as a palette and one
   index byte a pixel.

    python3 scripts/build_apollo.py             write the header
    python3 scripts/build_apollo.py --check     fail when the header is stale
    python3 scripts/build_apollo.py --selftest  check the steps on small images
"""

import math
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "firmware", "art", "apollo", "apollo.png")
BRAND = os.path.join(ROOT, "components", "gameday", "boot_brand.h")
OUT = os.path.join(ROOT, "components", "gameday", "apollo_logo.h")
HEIGHT = 64
LOBES = 3
BITS = 14
# Opaque near-white the fill clears on a flattened copy
WHITE = 236
# Gray and lighter than half: the fill passes through it
GRAY_SPREAD = 24


def read_png(data):
    """(w, h, [(r, g, b, a)]) for 8-bit RGB or RGBA, not interlaced."""
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        raise SystemExit("not a PNG")
    w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise SystemExit("the logo must be 8-bit RGB or RGBA, not interlaced")
    at, idat = 8, b""
    while at + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IDAT":
            idat += data[at + 8:at + 8 + n]
        at += 12 + n
    raw = zlib.decompress(idat)
    bpp = 4 if ctype == 6 else 3
    row = w * bpp
    px, prev = [], bytearray(row)
    for y in range(h):
        f = raw[y * (row + 1)]
        line = bytearray(raw[y * (row + 1) + 1:(y + 1) * (row + 1)])
        for i in range(row):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
            elif f != 0:
                raise SystemExit(f"bad PNG filter {f}")
        for x in range(w):
            v = line[x * bpp:(x + 1) * bpp]
            px.append((v[0], v[1], v[2], v[3] if bpp == 4 else 255))
        prev = line
    return w, h, px


def png_bytes(w, h, px):
    """An RGBA PNG, for the selftest and previews."""
    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in px[y * w:(y + 1) * w]) for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def cut_out(w, h, px):
    """Clears the background reachable from the edges."""
    px = list(px)

    def light_gray(p):
        r, g, b, a = p
        return a == 255 and max(r, g, b) - min(r, g, b) <= GRAY_SPREAD and min(r, g, b) > 127

    def passable(p):
        return p[3] == 0 or light_gray(p)
    seen = bytearray(w * h)
    stack = [i for i in range(w * h) if (i % w in (0, w - 1) or i // w in (0, h - 1)) and passable(px[i])]
    for i in stack:
        seen[i] = 1
    while stack:
        i = stack.pop()
        x, y = i % w, i // w
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                j = ny * w + nx
                if not seen[j] and passable(px[j]):
                    seen[j] = 1
                    stack.append(j)
    # The outline: darkest opaque pixel touching the cleared area
    edge = [i for i in range(w * h) if not seen[i] and touches(seen, w, h, i)]
    dark = min((px[i] for i in edge if px[i][3] == 255), key=sum, default=(0, 0, 0, 255))
    ink = sum(dark[:3]) // 3

    def unmix(p):
        lum = sum(p[:3]) // 3
        a = 255 if lum <= ink else max(0, min(255, round(255 * (255 - lum) / (255 - ink))))
        return (dark[0], dark[1], dark[2], a) if a < 255 else p
    for i in range(w * h):
        if seen[i]:
            p = px[i]
            px[i] = (0, 0, 0, 0) if p[3] == 0 or min(p[:3]) >= WHITE else unmix(p)
    # A flattened copy's light rim, one pixel in
    for i in edge:
        p = px[i]
        if p[3] == 255 and max(p[:3]) - min(p[:3]) <= GRAY_SPREAD:
            px[i] = unmix(p)
    return px


def touches(seen, w, h, i):
    x, y = i % w, i // w
    return any(0 <= nx < w and 0 <= ny < h and seen[ny * w + nx]
               for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))


def crop(w, h, px):
    xs = [i % w for i in range(w * h) if px[i][3]]
    ys = [i // w for i in range(w * h) if px[i][3]]
    if not xs:
        raise SystemExit("the logo is empty after the cut")
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    return x1 - x0, y1 - y0, [px[y * w + x] for y in range(y0, y1) for x in range(x0, x1)]


def lanczos(x):
    if x == 0:
        return 1.0
    if abs(x) >= LOBES:
        return 0.0
    t = math.pi * x
    return LOBES * math.sin(t) * math.sin(t / LOBES) / (t * t)


def taps(src, dst):
    """Each output's (first source index, integer weights summing to 1 << BITS)."""
    scale = src / dst
    support = LOBES * max(scale, 1.0)
    span = max(scale, 1.0)
    out = []
    for i in range(dst):
        center = (i + 0.5) * scale
        lo, hi = max(0, int(math.floor(center - support))), min(src, int(math.ceil(center + support)))
        ws = [lanczos((j + 0.5 - center) / span) for j in range(lo, hi)]
        total = sum(ws)
        iw = [round(v / total * (1 << BITS)) for v in ws]
        iw[max(range(len(iw)), key=lambda k: iw[k])] += (1 << BITS) - sum(iw)
        out.append((lo, iw))
    return out


def resize(w, h, px, nw, nh):
    """Premultiplied RGBA at 255*255 scale, integers throughout."""
    pm = [(r * a, g * a, b * a, a * 255) for r, g, b, a in px]
    half = 1 << (BITS - 1)
    tx = taps(w, nw)
    mid = []
    for y in range(h):
        row = pm[y * w:(y + 1) * w]
        for lo, iw in tx:
            acc = [0, 0, 0, 0]
            for k, wt in enumerate(iw):
                p = row[lo + k]
                for c in range(4):
                    acc[c] += wt * p[c]
            mid.append(tuple((v + half) >> BITS for v in acc))
    out = []
    for lo, iw in taps(h, nh):
        for x in range(nw):
            acc = [0, 0, 0, 0]
            for k, wt in enumerate(iw):
                p = mid[(lo + k) * nw + x]
                for c in range(4):
                    acc[c] += wt * p[c]
            a = max(0, min(65025, (acc[3] + half) >> BITS))
            out.append(tuple(max(0, min(a, (v + half) >> BITS)) for v in acc[:3]) + (a,))
    return out


def on_navy(px, navy):
    """8-bit RGB of the premultiplied pixels over navy."""
    bg = ((navy >> 16) & 255, (navy >> 8) & 255, navy & 255)
    return [tuple((p[c] * 255 + bg[c] * (65025 - p[3]) + 32512) // 65025 for c in range(3)) for p in px]


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def brand_navy():
    with open(BRAND, encoding="utf-8") as fh:
        m = re.search(r"kBrandNavy = 0x([0-9A-Fa-f]{6})", fh.read())
    if not m:
        raise SystemExit("kBrandNavy not found in boot_brand.h")
    return int(m.group(1), 16)


def ledge_row(w, h, alpha):
    """The lowest row his ledge fills almost edge to edge."""
    rows = [y for y in range(h) if sum(alpha[y * w + x] >= 65025 * 3 // 4 for x in range(w)) * 10 >= w * 9]
    if not rows:
        raise SystemExit("no ledge found under the dog")
    return rows[-1]


def scale_logo(png, navy, height=HEIGHT):
    w, h, px = read_png(png)
    w, h, px = crop(w, h, cut_out(w, h, px))
    nw = max(1, round(w * height / h))
    small = resize(w, h, px, nw, height)
    return nw, height, small, on_navy(small, navy)


def header(navy, w, h, small, rgb):
    colors = [rgb565(*p) for p in rgb]
    pal = sorted(set(colors))
    if len(pal) > 256:
        raise SystemExit(f"{len(pal)} colors after scaling; the index is one byte")
    idx = {c: i for i, c in enumerate(pal)}
    out = ["// Generated by scripts/build_apollo.py; do not edit",
           "// Source: firmware/art/apollo/apollo.png", "#pragma once", "", "#include <cstdint>", "",
           "namespace espn {", "",
           f"inline constexpr uint32_t kApolloNavy = 0x{navy:06X};",
           f"inline constexpr int kApolloW = {w}, kApolloH = {h};",
           f"inline constexpr int kApolloLedgeY = {ledge_row(w, h, [p[3] for p in small])};",
           f"inline constexpr uint16_t kApolloPal[{len(pal)}] = {{"]
    for i in range(0, len(pal), 12):
        out.append("    " + ", ".join(f"0x{c:04X}" for c in pal[i:i + 12]) + ",")
    out.append("};")
    out.append(f"inline constexpr uint8_t kApolloPx[{w * h}] = {{")
    for y in range(h):
        out.append("    " + ", ".join(str(idx[c]) for c in colors[y * w:(y + 1) * w]) + ",")
    out += ["};", "", "}  // namespace espn", ""]
    return "\n".join(out)


def build():
    navy = brand_navy()
    with open(SRC, "rb") as fh:
        w, h, small, rgb = scale_logo(fh.read(), navy)
    return header(navy, w, h, small, rgb)


def selftest():
    # PNG round trip
    px = [(x * 40, y * 50, 7, 255 - x) for y in range(4) for x in range(5)]
    assert read_png(png_bytes(5, 4, px)) == (5, 4, px), "PNG round trip"

    # Black ring on white: its enclosed white glint stays
    n = 21
    img = []
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - 10, y - 10)
            img.append((255, 255, 255, 255) if d > 8 or d < 2 else (15, 14, 12, 255))
    img[10 * n + 1] = (100, 100, 100, 255)
    img[1 * n + 10] = (200, 200, 200, 255)
    cut = cut_out(n, n, img)
    assert cut[0][3] == 0 and cut[(n - 1) * n][3] == 0, "outside cleared"
    assert cut[10 * n + 10] == (255, 255, 255, 255), "the enclosed glint stays white"
    for i in (10 * n + 1, 1 * n + 10):
        assert 0 < cut[i][3] < 255 and cut[i][:3] == (15, 14, 12), "a gray edge unmixes into the ink"
    assert all(p[3] == 255 for i, p in enumerate(cut) if 2 <= math.hypot(i % n - 10, i // n - 10) <= 8)
    # Already clear outside: nothing else changes
    clear = [(0, 0, 0, 0) if math.hypot(i % n - 10, i // n - 10) > 8 else p for i, p in enumerate(img)]
    assert cut_out(n, n, clear) == clear, "a clean cutout is left alone"
    w, h, c = crop(n, n, cut)
    assert (w, h) == (18, 18), (w, h)

    # Taps sum to one; flat stays flat; means kept
    for src, dst in ((300, 64), (64, 64), (5, 3)):
        for _, iw in taps(src, dst):
            assert sum(iw) == 1 << BITS
    flat = resize(30, 30, [(200, 100, 50, 255)] * 900, 7, 7)
    assert all(p == (200 * 255, 100 * 255, 50 * 255, 65025) for p in flat), flat[0]
    ramp = resize(40, 1, [(x * 6, 0, 0, 255) for x in range(40)], 10, 1)
    mean = sum(p[0] for p in ramp) / 10 / 255
    assert abs(mean - 117) < 2, mean
    # Clear pixels come out exactly navy
    assert on_navy([(0, 0, 0, 0)], 0x1F425F) == [(0x1F, 0x42, 0x5F)]
    assert on_navy([(255 * 255, 0, 0, 65025)], 0x1F425F) == [(255, 0, 0)]
    assert rgb565(0x1F, 0x42, 0x5F) == 0x1A0B

    # The real logo builds, 64 rows tall
    text = build()
    assert "kApolloH = 64;" in text
    try:
        header(0, 300, 1, [(0, 0, 0, 65025)] * 300, [(i % 32 * 8, i // 32 * 4, 0) for i in range(300)])
    except SystemExit:
        pass
    else:
        raise AssertionError("accepted more than 256 colors")
    print("build_apollo selftest: ok")


def main():
    if "--selftest" in sys.argv:
        selftest()
        return 0
    text = build()
    if "--check" in sys.argv:
        try:
            with open(OUT, encoding="utf-8") as fh:
                same = fh.read() == text
        except FileNotFoundError:
            same = False
        if not same:
            print("components/gameday/apollo_logo.h is stale: run python3 scripts/build_apollo.py", file=sys.stderr)
            return 1
        print("apollo_logo.h is current")
        return 0
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {os.path.relpath(OUT, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
