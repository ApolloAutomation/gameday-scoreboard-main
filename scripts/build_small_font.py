#!/usr/bin/env python3
"""Regenerate firmware/fonts/spleen-5x8-latin1.bdf, the small font's Latin-1."""

import argparse
import sys
import urllib.request
from pathlib import Path

DESCRIPTION = """\
Regenerate firmware/fonts/spleen-5x8-latin1.bdf.

Spleen 5x8 draws ASCII only: its Latin-1 glyphs are there but blank, so the
smallest text showed a gap for a degree sign or an accented letter. This
script reads Spleen 5x8 at the commit the firmware pins and writes every
Latin-1 glyph the firmware lists: accented letters built from Spleen's own
letters with a mark over or under them, and the signs drawn here.
gameday-common.yaml takes them as the small font's extras.

  --check      fails when the committed file isn't what this script writes
  --spleen F   reads Spleen 5x8 from F instead of downloading it
"""

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "firmware" / "fonts" / "spleen-5x8-latin1.bdf"
SPLEEN = "https://github.com/fcambus/spleen/raw/c3b655c5718757863e828c02115e96b149281159/spleen-5x8.bdf"

# The signs, 4 wide, rows 0 to 7 (6 is the baseline row)
SIGNS = {
    "¡": {1: "..#.", 3: "..#.", 4: "..#.", 5: "..#.", 6: "..#.", 7: "..#."},
    "¢": {1: "..#.", 2: ".###", 3: "#.#.", 4: "#.#.", 5: ".###", 6: "..#."},
    "£": {1: "..##", 2: ".#..", 3: "###.", 4: ".#..", 5: ".#..", 6: "####"},
    "¤": {2: "#..#", 3: ".##.", 4: ".##.", 5: "#..#"},
    "¥": {1: "#..#", 2: "#..#", 3: ".##.", 4: "####", 5: "..#.", 6: "..#."},
    "¦": {0: "..#.", 1: "..#.", 2: "..#.", 4: "..#.", 5: "..#.", 6: "..#."},
    "§": {0: ".###", 1: "#...", 2: ".##.", 3: "#..#", 4: ".##.", 5: "...#", 6: "###."},
    "¨": {1: "#..#"},
    "©": {1: ".##.", 2: "#..#", 3: "#.##", 4: "#.##", 5: "#..#", 6: ".##."},
    "ª": {1: ".##.", 2: "#.#.", 3: ".##.", 5: "###."},
    "«": {2: ".#.#", 3: "#.#.", 4: ".#.#"},
    "¬": {3: "####", 4: "...#"},
    "®": {1: "###.", 2: "#.#.", 3: "##..", 4: "#.#."},
    "¯": {0: "####"},
    "°": {0: ".#..", 1: "#.#.", 2: ".#.."},
    "±": {1: ".#..", 2: "###.", 3: ".#..", 5: "###."},
    "²": {0: "##..", 1: "..#.", 2: ".#..", 3: "###."},
    "³": {0: "##..", 1: ".##.", 2: "..#.", 3: "##.."},
    "´": {0: "..#.", 1: ".#.."},
    "µ": {2: "#..#", 3: "#..#", 4: "#..#", 5: "###.", 6: "#...", 7: "#..."},
    "¶": {1: ".###", 2: "##.#", 3: "##.#", 4: ".#.#", 5: ".#.#", 6: ".#.#"},
    "·": {4: "..#."},
    "¸": {6: "..#.", 7: ".##."},
    "¹": {0: ".#..", 1: "##..", 2: ".#..", 3: "###."},
    "º": {1: ".#..", 2: "#.#.", 3: ".#..", 5: "###."},
    "»": {2: "#.#.", 3: ".#.#", 4: "#.#."},
    "¼": {0: "#...", 1: "#..#", 2: "#.#.", 3: "..#.", 4: ".#.#", 5: "#.##", 6: "...#"},
    "½": {0: "#...", 1: "#..#", 2: "#.#.", 3: "..##", 4: ".#.#", 5: "#.#.", 6: "..##"},
    "¾": {0: "##..", 1: ".#.#", 2: "##.#", 3: "..#.", 4: ".#.#", 5: "#.##", 6: "...#"},
    "¿": {1: "..#.", 3: "..#.", 4: ".#..", 5: "#...", 6: "#..#", 7: ".##."},
    "×": {3: "#.#.", 4: ".#..", 5: "#.#."},
    "÷": {2: ".#..", 4: "###.", 6: ".#.."},
    "Æ": {1: ".###", 2: "#.#.", 3: "####", 4: "#.#.", 5: "#.#.", 6: "#.##"},
    "æ": {2: ".##.", 3: "..##", 4: ".###", 5: "#.#.", 6: ".###"},
    "Ð": {1: "###.", 2: ".#.#", 3: "####", 4: ".#.#", 5: ".#.#", 6: "###."},
    "ð": {1: "##..", 2: "..#.", 3: ".###", 4: "#..#", 5: "#..#", 6: ".##."},
    "Ø": {1: ".###", 2: "#..#", 3: "#.##", 4: "##.#", 5: "#..#", 6: "###."},
    "ø": {2: ".###", 3: "#.##", 4: "#..#", 5: "##.#", 6: "###."},
    "Þ": {1: "#...", 2: "###.", 3: "#..#", 4: "###.", 5: "#...", 6: "#..."},
    "þ": {1: "#...", 2: "###.", 3: "#..#", 4: "#..#", 5: "###.", 6: "#...", 7: "#..."},
    "ß": {1: ".##.", 2: "#..#", 3: "#.#.", 4: "#..#", 5: "#..#", 6: "#.#."},
}

# Marks: the two rows right over the letter, a gap where it fits
MARKS = {
    "grave": ["##..", "...."], "acute": ["..##", "...."], "circ": [".##.", "#..#"], "tilde": ["##.#", "...."],
    "uml": ["#..#", "...."], "ring": [".##.", ".##."],
}
LETTERS = {
    "ÀÁÂÃÄÅ": "A", "Ç": "C", "ÈÉÊË": "E", "ÌÍÎÏ": "I", "Ñ": "N", "ÒÓÔÕÖ": "O", "ÙÚÛÜ": "U", "Ý": "Y",
    "àáâãäå": "a", "ç": "c", "èéêë": "e", "ìíîï": "i", "ñ": "n", "òóôõö": "o", "ùúûü": "u", "ýÿ": "y",
}
MARK_OF = {"À": "grave", "Á": "acute", "Â": "circ", "Ã": "tilde", "Ä": "uml", "Å": "ring", "Ç": "cedilla",
           "È": "grave", "É": "acute", "Ê": "circ", "Ë": "uml", "Ì": "grave", "Í": "acute", "Î": "circ", "Ï": "uml",
           "Ñ": "tilde", "Ò": "grave", "Ó": "acute", "Ô": "circ", "Õ": "tilde", "Ö": "uml", "Ù": "grave",
           "Ú": "acute", "Û": "circ", "Ü": "uml", "Ý": "acute", "ÿ": "uml"}
# The firmware's list (gameday-common.yaml's panel glyphs)
WANTED = "¡¢£¤¥¦§¨©ª«¬®¯°±²³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ"


def mark_of(ch):
    if ch in MARK_OF:
        return MARK_OF[ch]
    return MARK_OF[ch.upper()]


def read_spleen(text):
    glyphs, code, rows, bits = {}, None, [], False
    for line in text.splitlines():
        k = line.split()
        if not k:
            continue
        if k[0] == "ENCODING":
            code = int(k[1])
        elif k[0] == "BITMAP":
            bits, rows = True, []
        elif k[0] == "ENDCHAR":
            glyphs[code] = rows
            bits = False
        elif bits:
            rows.append("".join("#" if int(k[0], 16) >> (7 - i) & 1 else "." for i in range(4)))
    return glyphs


def rows_of(drawn):
    return [drawn.get(r, "....") for r in range(8)]


def compose(ch, base):
    rows = list(base)
    mark = mark_of(ch)
    if mark == "cedilla":
        rows[7] = "..#."
        return rows
    if ch in "ìíîï":
        # Dotless: the i's own dot goes
        rows[next(r for r in range(8) if rows[r] != "....")] = "...."
    if ch.isupper():
        # A capital loses a repeated row to make room
        cap = rows[1:7]
        drop = next((i for i in range(len(cap) - 1, 0, -1) if cap[i] == cap[i - 1]), 3)
        rows = ["....", "...."] + cap[:drop] + cap[drop + 1:] + [rows[7]]
    top = next(r for r in range(8) if rows[r] != "....")
    rows[top - 2:top] = MARKS[mark]
    return rows


def glyph_rows(ch, spleen):
    if ch in SIGNS:
        return rows_of(SIGNS[ch])
    for group, base in LETTERS.items():
        if ch in group:
            return compose(ch, spleen[ord(base)])
    raise KeyError(ch)


def bdf(spleen):
    out = ["STARTFONT 2.1",
           "COMMENT Latin-1 for Spleen 5x8 (2.1.0), which draws ASCII only.",
           "COMMENT Built by scripts/build_small_font.py from Spleen 5x8's letters;",
           "COMMENT Spleen: Copyright (c) 2018-2024, Frederic Cambus, BSD-2-Clause.",
           "FONT -misc-spleen-medium-r-normal--8-80-72-72-C-50-ISO10646-1",
           "SIZE 8 72 72",
           "FONTBOUNDINGBOX 5 8 0 -1",
           "STARTPROPERTIES 4",
           "FONT_ASCENT 7",
           "FONT_DESCENT 1",
           "PIXEL_SIZE 8",
           "SPACING \"C\"",
           "ENDPROPERTIES",
           f"CHARS {len(WANTED)}"]
    for ch in WANTED:
        rows = glyph_rows(ch, spleen)
        assert len(rows) == 8 and any("#" in r for r in rows), ch
        out += [f"STARTCHAR U+{ord(ch):04X}", f"ENCODING {ord(ch)}", "SWIDTH 625 0", "DWIDTH 5 0",
                "BBX 5 8 0 -1", "BITMAP"]
        out += [f"{sum(1 << (7 - i) for i, c in enumerate(r) if c == '#'):02X}" for r in rows]
        out.append("ENDCHAR")
    out.append("ENDFONT")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--spleen")
    ap.add_argument("--show", action="store_true", help="prints every glyph")
    a = ap.parse_args()
    if a.spleen:
        text = Path(a.spleen).read_text()
    else:
        with urllib.request.urlopen(SPLEEN, timeout=30) as r:
            text = r.read().decode()
    spleen = read_spleen(text)
    font = bdf(spleen)
    if a.show:
        for ch in WANTED:
            print(ch, *(r.replace(".", " ") for r in glyph_rows(ch, spleen)), sep="\n")
    if a.check:
        if not OUT.exists() or OUT.read_text() != font:
            print(f"{OUT.relative_to(ROOT)} isn't what this script writes; rerun it", file=sys.stderr)
            return 1
        print(f"{OUT.relative_to(ROOT)}: ok")
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(font)
    print(f"wrote {OUT.relative_to(ROOT)}, {len(WANTED)} glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
