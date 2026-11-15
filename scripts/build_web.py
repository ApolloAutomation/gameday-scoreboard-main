#!/usr/bin/env python3
"""Bundle the device web page: team table (from teams.h) + app.js -> bundle.js.

The bundle also gets the timezone list (timezones.h), the table the
firmware folds accented letters to plain ASCII with (sport_soccer.cpp's
kFold and fold_special), so the page names a favorite player the way the
panel reads ESPN's words, and the player prop stats (props.h's kPropStats:
sport, key, the panel's label and whether it is a yes prop), and the coins
and indexes from markets.h, for typed symbols.

TEAMS rows are [league key, id, abbr, name], and a minor league baseball team's
has its level (11 Triple-A to 14 Single-A) fifth, an AHL or ECHL team's its
logo file (teams.h's group).

ESPHome embeds a single js_include file, so the team list and the app are
concatenated here. bundle-demo.js is the same plus demo.js, for the demo
build only (firmware/pages/demo.yaml). Run after editing firmware/web/app.js
or demo.js or regenerating components/gameday/teams.h, and commit both bundles.

The bundles carry app.js and demo.js without their comments, indentation and
blank lines (strip_js), which the panel would otherwise keep in flash and send
on every page load. Strings, template literals and regexes are copied as they
are and every remaining line break stays, so the code reads the same. When
Node is installed, the build stops if a stripped file no longer parses.
"""

import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEAMS_H = ROOT / "components" / "gameday" / "teams.h"
APP_JS = ROOT / "firmware" / "web" / "app.js"
BUNDLE = ROOT / "firmware" / "web" / "bundle.js"
DEMO_JS = ROOT / "firmware" / "web" / "demo.js"
DEMO_BUNDLE = ROOT / "firmware" / "web" / "bundle-demo.js"

TEAM_RE = re.compile(r'\{League::(\w+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)",\s*(\d+)\}')
LEAGUES_H = ROOT / "components" / "gameday" / "leagues.h"
# One kLeagues row: enum, key, path, prefix, logo dir, then the sport and the
# per_event, season_list and logo_by_id flags.
LEAGUE_RE = re.compile(
    r'\{League::(\w+),\s*"(\w+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*Sport::\w+,'
    r"\s*(?:true|false),\s*(?:true|false),\s*(true|false),"
)
# One row of app.js's LEAGUES table, the fields it copies from leagues.h.
JS_LEAGUE_RE = re.compile(r'^\s*(\w+): \{ prefix: "([^"]*)",.*?path: "([^"]*)", logo: "([^"]*)", byId: (true|false)', re.M)
# Non-ESPN leagues carry their teams.h group in TEAMS.
SOURCE_RE = re.compile(r"\{League::(\w+),[^}\n]*Source::(?:MLB_STATS|HOCKEYTECH)\}")
TZ_H = ROOT / "components" / "gameday" / "timezones.h"
TZ_RE = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')
SOCCER_CPP = ROOT / "components" / "gameday" / "sport_soccer.cpp"
PROPS_H = ROOT / "components" / "gameday" / "props.h"
PROP_RE = re.compile(r'\{"(\w+)",\s*"([^"]+)",\s*Sport::(\w+),\s*(true|false),')
MARKETS_H = ROOT / "components" / "gameday" / "markets.h"
COIN_RE = re.compile(r'\{"(\w+)", "[\w-]+", "\w+"\}')
INDEX_RE = re.compile(r'\{"(\w+)", "\^(\w+)", "\w*", (true|false)\}')
ALIAS_RE = re.compile(r'if \(s == "(\w+)"\)\s*return "(\w+)";')


def market_tables(markets_h):
    """markets.h's coins, guessed indexes, each index's Yahoo symbol and coin aliases."""
    coins = COIN_RE.findall(markets_h)
    rows = INDEX_RE.findall(markets_h)
    indexes = [sym for sym, _, guess in rows if guess == "true"]
    yahoo = {sym: y for sym, y, _ in rows}
    alias = dict(ALIAS_RE.findall(markets_h))
    if len(coins) < 10 or len(indexes) < 5 or "XBT" not in alias or "SPX" not in yahoo or yahoo["SPX"] != yahoo.get("GSPC"):
        raise SystemExit("markets.h's coin and index tables don't parse")
    return {"coins": coins, "indexes": indexes, "yahoo": yahoo, "alias": alias}


def fold_tables(cpp):
    """sport_soccer.cpp's ASCII folding: kFold (U+00C0..U+017F, one letter each, '*' for the
    two-letter ones) and fold_special()'s code points, each to its letters."""
    m = re.search(r'static const char kFold\[\] =\s*((?:"[^"]*"\s*)+);', cpp)
    if not m:
        raise SystemExit("sport_soccer.cpp has no kFold table")
    fold = "".join(re.findall(r'"([^"]*)"', m.group(1)))
    if len(fold) != 0x180 - 0xC0:
        raise SystemExit(f"kFold has {len(fold)} letters, not {0x180 - 0xC0}")
    body = re.search(r"fold_special\(uint32_t cp\) \{(.*?)\n\}", cpp, re.S)
    if not body:
        raise SystemExit("sport_soccer.cpp has no fold_special()")
    special, cases = {}, []
    for case, ret in re.findall(r'case (0x[0-9A-Fa-f]+):|return "((?:[^"\\]|\\.)*)";', body.group(1)):
        if case:
            cases.append(int(case, 16))
        else:
            for cp in cases:
                special[cp] = ret.encode().decode("unicode_escape")
            cases = []
    if special.get(0x2019) != "'" or "*" in "".join(special.values()):
        raise SystemExit("fold_special() doesn't parse")
    for i, ch in enumerate(fold):
        if ch == "*" and 0xC0 + i not in special:
            raise SystemExit(f"kFold has '*' for U+{0xC0 + i:04X} but fold_special() doesn't")
    return fold, special


# A slash after these words starts a regex
JS_WORD = re.compile(r"[\w$]+")
REGEX_AFTER = {"return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw",
               "instanceof", "yield", "await"}


def strip_js(src):
    """src without comments, indentation, trailing spaces or blank lines."""
    lines, cur = [], []
    subs = []       # brace depth inside each open template ${...}
    tmpl = False    # inside a template literal's text
    last = ""       # last token: "v" a value, else its text
    i, n = 0, len(src)

    def end_line():
        text = "".join(cur).rstrip(" \t")
        if text:
            lines.append(text)
        cur.clear()

    def take(j):
        # src[i:j] as it is
        cur.append(src[i:j])
        return j

    while i < n:
        c = src[i]
        if tmpl:
            j = i
            while j < n and src[j] not in "`\\$":
                j += 1
            if j >= n:
                raise SystemExit("strip_js: a template literal never ends")
            if src[j] == "\\":
                i = take(j + 2)
            elif src[j] == "`":
                i = take(j + 1)
                tmpl, last = False, "v"
            elif src.startswith("${", j):
                i = take(j + 2)
                subs.append(0)
                tmpl, last = False, "{"
            else:
                i = take(j + 1)
            continue
        if c == "\n":
            end_line()
            i += 1
        elif c in " \t\r":
            if cur:
                cur.append(c)
            i += 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise SystemExit("strip_js: a block comment never ends")
            if "\n" in src[i:j]:
                end_line()
            elif cur:
                cur.append(" ")
            i = j + 2
        elif c in "'\"":
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\n":
                    raise SystemExit("strip_js: a string runs past its line")
                j += 2 if src[j] == "\\" else 1
            i = take(j + 1)
            last = "v"
        elif c == "`":
            i = take(i + 1)
            tmpl = True
        elif c == "/" and (last not in ("v", ")", "]") and not JS_WORD.fullmatch(last) or last in REGEX_AFTER):
            j, cls = i + 1, False
            while j < n and (cls or src[j] != "/"):
                if src[j] == "\n":
                    raise SystemExit("strip_js: a regex runs past its line")
                cls = (cls or src[j] == "[") and src[j] != "]"
                j += 2 if src[j] == "\\" else 1
            j += 1
            while j < n and src[j].isalpha():
                j += 1
            i = take(j)
            last = "v"
        elif JS_WORD.match(c):
            j = JS_WORD.match(src, i).end()
            last = src[i:j]
            i = take(j)
        else:
            if c == "{" and subs:
                subs[-1] += 1
            elif c == "}" and subs:
                if subs[-1] == 0:
                    subs.pop()
                    i = take(i + 1)
                    tmpl = True
                    continue
                subs[-1] -= 1
            last = c
            i = take(i + 1)
    end_line()
    if tmpl or subs:
        raise SystemExit("strip_js: a template literal never ends")
    return "\n".join(lines) + "\n"


def check_parses(name, text):
    """Stops the build when Node can't parse text."""
    node = shutil.which("node")
    if not node:
        return
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / (name + ".js")
        path.write_text(text, encoding="utf-8")
        r = subprocess.run([node, "--check", str(path)], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"stripped {name} doesn't parse:\n{r.stderr}")


def check_leagues(leagues, app_js):
    """app.js keeps its own copy of the leagues.h table. Stop the build when
    they differ, because a mismatch otherwise only shows as a broken logo or
    scoreboard URL on the device page."""
    want = {key: (prefix, path, logo, by_id == "true") for _lg, key, path, prefix, logo, by_id in leagues}
    have = {key: (prefix, path, logo, by_id == "true") for key, prefix, path, logo, by_id in JS_LEAGUE_RE.findall(app_js)}
    for key in sorted(set(want) | set(have)):
        if want.get(key) != have.get(key):
            raise SystemExit(f"app.js LEAGUES.{key} is {have.get(key)}, leagues.h says {want.get(key)}")


def main():
    leagues_h = LEAGUES_H.read_text(encoding="utf-8")
    leagues = LEAGUE_RE.findall(leagues_h)
    with_group = set(SOURCE_RE.findall(leagues_h))
    if with_group != {"MILB", "AHL", "ECHL"}:
        raise SystemExit(f"leagues.h's other sources are {sorted(with_group)}; app.js reads MILB, AHL and ECHL")
    app_js = APP_JS.read_text(encoding="utf-8")
    check_leagues(leagues, app_js)
    keys = {lg: key for lg, key, *_rest in leagues}
    rows = []
    for league, tid, abbr, name, group in TEAM_RE.findall(TEAMS_H.read_text(encoding="utf-8")):
        # Fifth field: MiLB level for headings, or AHL/ECHL logo.
        rows.append([keys[league], int(tid), abbr, name] + ([int(group)] if league in with_group else []))
    if len(rows) < 100:
        raise SystemExit("teams.h looks truncated")
    header = "// Generated by scripts/build_web.py from teams.h and app.js. Do not edit.\n"
    teams = "const TEAMS=" + json.dumps(rows, separators=(",", ":"), ensure_ascii=False) + ";\n"
    tz_text = "\n".join(ln for ln in TZ_H.read_text(encoding="utf-8").splitlines() if not ln.lstrip().startswith("//"))
    tzs = [[name, iana] for name, _posix, iana in TZ_RE.findall(tz_text)]
    if not tzs:
        raise SystemExit("timezones.h has no entries")
    tz = "const TZS=" + json.dumps(tzs, separators=(",", ":")) + ";\n"
    fold, special = fold_tables(SOCCER_CPP.read_text(encoding="utf-8"))
    folds = ("const FOLD=" + json.dumps(fold) + ";\nconst FOLD2=" +
             json.dumps({str(cp): v for cp, v in sorted(special.items())}, separators=(",", ":")) + ";\n")
    stats = [[sport.lower(), key, label, yes == "true"] for key, label, sport, yes in PROP_RE.findall(
        PROPS_H.read_text(encoding="utf-8"))]
    if len(stats) < 10:
        raise SystemExit("props.h's kPropStats looks truncated")
    props = "const PROP_STATS=" + json.dumps(stats, separators=(",", ":")) + ";\n"
    markets = "const MARKETS=" + json.dumps(market_tables(MARKETS_H.read_text(encoding="utf-8")),
                                            separators=(",", ":")) + ";\n"
    app_min = strip_js(app_js)
    demo_min = strip_js(DEMO_JS.read_text(encoding="utf-8"))
    for name, text in (("app", app_min), ("demo", demo_min)):
        if strip_js(text) != text:
            raise SystemExit(f"strip_js isn't stable on {name}.js")
        check_parses(name, text)
    page = header + teams + tz + folds + props + markets + app_min
    BUNDLE.write_text(page, encoding="utf-8", newline="\n")
    print(f"wrote {BUNDLE} ({BUNDLE.stat().st_size} bytes, {len(rows)} teams)")
    demo = page.replace("from teams.h and app.js", "from teams.h, app.js and demo.js", 1)
    DEMO_BUNDLE.write_text(demo + demo_min, encoding="utf-8", newline="\n")
    print(f"wrote {DEMO_BUNDLE} ({DEMO_BUNDLE.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
