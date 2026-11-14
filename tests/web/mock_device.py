"""Mock of the scoreboard's web server for the device page tests.

Serves firmware/web/bundle.js and app.css at /0.js and /0.css (where ESPHome's
web_server puts js_include and css_include), the ESPHome /events stream for
the entities the page builds controls from, and the gameday component's
routes with the response shapes from components/gameday/gameday.cpp:

  GET  /gameday/state    the document rebuild_state_() writes
  POST /gameday/set      only the keys in SET_KEYS, then each listed display's own (Stocks &
                         Crypto's, the Flight Tracker's); 400 when none match (player= and
                         playerdel= keep the favorite players in their slots, as players.h does,
                         and prop= and propdel= the player props in theirs, as props.h does)
  POST /gameday/action   400 without do=, 404 for a name the firmware does not know
                         (anim takes play=pass|run|ret|fg|pat|two|tworun|hr|soccer|hockey);
                         power_ok sets health.power back to "ok"
  GET  /gameday/wifi     the saved networks (components/saved_networks), names only
  POST /gameday/wifi     forget=<ssid>; 404 when it is not saved

/?demo loads bundle-demo.js instead, the demo build's page, and
GET/POST /gameday/demo answer as demo.cpp does.

Test hooks: POST /_reset with an optional JSON body
{"state": {...overrides...}, "fail": ["state", "set"], "wifi": [...]} puts the
state back, merges the overrides and makes the named routes answer 500. Two
more fail names drop the connection with no answer, the way a panel that has
gone does: "events" for the event stream (an open one ends within half a
second), "down" for every route but the test hooks. "wifi" replaces the saved
network list; null stands for firmware without the list, whose /gameday/wifi
answers 404. GET /_log lists the /gameday/set, /gameday/action and
/gameday/wifi requests seen since the last reset.

Started by playwright.config.js. Run by hand with `python mock_device.py 8765`
and open http://127.0.0.1:8765/.

The set keys, their limits and the Live auto pick come from
components/gameday/set_request.h, the bet limits from bets.h, and the prop
limits and stats from props.h. `python mock_device.py --selftest` runs
tests/fixtures/set_cases.json, the table the firmware's host tests also
check, through apply_set, so the mock can't drift from the panel.
"""
import copy
import itertools
import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

REPO = Path(__file__).resolve().parents[2]
WEB = Path(os.environ.get("GAMEDAY_WEB", REPO / "firmware" / "web"))

# Same tables the firmware and scripts/build_web.py read.
# League names and keys can hold digits: League::F1, "f1".
TEAM_RE = re.compile(r'\{League::([A-Z0-9]+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)",\s*(\d+)\}')
LEAGUE_RE = re.compile(r'\{League::[A-Z0-9]+,\s*"([a-z0-9]+)",')
TZ_RE = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')
TEAMS = {}
for lg, tid, abbr, name, _ in TEAM_RE.findall((REPO / "components/gameday/teams.h").read_text(encoding="utf-8")):
    TEAMS[(lg.lower(), int(tid))] = (abbr, name)
# kLeagues order, which is the order the firmware lists live leagues in.
LEAGUES_H = (REPO / "components/gameday/leagues.h").read_text(encoding="utf-8")
LEAGUES = LEAGUE_RE.findall(LEAGUES_H)
# The golf tours and racing series: a team, but never a favorite or a Live
# games league (set_request.h).
BOARD = set(re.findall(r'\{League::\w+,\s*"(\w+)",[^}]*Sport::(?:GOLF|RACING)', LEAGUES_H))
# Non-ESPN leagues have no favorite players
NOT_ESPN = set(re.findall(r'\{League::\w+,\s*"(\w+)",[^}]*Source::(?!ESPN\b)\w+', LEAGUES_H))
TZ_TEXT = "\n".join(ln for ln in (REPO / "components/gameday/timezones.h").read_text(encoding="utf-8").splitlines()
                    if not ln.lstrip().startswith("//"))
TZ_ROWS = TZ_RE.findall(TZ_TEXT)
TZS = [name for name, _posix, _iana in TZ_ROWS]

# /gameday/set's keys in the order the firmware applies them, and its limits.
SET_H = (REPO / "components/gameday/set_request.h").read_text(encoding="utf-8")
SET_KEYS = tuple(re.findall(r'"(\w+)"', re.search(r"kSetKeys\[\] = \{(.*?)\};", SET_H, re.S).group(1)))
LIMITS = {k: int(v) for k, v in re.findall(r"(?:constexpr|static const) int (k\w+) = (\d+);",
                                             SET_H + (REPO / "components/gameday/startup.h").read_text(encoding="utf-8")
                                             + (REPO / "components/gameday/idle.h").read_text(encoding="utf-8")
                                             + (REPO / "components/gameday/bets.h").read_text(encoding="utf-8")
                                             + (REPO / "components/gameday/props.h").read_text(encoding="utf-8")
                                             + (REPO / "components/gameday/apps.h").read_text(encoding="utf-8"))}
# props.h's kPropStats: (sport, key) to (label, yes)
PROP_STATS = {(sport.lower(), key): (label, yes == "true") for key, label, sport, yes in re.findall(
    r'\{"(\w+)",\s*"([^"]+)",\s*Sport::(\w+),\s*(true|false),',
    (REPO / "components/gameday/props.h").read_text(encoding="utf-8"))}
SPORT_OF = dict(re.findall(r'\{League::\w+,\s*"(\w+)",[^}]*Sport::(\w+)', LEAGUES_H))
FAV_MAX = int(re.search(r"FAV_MAX = (\d+);", (REPO / "components/gameday/gameday.h").read_text(encoding="utf-8"))[1])
# 2x2 needs max_rows 2 and max_cols at least 4
COMMON = (REPO / "firmware/gameday-common.yaml").read_text(encoding="utf-8")
MAX_COLS = int(re.search(r"^\s*max_cols: (\d+)", COMMON, re.M)[1])
MAX_ROWS = int(re.search(r"^\s*max_rows: (\d+)", COMMON, re.M)[1])
LAYOUTS_H = (REPO / "components/panel_layout/layouts.h").read_text(encoding="utf-8")
WIRING_COUNT = int(re.search(r"kWiringCount = (\d+);", LAYOUTS_H)[1])


def layout_ok(cols, rows):
    """panel_layouts::layout_ok() with the build's limits."""
    if rows == 1:
        return 1 <= cols <= MAX_COLS
    return rows == 2 and rows <= MAX_ROWS and cols == 2 and cols * rows <= MAX_COLS
FIXTURE = REPO / "tests/fixtures/set_cases.json"

# apps.h's displays by id; split is 5 plus id.
APPS_H = (REPO / "components/gameday/apps.h").read_text(encoding="utf-8")
DISPLAY_IDS = dict(re.findall(r"(\w+) = (\d+)", re.search(r"enum class Display : uint8_t \{(.*?)\};", APPS_H).group(1)))
DISPLAY_KEYS = dict(re.findall(r"case Display::(\w+):\s*return \"(\w+)\";",
                               APPS_H[APPS_H.index("display_key("):APPS_H.index("display_name(")]))
DISPLAY_NAMES = dict(re.findall(r"case Display::(\w+):\s*return \"([^\"]+)\";",
                                APPS_H[APPS_H.index("display_name("):APPS_H.index("kGameDayOnly")]))
SPLIT_GAMEDAY = int(re.search(r"GAMEDAY = (\d+)", SET_H)[1])
# The Flight Tracker's own keys and limits (app_flights.h, app_flights.cpp)
FLIGHTS_H = (REPO / "components/gameday/app_flights.h").read_text(encoding="utf-8")
FLIGHTS_CPP = (REPO / "components/gameday/app_flights.cpp").read_text(encoding="utf-8")
FLIGHT_SET_KEYS = tuple(re.findall(r'"(\w+)"', re.search(r"kFlightSetKeys\[\] = \{(.*?)\};", FLIGHTS_H, re.S).group(1)))
FLIGHTS_ID = int(DISPLAY_IDS["FLIGHTS"])
FLIGHT_MAX = {k: int(v) for k, v in zip(("km", "nm", "mi"), re.search(
    r"radius_max\(DistUnit u\) \{ return u == DistUnit::KM \? (\d+) : u == DistUnit::NM \? (\d+) : (\d+);",
    FLIGHTS_CPP).groups())}
FLIGHT_LIMITS = {k: int(v) for k, v in re.findall(r"static const \w+ (k\w+) = (\d+);", FLIGHTS_H)}
COORDS_RE = re.compile(r"[ \t]*([+-]?)([0-9]{1,3})(?:\.([0-9]*))?(?:[ \t]*,[ \t]*|[ \t]+)"
                       r"([+-]?)([0-9]{1,3})(?:\.([0-9]*))?[ \t]*\Z")


def flights_doc():
    """prefs_state(): the Flight Tracker's settings, as a fresh panel has them."""
    return {"home": "", "place": "", "radius": FLIGHT_LIMITS["kRadiusDefault"], "radius_unit": "mi",
            "receiver": "", "units": "imperial"}


def parse_coords(v):
    """parse_coords(): two decimals, digit by digit, to four places; None when refused."""
    m = COORDS_RE.match(v)
    if not m:
        return None
    out = []
    for sign, whole, frac, top in ((m[1], m[2], m[3], 900000), (m[4], m[5], m[6], 1800000)):
        e4 = (int(whole) * 100000 + int(((frac or "") + "00000")[:5]) + 5) // 10
        if e4 > top:
            return None
        out.append(-e4 if sign == "-" else e4)
    return None if out == [0, 0] else out


def coord_text(e4):
    return "-" * (e4 < 0) + "%d.%04d" % divmod(abs(e4), 10000)


def flights_set(k, v):
    """prefs_set() for one of the Flight Tracker's keys."""
    f = STATE.setdefault("flights", flights_doc())
    if k == "flyhome":
        if v in ("", "none"):
            f["home"], f["place"] = "", ""
        elif parse_coords(v):
            f["home"] = ",".join(coord_text(x) for x in parse_coords(v))
    elif k == "flyplace":
        b = bytes(c for c in v.encode() if c >= 0x20 and c != 0x7F)
        n = min(len(b), FLIGHT_LIMITS["kPlaceMax"])
        while 0 < n < len(b) and b[n] & 0xC0 == 0x80:
            n -= 1
        f["place"] = b[:n].decode()
    elif k == "flyradius":
        m = re.fullmatch(r"([0-9]{1,3})(mi|km|nm)?", v, re.ASCII)
        if m and int(m[1]) >= 1:
            unit = m[2] or "mi"
            f["radius"], f["radius_unit"] = min(int(m[1]), FLIGHT_MAX[unit]), unit
    elif k == "flyreceiver":
        v = "" if v == "none" else v
        if len(v) <= FLIGHT_LIMITS["kReceiverMax"] and all(0x20 < ord(c) < 0x7F for c in v):
            f["receiver"] = v


def set_keys():
    """handle_set_()'s keys: kSetKeys, then each display's the firmware has."""
    ids = display_ids()
    return SET_KEYS + (MARKET_KEYS if MARKETS in ids else ()) + (FLIGHT_SET_KEYS if FLIGHTS_ID in ids else ())


def display_doc(d):
    """rebuild_state_()'s entry in displays for Display d."""
    name = next(n for n, i in DISPLAY_IDS.items() if int(i) == d)
    return {"id": d, "key": DISPLAY_KEYS.get(name, "gameday"), "name": DISPLAY_NAMES.get(name, "Game Day"),
            "split": SPLIT_GAMEDAY + d}


def display_ids():
    return {d["id"] for d in STATE.get("displays") or [display_doc(0)]}


# Stocks & Crypto's keys, coins, indexes and defaults (app_markets.h).
APP_MARKETS_H = (REPO / "components/gameday/app_markets.h").read_text(encoding="utf-8")
MARKETS_H = (REPO / "components/gameday/markets.h").read_text(encoding="utf-8")
MARKETS = int(DISPLAY_IDS["MARKETS"])
MARKET_KEYS = tuple(re.findall(r'"(\w+)"', re.search(r"kMarketSetKeys\[\] = \{(.*?)\};", APP_MARKETS_H, re.S).group(1)))
MARKET_SLOTS = int(re.search(r"kMarketSlots = (\d+);", MARKETS_H)[1])
MARKET_SYM_MAX = int(re.search(r"kMarketSymMax = (\d+);", MARKETS_H)[1])
MARKET_COINS = set(re.findall(r'\{"(\w+)", "[\w-]+", "\w+"\}', MARKETS_H))
MARKET_INDEX_ROWS = re.findall(r'\{"(\w+)", "\^(\w+)", "\w*", (true|false)\}', MARKETS_H)
MARKET_INDEX_GUESS = {sym: guess == "true" for sym, _, guess in MARKET_INDEX_ROWS}
MARKET_INDEX_YAHOO = {sym: yahoo for sym, yahoo, _ in MARKET_INDEX_ROWS}
MARKET_ALIAS = dict(re.findall(r'if \(s == "(\w+)"\)\s*return "(\w+)";', MARKETS_H))
MARKET_DEFAULTS = re.findall(r'"(\w+)"', re.search(r"for \(const char \*t : \{(.*?)\}\)", APP_MARKETS_H).group(1))
MARKET_KINDS = ("stock", "index", "crypto")
# Saved (symbol, kind) pairs; empty shows the defaults
MARKET_LIST = []
# Made-up quotes for the page: price, change, percent, direction, state
FAKE_QUOTES = {"SPY": ("672.04", "-1.30", "-0.19%", -1, "closed"), "AAPL": ("355.63", "+7.12", "+2.04%", 1, "closed"),
               "BTC": ("73,738.65", "-186.54", "-0.25%", -1, "always"), "ETH": ("3,201.17", "+12.40", "+0.39%", 1, "always"),
               "TSLA": ("402.10", "0.00", "0.00%", 0, "closed")}


def market_ok(s, kind):
    """market_symbol_valid() (markets.h)."""
    alnum = lambda c: "A" <= c <= "Z" or "0" <= c <= "9"
    if not s or len(s) > MARKET_SYM_MAX:
        return False
    if kind != "stock":
        return all(alnum(c) for c in s)
    if not alnum(s[0]) or not alnum(s[-1]):
        return False
    return all(alnum(c) or (c in ".-=" and alnum(s[i])) for i, c in enumerate(s[1:]))


def parse_market_symbol(text, kind):
    """parse_market_symbol() (markets.h): (symbol, kind) or None; kind "" guesses."""
    s = ""
    for c in text:
        if c == " ":
            continue
        s += c.upper() if "a" <= c <= "z" else c
        if len(s.encode()) > 16:
            return None
    if s.startswith("$"):
        s = s[1:]
    if s.startswith("^"):
        if kind not in ("", "index"):
            return None
        s, kind = s[1:], "index"
    coin_ok = kind in ("", "crypto")
    if coin_ok and len(s) > 4 and s[-4:] in ("-USD", "/USD"):
        s, kind = s[:-4], "crypto"
    if coin_ok:
        s = MARKET_ALIAS.get(s, s)
        if kind == "" and s in MARKET_COINS:
            kind = "crypto"
    if kind == "":
        kind = "index" if MARKET_INDEX_GUESS.get(s) else "stock"
    if kind == "stock":
        s = s.replace("/", ".")
        if len(s) >= 3 and s[-2] == "-" and "A" <= s[-1] <= "Z":
            s = s[:-2] + "." + s[-1]
    return (s, kind) if market_ok(s, kind) else None


def market_same(a, b):
    """market_same() (markets.h): two names for one index match."""
    if a[1] != b[1]:
        return False
    return a[0] == b[0] or (a[1] == "index" and a[0] in MARKET_INDEX_YAHOO and
                            MARKET_INDEX_YAHOO[a[0]] == MARKET_INDEX_YAHOO.get(b[0]))


def market_slot(v, free_ok=False):
    """parse_market_slot() (markets.h): one ASCII digit, 0 only when free_ok."""
    return int(v) if len(v) == 1 and ("0" if free_ok else "1") <= v <= str(MARKET_SLOTS) else None


def markets_shown():
    return list(MARKET_LIST) if MARKET_LIST else [parse_market_symbol(t, "") for t in MARKET_DEFAULTS]


def markets_set(k, v):
    """markets_set() (app_markets.h): edits the list shown."""
    was = markets_shown()
    work = list(was)
    if k == "mkt":
        f = v.split(",")
        if len(f) < 2 or len(f) > 3 or market_slot(f[0], True) is None or (len(f) == 3 and f[2] not in ("",) + MARKET_KINDS):
            return
        got = parse_market_symbol(f[1], f[2] if len(f) == 3 else "")
        if not got:
            return
        slot = market_slot(f[0], True)
        if not any(market_same(x, got) for x in work):
            if slot == 0 or slot > len(work):
                if len(work) == MARKET_SLOTS:
                    return
                work.append(got)
            else:
                work[slot - 1] = got
    elif k == "mktdel":
        slot = market_slot(v)
        if slot is None or slot > len(work):
            return
        del work[slot - 1]
    elif k == "mktmove":
        a, comma, b = v.partition(",")
        a, b = market_slot(a), market_slot(b) if comma else None
        if a is None or b is None or a > len(work) or b > len(work):
            return
        work.insert(b - 1, work.pop(a - 1))
    if work != was:
        MARKET_LIST[:] = work


def markets_doc():
    """MarketsApp::state(), with made-up quotes for known symbols."""
    syms = []
    for i, (sym, kind) in enumerate(markets_shown()):
        o = {"slot": i + 1, "sym": sym, "kind": kind}
        if sym in FAKE_QUOTES:
            price, chg, pct, d, st = FAKE_QUOTES[sym]
            o.update({"price": price, "chg": chg, "pct": pct, "dir": d, "st": st})
        else:
            o["miss"] = True
        syms.append(o)
    return {"syms": syms, "max": MARKET_SLOTS, "defaults": not MARKET_LIST, "src": STATE.get("markets_src", "yahoo"),
            "read": STATE.get("markets_read") or int(time.time())}


def team(lg, tid):
    """put_team(): league, id, abbr and name, or an empty object."""
    if (lg, tid) not in TEAMS:
        return {}
    abbr, name = TEAMS[(lg, tid)]
    return {"l": lg, "id": tid, "abbr": abbr, "name": name}


INITIAL = {
    "name": "gameday-2f6a70", "version": "v1.5.4", "setup": True, "panels": 2, "rows": 1, "wiring": 0,
    "team": team("nfl", 6),
    # rotate in whole minutes for old pages; rotatesec in seconds
    "mode": 0, "rotate": 1, "rotatesec": 60, "lockon": 15, "release": 30, "collide": 0,
    "favs": [team("nfl", 6), team("nfl", 34), team("ncaa", 145), {}],
    "favmax": FAV_MAX, "today": False, "live": ["nfl", "ncaa"], "live_auto": True,
    "tz": 1, "tz_name": "US Central", "tz_auto": True,
    "down": True, "count": True, "match": True, "play": True, "odds": True, "opp": True, "bootaddr": True,
    "autoupd": False, "beta": False, "standing": 0,
    # Clock size and Text size: 0 Normal, 1 Large.
    "clocksize": 0, "textsize": 0,
    # Big-play alert types on, as bit (type - 1).
    "alerts": (1 << LIMITS["kAlertBits"]) - 1, "alertfav": False, "anim": True,
    # The big-play watcher (components/gameday/watch.h): stay in seconds.
    "watch": True, "watchstay": 60, "watchmine": False,
    "misses": 0, "captive": False, "stale_s": 4, "fallback": False, "recap": False, "follow": False,
    # A big-play visit, and whether Follow still applies
    "visit": False, "visit_follow": False,
    # idle_view is "", "clock", "scores", "today" or "logo" (idle.h)
    "idle": 0, "idledim": 0, "night": False, "nightstart": "22:00", "nightend": "07:00", "nightbri": 20,
    "idle_view": "", "night_now": False, "dark": False,
    # split 0-8 (split.h); state_doc() derives split_at, split_now
    "split": 0, "splitteam": {}, "split_at": "", "split_now": "",
    # Full screen: a main board display fills the panel
    "full": False, "full_now": False,
    # The main area's display and those the firmware has
    "display": 0, "display_now": "gameday",
    "espn_kb": 412, "espn_reads": 23,
    # Manual bets (bets.h): filled slots only, and capacity
    "bets": [], "betmax": LIMITS["kBetSlots"],
    # Player props (props.h): filled slots only, and capacity
    "props": [], "propmax": LIMITS["kPropSlots"],
    # Favorite players (players.h): filled slots in slot order
    "players": [], "playermax": LIMITS["kPlayerSlots"],
    # GamedayComponent::health_json_(), added at the end of each read.
    "health": {"up": 7260, "start": "restart", "boots": 2, "fault": "crash", "fault_ago": 90000,
               "heap_kb": 143.2, "block_kb": 40.0, "psram_kb": 4100.5, "rssi": -58, "espn": 200,
               "espn_ago": 9, "version": "1.5.4", "built": 1792285260, "channel": "stable",
               # guard_json_keys() (components/gameday/guard.h)
               "temp": 46.5, "heat": 0, "brownouts": 0, "power": "ok"},
    # Spliced in live by the firmware (wifi_history.h): an hour up, one drop.
    "wifi": {"rssi": -61, "step": 300, "hist": [-58, -59, -60, None, -64, -62, -61, -61, -60, -62, -61, -61],
             "weak": False, "drops": 1, "day": 1, "causes": {"signal": 1}, "last": "signal", "last_ago": 2400,
             "last_down": 42},
    "game": {"s": "PRE", "l": "nfl", "ta": "DAL", "ti": 6, "ts": 0, "oa": "PHI", "oi": 21, "os": 0,
             "tr": "1-0", "or": "0-1", "p": 0, "tt": 3, "ot": 3, "rz": False, "tv": "FOX",
             "venue": "AT&T Stadium", "odds": "DAL -3", "ou": "47.5", "detail": "Sun 3:25 PM",
             "kick": 1792531500, "lp": "", "dd": "", "c": "", "d": "", "k": "Sun 3:25 PM",
             "tc": "041E42", "oc": "004C54", "id": "401772001"},
    "status": "DAL vs PHI | Sun 3:25 PM | on FOX",
    "splash": "", "splash_color": "000000",
    "next": [{"id": "401772001", "kick": 1792531500, "oi": 21, "oa": "PHI", "on": "Eagles", "home": True,
              "neutral": False, "tv": "FOX"},
             {"id": "401772002", "kick": 1793136300, "oi": 28, "oa": "WSH", "on": "Commanders", "home": False,
              "neutral": False, "tv": "CBS"},
             {"id": "401772003", "kick": 1793741100, "oi": 34, "oa": "HOU", "on": "Texans", "home": True,
              "neutral": False, "tv": "NBC"}],
}

# ESPHome 2026.x state events (detail=all shape) for the entities app.js uses.
ENTITIES = [
    {"id": "switch/Power", "domain": "switch", "name": "Power", "state": "ON", "value": True},
    {"id": "number/Brightness", "domain": "number", "name": "Brightness", "state": "128", "value": "128",
     "min_value": "1", "max_value": "255", "step": "1"},
    {"id": "number/Scroll Speed", "domain": "number", "name": "Scroll Speed", "state": "5", "value": "5",
     "min_value": "1", "max_value": "10", "step": "1"},
    {"id": "select/Select Page", "domain": "select", "name": "Select Page", "state": "Scoreboard",
     "value": "Scoreboard", "option": ["Scoreboard", "Clock"]},
    {"id": "text_sensor/Game Status", "domain": "text_sensor", "name": "Game Status",
     "state": INITIAL["status"], "value": INITIAL["status"]},
    {"id": "update/Firmware", "domain": "update", "name": "Firmware", "state": "NO UPDATE", "value": "1.5.4",
     "current_version": "1.5.4", "summary": "", "release_url": ""},
    {"id": "text_sensor/IP", "domain": "text_sensor", "name": "IP", "state": "10.10.10.97", "value": "10.10.10.97"},
    {"id": "button/Refresh Now", "domain": "button", "name": "Refresh Now", "state": ""},
    {"id": "button/Check for Updates", "domain": "button", "name": "Check for Updates", "state": ""},
    {"id": "button/Reboot", "domain": "button", "name": "Reboot", "state": ""},
    {"id": "button/Reset Wi-Fi", "domain": "button", "name": "Reset Wi-Fi", "state": ""},
    {"id": "button/Factory Reset", "domain": "button", "name": "Factory Reset", "state": ""},
]
FIRMWARE = next(e for e in ENTITIES if e["name"] == "Firmware")
INITIAL["displays"] = [display_doc(0)]
# Flight Tracker settings; resets may add status and planes
INITIAL["flights"] = flights_doc()

STATE = copy.deepcopy(INITIAL)
FAIL = set()
LOG = []
# The saved Wi-Fi networks as GET /gameday/wifi lists them, the one tried first
# first. None: firmware without the list.
WIFI_INITIAL = [{"ssid": "Home", "connected": True}, {"ssid": "iPhone", "connected": False}]
WIFI = {"list": copy.deepcopy(WIFI_INITIAL)}
# The saved team's league byte, which outlives a cleared team and counts in
# Live auto. A fresh panel's is NFL.
TEAM_LEAGUE = ["nfl"]
# Players by slot (players.h's blob); None when empty
PLAYER_SLOTS = [None] * LIMITS["kPlayerSlots"]


def reset_state(overrides):
    STATE.clear()
    STATE.update(copy.deepcopy(INITIAL))
    STATE.update(overrides)
    if isinstance(overrides.get("flights"), dict):
        STATE["flights"] = {**flights_doc(), **overrides["flights"]}
    # rotate and rotatesec are one setting
    if isinstance(overrides.get("rotate"), int) and "rotatesec" not in overrides:
        STATE["rotatesec"] = clamp(overrides["rotate"], "kRotateMinMinutes", "kRotateMaxMinutes") * 60
    if isinstance(overrides.get("rotatesec"), int) and "rotate" not in overrides:
        STATE["rotate"] = rotate_minutes_of(overrides["rotatesec"])
    # A null override drops the key, like firmware that doesn't send it.
    for k in [k for k, v in STATE.items() if v is None]:
        del STATE[k]
    TEAM_LEAGUE[0] = (STATE.get("team") or {}).get("l", "nfl")
    saved = STATE.pop("markets", None) or []
    MARKET_LIST[:] = [(m["sym"], m["kind"]) for m in saved]
    players = STATE.get("players") or []
    PLAYER_SLOTS[:] = [players[i] if i < len(players) else None for i in range(LIMITS["kPlayerSlots"])]


# The firmware's C library reads ASCII digits and spaces only; Python's \d and
# \s and str.isdigit() also take other scripts' digits, like "\u0663".
def atoi(v):
    m = re.match(r"\s*[+-]?\d+", v, re.ASCII)
    return int(m.group()) if m else 0


def clamp(v, lo, hi):
    return min(LIMITS[hi], max(LIMITS[lo], v))


def clamp_rotate_seconds(v):
    """clamp_rotate_seconds() (startup.h): under a minute is 30 s, else whole minutes, rounded down, at most 30."""
    if v < 60:
        return LIMITS["kRotateHalfSeconds"]
    return clamp(v // 60, "kRotateMinMinutes", "kRotateMaxMinutes") * 60


def rotate_minutes_of(seconds):
    """rotate_minutes_of() (startup.h): the state's "rotate", 1 for 30 s."""
    return max(LIMITS["kRotateMinMinutes"], clamp_rotate_seconds(seconds) // 60)


def set_rotate(seconds):
    """set_rotate_seconds(): one setting, in both of the state's keys."""
    STATE["rotatesec"] = seconds
    STATE["rotate"] = rotate_minutes_of(seconds)


def parse_team_ref(v):
    """parse_team_ref() plus known_team(): (league, id) of a team in teams.h, else None."""
    lg, colon, tid = v.partition(":")
    # strtoull: leading spaces and a plus sign are fine; a minus sign wraps
    # past 32 bits and is refused.
    m = re.match(r"\s*\+?\d+", tid, re.ASCII)
    ref = (lg, int(m.group())) if colon and lg in LEAGUES and m else None
    return ref if ref in TEAMS else None


def parse_hhmm(v):
    """idle.h's parse_hhmm(): "22:00" or "7:05" to minutes after midnight, -1 for anything else."""
    m = re.fullmatch(r"([0-9]{1,2}):([0-9]{2})", v)
    if not m or int(m[1]) > 23 or int(m[2]) > 59:
        return -1
    return int(m[1]) * 60 + int(m[2])


def timezone_index(v):
    if v.isascii() and v.isdigit():
        return int(v) if int(v) < len(TZ_ROWS) else -1
    return next((i for i, (name, _posix, iana) in enumerate(TZ_ROWS) if v in (name, iana)), -1) if v else -1


def live_auto_leagues():
    """live_leagues() with a mask of 0: the saved team's league and the favorites'."""
    used = {TEAM_LEAGUE[0]} | {t["l"] for t in STATE["favs"] if t}
    return [lg for lg in LEAGUES if lg in used and lg not in BOARD] or ["nfl"]


SPLIT_NOW = {1: "live", 2: "team", 3: "scores", 4: "clock"}


def split_doc():
    """split.h's split_at and split_effective, and a split_now for the choice: the
    clock for Off on two rows of two and for a second team's game with no team, the
    rotation with split_n games or else the clock. The panel's own split_now also
    follows the night hours and what the main board shows."""
    if "split" not in STATE:
        return
    cols, rows = STATE.get("panels", 1), STATE.get("rows", 1)
    at = "bottom" if (cols, rows) == (2, 2) else "right2" if (cols, rows) == (4, 1) else \
        "right1" if (cols, rows) == (3, 1) else ""
    STATE["split_at"] = at
    show = STATE["split"] if at else 0
    if at == "bottom" and show == 0:
        show = 4
    now = SPLIT_NOW.get(show, "")
    if show == 2 and not STATE.get("splitteam"):
        now = "clock"
    # Game Day or a display the main board isn't showing
    if show >= SPLIT_GAMEDAY:
        d = show - SPLIT_GAMEDAY
        now = "game" if d == 0 and STATE.get("display", 0) != 0 else \
            next((x["key"] for x in STATE.get("displays") or [] if x["id"] == d), "clock") \
            if d != 0 and d != STATE.get("display", 0) else "clock"
    if show == 1 and not STATE.get("split_n"):
        now = "clock"
    # A display at night: the date under the board's clock
    if show > SPLIT_GAMEDAY and now not in ("", "clock") and STATE.get("night_now"):
        now = "date"
    STATE["split_now"] = now
    if show == 1:
        STATE.setdefault("split_n", 0)
    else:
        STATE.pop("split_n", None)


def state_doc():
    """rebuild_state_() works the auto leagues out each time from the team and favorites."""
    if STATE.get("live_auto"):
        STATE["live"] = live_auto_leagues()
    split_doc()
    if "display" in STATE:
        STATE["display_now"] = next((x["key"] for x in STATE.get("displays") or [] if x["id"] == STATE["display"]),
                                    "gameday")
        # main_view(): the night hours' clock over a display
        if STATE.get("night_now") and STATE["display"] != 0:
            STATE["display_now"] = "clock"
    # split_full(): a display drawn beside a second area
    if "full" in STATE:
        STATE["full_now"] = bool(STATE["full"] and STATE.get("split_at") and
                                 STATE.get("display_now") not in (None, "gameday", "clock"))
        if STATE["full_now"]:
            STATE["split_now"] = ""
            STATE.pop("split_n", None)
    if isinstance(STATE.get("flights"), dict):
        # tz_miles(): the US and UK zones are imperial
        tz = TZS[STATE.get("tz", 1)] if 0 <= STATE.get("tz", 1) < len(TZS) else ""
        STATE["flights"]["units"] = "imperial" if tz.startswith(("US ", "UK ")) else "metric"
    if "players" in STATE:
        STATE["players"] = [p for p in PLAYER_SLOTS if p]
    if MARKETS in display_ids():
        STATE["markets"] = markets_doc()
    return STATE


def can_follow():
    """set_follow() (follow.h): the mode put the game on the board itself. Live games
    (and the old live modes 1-3) with a game it picked, not the team's card or the
    finals, or Favorite teams with favorites. My team has nothing to pin, and a game
    that has ended has nothing left to keep up (a pin it already has stays). While
    another game's big play has the board (visit), the game to follow is the mode's
    own, which the mock doesn't keep: visit_follow says whether it can be followed."""
    g = STATE.get("game") or {}
    up = g.get("s", "NOT_FOUND") not in ("NOT_FOUND", "POST") and bool(g.get("id")) and g.get("sport") not in (5, 6)
    if STATE.get("visit"):
        up = bool(STATE.get("visit_follow"))
    if STATE["mode"] == 4:
        return up and any(STATE["favs"])
    return up and STATE["mode"] in (1, 2, 3, 5) and not STATE.get("fallback") and not STATE.get("recap")


def let_go():
    """A new team or mode, any favorites post and new leagues let the pin go."""
    STATE["follow"] = False


def end_visit():
    """end_visit_() and drop_visit_(): another game's big play gives the board back."""
    STATE["visit"] = False
    STATE["visit_follow"] = False


def bet_line(v, spread):
    """parse_bet_line() (bets.h): whole or half points into half points, else None."""
    m = re.fullmatch(r"([+-]?)(\d{1,4})(?:\.([05]))?" if spread else r"()(\d{1,4})(?:\.([05]))?", v, re.ASCII)
    if not m:
        return None
    half = int(m.group(2)) * 2 + (1 if m.group(3) == "5" else 0)
    half = -half if m.group(1) == "-" else half
    if spread:
        return half if abs(half) <= LIMITS["kBetMaxSpreadHalf"] else None
    return half if 1 <= half <= LIMITS["kBetMaxTotalHalf"] else None


def parse_slot(v, top, free_ok):
    """parse_slot() (set_request.h): slot 1 to top, 0 with free_ok."""
    if not re.fullmatch(r"[0-9]{1,2}", v, re.ASCII) or (len(v) == 2 and v[0] == "0"):
        return None
    n = int(v)
    return n if (0 if free_ok else 1) <= n <= top else None


def parse_bet(v):
    """parse_bet() (set_request.h): (slot, league, team id, type, half) or None. Slot 0 is the first empty one."""
    f = v.split(",")
    if len(f) != 5 or parse_slot(f[0], LIMITS["kBetSlots"], True) is None:
        return None
    lg, tid, kind, line = f[1], f[2], f[3], f[4]
    if lg not in LEAGUES or lg in BOARD or not re.fullmatch(r"\d{1,10}", tid, re.ASCII) or (lg, int(tid)) not in TEAMS:
        return None
    if kind == "ml":
        half = 0 if line == "" else None
    elif kind in ("spread", "over", "under"):
        half = bet_line(line, kind == "spread")
    else:
        half = None
    return None if half is None else (int(f[0]), lg, int(tid), kind, half)


def bet_points(half, sign):
    """bet_points() (bets.h): "3.5", "+7"."""
    out = "+" if sign and half > 0 else "-" if half < 0 else ""
    return out + str(abs(half) // 2) + (".5" if abs(half) % 2 else "")


def bet_doc(slot, lg, tid, kind, half):
    """One "bets" entry of rebuild_state_(), for a bet with no game yet."""
    abbr = TEAMS[(lg, tid)][0]
    text = (kind.upper() + " " + bet_points(half, False) if kind in ("over", "under")
            else abbr + " ML" if kind == "ml" else abbr + " " + ("PK" if half == 0 else bet_points(half, True)))
    return {"slot": slot, "l": lg, "id": tid, "abbr": abbr, "type": kind, "half": half, "text": text, "st": "waiting"}


def bet_on_board(b):
    """bets_tick_(): a bet on the board's game takes it, and where it stands."""
    g = STATE.get("game") or {}
    if g.get("l") != b["l"] or b["id"] not in (g.get("ti"), g.get("oi")) or g.get("s") not in ("PRE", "IN", "POST"):
        return b
    ours = b["id"] == g.get("ti")
    us, them = (g.get("ts", 0), g.get("os", 0)) if ours else (g.get("os", 0), g.get("ts", 0))
    b.update({"ev": str(g.get("id", "")), "kick": g.get("kick", 0), "oa": g.get("oa") if ours else g.get("ta")})
    if g["s"] == "PRE":
        b["st"] = "pre"
        return b
    m = (2 * (us - them) + b["half"] if b["type"] == "spread" else us - them if b["type"] == "ml"
         else (2 * (us + them) - b["half"]) * (-1 if b["type"] == "under" else 1))
    words = ("covering", "not_covering", "level") if g["s"] == "IN" else ("won", "lost", "push")
    b.update({"st": words[0] if m > 0 else words[1] if m < 0 else words[2], "ts": us, "os": them})
    return b


def put_bet(slot, doc):
    """The slot's entry, or none: the list stays in slot order. bet_put() (bets.h): with a doc, slot 0 is the
    first empty slot, and with every slot in use nothing changes."""
    if slot == 0 and doc:
        used = {b["slot"] for b in STATE.get("bets", [])}
        slot = next((s for s in range(1, LIMITS["kBetSlots"] + 1) if s not in used), 0)
        if not slot:
            return
        doc["slot"] = slot
    keep = [b for b in STATE.get("bets", []) if b["slot"] != slot]
    STATE["bets"] = sorted(keep + ([doc] if doc else []), key=lambda b: b["slot"])


def parse_prop(v):
    """parse_prop() (set_request.h): (slot, entry) or None. Slot 0 is the first empty one."""
    f = v.split(",")
    slot = parse_slot(f[0], LIMITS["kPropSlots"], True)
    if len(f) != 8 or slot is None:
        return None
    lg, athlete, team_id, name = f[1], espn_id(f[2]), espn_id(f[3]), clean_player_name(f[4])
    if lg not in LEAGUES or lg in BOARD or lg in NOT_ESPN or athlete is None or team_id is None \
            or (lg, team_id) not in TEAMS or not name:
        return None
    stat = PROP_STATS.get((SPORT_OF[lg].lower(), f[5]))
    if stat is None:
        return None
    if stat[1]:
        side, half = ("yes", 0) if f[6] == "yes" and f[7] == "" else (None, None)
    elif f[6] in ("over", "under"):
        side, half = f[6], bet_line(f[7], False)
    else:
        side, half = None, None
    if side is None or half is None:
        return None
    return slot, prop_doc(0, lg, athlete, team_id, name, f[5], side, half)


def prop_doc(slot, lg, athlete, team_id, name, stat, side, half):
    """One "props" entry of rebuild_state_(), for a prop with no game yet."""
    label = PROP_STATS[(SPORT_OF[lg].lower(), stat)][0]
    text = name + " " + label if side == "yes" else name + (" U " if side == "under" else " O ") + \
        bet_points(half, False) + " " + label
    return {"slot": slot, "l": lg, "id": athlete, "team": team_id, "name": name, "stat": stat, "side": side,
            "half": half, "text": text, "st": "waiting"}


def put_prop(slot, doc):
    """prop_put() (props.h): the slot's entry, slot 0 the first empty one, nothing when all are in use."""
    if slot == 0 and doc:
        used = {e["slot"] for e in STATE.get("props", [])}
        slot = next((i for i in range(1, LIMITS["kPropSlots"] + 1) if i not in used), 0)
        if not slot:
            return
    if doc:
        doc["slot"] = slot
    keep = [e for e in STATE.get("props", []) if e["slot"] != slot]
    STATE["props"] = sorted(keep + ([doc] if doc else []), key=lambda e: e["slot"])


def clean_player_name(v):
    """clean_player_name() (set_request.h): capitals, digits, spaces and . ' - only, spaces at either end
    dropped and a run inside made one, cut to kPlayerNameMax and a cut never left ending in a space or
    . ' -; "" refuses it, as does a name with no letter or digit left."""
    out = ""
    for ch in v:
        if "a" <= ch <= "z":
            ch = chr(ord(ch) - 32)
        if not ("A" <= ch <= "Z" or "0" <= ch <= "9" or ch in " .'-"):
            return ""
        if ch != " " or (out and out[-1] != " "):
            out += ch
    out = out.rstrip(" ")
    if len(out) > LIMITS["kPlayerNameMax"]:
        out = out[:LIMITS["kPlayerNameMax"]].rstrip(" .'-")
    return out if re.search(r"[A-Z0-9]", out) else ""


def espn_id(v):
    """parse_espn_id() (set_request.h): 1 to 10 ASCII digits, not 0, under 2^32; else None."""
    if not re.fullmatch(r"[0-9]{1,10}", v, re.ASCII) or not 0 < int(v) <= 0xFFFFFFFF:
        return None
    return int(v)


def parse_player(v, delete):
    """parse_player() (set_request.h): {l, id, team, name} for player=, (league, id) for playerdel=, else None."""
    f = v.split(",")
    if len(f) != (2 if delete else 4) or f[0] not in LEAGUES or f[0] in BOARD or f[0] in NOT_ESPN \
            or espn_id(f[1]) is None:
        return None
    if delete:
        return (f[0], espn_id(f[1]))
    team_id = espn_id(f[2])
    name = clean_player_name(f[3])
    if team_id is None or (f[0], team_id) not in TEAMS or not name:
        return None
    return {"l": f[0], "id": espn_id(f[1]), "team": team_id, "name": name}


def put_player(doc):
    """player_put() (players.h): the same league and id keeps its slot, anyone else the first empty one,
    and with every slot in use nothing changes."""
    for i, p in enumerate(PLAYER_SLOTS):
        if p and (p["l"], p["id"]) == (doc["l"], doc["id"]):
            PLAYER_SLOTS[i] = doc
            return
    if None in PLAYER_SLOTS:
        PLAYER_SLOTS[PLAYER_SLOTS.index(None)] = doc


def drop_player(lg, pid):
    """player_drop() (players.h)."""
    for i, p in enumerate(PLAYER_SLOTS):
        if p and (p["l"], p["id"]) == (lg, pid):
            PLAYER_SLOTS[i] = None


def set_pairs(q):
    """handle_set_(): kSetKeys, then each display's own keys, in order."""
    return [(k, q[k]) for k in set_keys() if k in q]


def apply_set(kv):
    """GamedayComponent::apply_set_ through set_request.h's parse_set."""
    for k, v in kv:
        if k == "follow":
            if v != "1":
                let_go()
            elif can_follow():
                STATE["follow"] = True
                end_visit()
        elif k == "team":
            ref = parse_team_ref(v)
            if ref:
                if (STATE["team"].get("l"), STATE["team"].get("id")) != ref:
                    let_go()
                    end_visit()
                STATE["team"] = team(*ref)
                TEAM_LEAGUE[0] = ref[0]
            elif v == "none":
                if STATE["team"]:
                    let_go()
                    end_visit()
                STATE["team"] = {}
        elif k == "mode":
            if 0 <= atoi(v) < LIMITS["kModeCount"]:
                if atoi(v) != STATE["mode"]:
                    let_go()
                    end_visit()
                STATE["mode"] = atoi(v)
        elif k == "rotate":
            # Whole minutes; at least 1, never 30 s
            set_rotate(clamp(atoi(v), "kRotateMinMinutes", "kRotateMaxMinutes") * 60)
        elif k == "rotatesec":
            set_rotate(clamp_rotate_seconds(atoi(v)))
        elif k == "lockon":
            STATE["lockon"] = clamp(atoi(v), "kLockonMinMinutes", "kLockonMaxMinutes")
        elif k == "release":
            STATE["release"] = clamp(atoi(v), "kReleaseMinSeconds", "kReleaseMaxSeconds")
        elif k == "collide":
            STATE["collide"] = 1 if v == "1" else 0
        elif k == "favs":
            refs = []
            for part in v.split(",") if v else []:
                if len(refs) >= FAV_MAX:
                    break
                ref = parse_team_ref(part)
                if ref and ref[0] not in BOARD and ref not in refs:
                    refs.append(ref)
            # set_favorites_(): the list in order, then empty slots up to the four remote slots.
            STATE["favs"] = [team(*r) for r in refs] + [{}] * (4 - len(refs))
            let_go()
        elif k in ("fav1", "fav2", "fav3", "fav4"):
            ref = parse_team_ref(v)
            if ref and ref[0] in BOARD:
                continue
            if ref is None and v not in ("none", ""):
                continue
            STATE["favs"][int(k[3]) - 1] = team(*ref) if ref else {}
            let_go()
        elif k == "today":
            STATE["today"] = v == "1"
        elif k == "live":
            keys = [lg for lg in LEAGUES if lg in v.split(",") and lg not in BOARD]
            if keys != ([] if STATE.get("live_auto") else STATE["live"]):
                let_go()
            STATE["live_auto"] = not keys
            STATE["live"] = keys
        elif k == "tz":
            i = timezone_index(v)
            if i >= 0:
                STATE["tz"], STATE["tz_name"] = i, TZS[i]
        elif k == "tzauto":
            STATE["tz_auto"] = v == "1"
        elif k in ("down", "count", "match", "play", "odds", "opp", "bootaddr", "autoupd", "beta", "alertfav", "anim", "watch",
                   "watchmine", "full"):
            STATE[k] = v == "1"
            if k == "watch" and v != "1":
                end_visit()
        elif k == "alerts":
            # Refuse unknown bits, negatives, and non-numbers (atoi gives 0)
            if re.match(r"\s*[+-]?\d", v, re.ASCII) and 0 <= atoi(v) < 1 << LIMITS["kAlertBits"]:
                STATE["alerts"] = atoi(v)
        elif k == "betdel":
            slot = parse_slot(v, LIMITS["kBetSlots"], False)
            if slot is not None:
                put_bet(slot, None)
        elif k == "bet":
            bet = parse_bet(v)
            if bet:
                put_bet(bet[0], bet_on_board(bet_doc(*bet)))
        elif k == "propdel":
            slot = parse_slot(v, LIMITS["kPropSlots"], False)
            if slot is not None:
                put_prop(slot, None)
        elif k == "prop":
            got = parse_prop(v)
            if got:
                put_prop(*got)
        elif k == "watchstay":
            # Clamped, then kept in tens of seconds (clamp_watch_stay_seconds).
            STATE["watchstay"] = clamp(atoi(v), "kWatchStayMinSeconds", "kWatchStayMaxSeconds") // 10 * 10
        elif k == "playerdel":
            ref = parse_player(v, True)
            if ref:
                drop_player(*ref)
        elif k == "player":
            doc = parse_player(v, False)
            if doc:
                put_player(doc)
        elif k == "panels":
            # "3" or "2x2"; each number a byte
            cols, x, high = v.partition("x")
            cols, rows = atoi(cols), atoi(high) if x else 1
            if 1 <= cols <= 255 and 1 <= rows <= 255 and layout_ok(cols, rows):
                STATE["panels"], STATE["rows"] = cols, rows
        elif k == "wiring":
            if 0 <= atoi(v) < WIRING_COUNT:
                STATE["wiring"] = atoi(v)
        elif k == "split":
            # One ASCII digit; never a display the firmware lacks.
            if re.fullmatch(r"[0-9]", v, re.ASCII) and int(v) < LIMITS["kSplitCount"] and \
                    (int(v) < SPLIT_GAMEDAY or int(v) - SPLIT_GAMEDAY in display_ids()):
                STATE["split"] = int(v)
        elif k == "display":
            # One ASCII digit, a display the firmware has.
            if re.fullmatch(r"[0-9]", v, re.ASCII) and int(v) < LIMITS["kDisplayCount"] and int(v) in display_ids():
                STATE["display"] = int(v)
        elif k == "splitteam":
            # Never a golf tour, racing series or own team
            ref = parse_team_ref(v)
            if v in ("none", ""):
                STATE["splitteam"] = {}
            elif ref and ref[0] not in BOARD and (STATE["team"].get("l"), STATE["team"].get("id")) != ref:
                STATE["splitteam"] = team(*ref)
        elif k == "standing":
            if 0 <= atoi(v) < LIMITS["kStandingCount"]:
                STATE["standing"] = atoi(v)
        elif k in ("clocksize", "textsize"):
            # One ASCII digit, as for split
            if re.fullmatch(r"[0-9]", v, re.ASCII) and int(v) < LIMITS["kSizeCount"]:
                STATE[k] = int(v)
        elif k == "idle":
            if 0 <= atoi(v) < LIMITS["kIdleCount"]:
                STATE["idle"] = atoi(v)
        elif k == "idledim":
            STATE["idledim"] = clamp(atoi(v), "kIdleDimMin", "kIdleDimMax")
        elif k == "night":
            STATE["night"] = v == "1"
        elif k in ("nightstart", "nightend"):
            minute = parse_hhmm(v)
            if minute >= 0:
                STATE[k] = "%02d:%02d" % divmod(minute, 60)
        elif k == "nightbri":
            STATE["nightbri"] = clamp(atoi(v), "kNightBriMin", "kNightBriMax")
        elif k in MARKET_KEYS:
            markets_set(k, v)
        elif k in FLIGHT_SET_KEYS:
            flights_set(k, v)


def selftest():
    """Runs tests/fixtures/set_cases.json through apply_set and state_doc."""
    fx = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def to_team(ref):
        return team(*parse_team_ref(ref)) if ref else {}

    def compact(key, value):
        if key in ("team", "splitteam"):
            return f'{value["l"]}:{value["id"]}' if value else ""
        if key == "favs":
            return [compact("team", t) for t in value]
        if key == "bets":
            # As entered; game and status are the firmware's
            return [{k: b[k] for k in ("slot", "l", "id", "type", "half")} for b in value]
        if key == "props":
            return [{k: e[k] for k in ("slot", "l", "id", "team", "name", "stat", "side", "half")} for e in value]
        if key == "markets":
            # As listed; quotes are the panel's
            return [{k: m[k] for k in ("slot", "sym", "kind")} for m in (value or {}).get("syms", [])]
        return value

    bad = 0
    for case in fx["cases"]:
        start = {**fx["base"], **case.get("start", {})}
        team_league = start.pop("team_league", "nfl")
        start["team"] = to_team(start["team"])
        start["favs"] = [to_team(r) for r in start["favs"]]
        start["splitteam"] = to_team(start.get("splitteam", ""))
        start["displays"] = [display_doc(d) for d in start.get("displays", [0])]
        # board false: nothing to follow; final true: game ended
        if start.get("visit"):
            start["visit_follow"] = start.get("board", True) and not start.get("final", False)
        if start.pop("final", False):
            start["game"] = {**INITIAL["game"], "s": "POST"}
        if not start.pop("board", True):
            start["game"] = {"s": "NOT_FOUND"}
        start["bets"] = [bet_doc(b["slot"], b["l"], b["id"], b["type"], b["half"]) for b in start.get("bets", [])]
        start["props"] = [prop_doc(e["slot"], e["l"], e["id"], e["team"], e["name"], e["stat"], e["side"], e["half"])
                          for e in start.get("props", [])]
        reset_state(start)
        if not STATE["team"]:
            TEAM_LEAGUE[0] = team_league
        kv = [(k, v) for key in set_keys() for k, v in case["set"] if k == key]
        status = 200 if kv else 400
        if kv:
            apply_set(kv)
        got = state_doc()
        if status != case.get("status", 200):
            bad += 1
            print(f"FAIL {case['name']}: status {status}")
        for key, want in case["expect"].items():
            have = compact(key, got.get(key))
            if json.dumps(have) != json.dumps(want):
                bad += 1
                print(f"FAIL {case['name']}: {key} is {json.dumps(have)}, expected {json.dumps(want)}")
    reset_state({})
    print(f"{len(fx['cases'])} set cases, {bad} failures")
    return bad == 0


# The demo build's /gameday/demo (components/gameday/demo.cpp), with the
# stock matchups and made-up teams from demo.h.
DEMO_KEYS = ("do", "kind", "pro_home", "pro_away", "college_home", "college_away", "madeup")
DEMO_INITIAL = {"kind": "off", "madeup": False,
                "picks": {"pro_home": ("nfl", 6), "pro_away": ("nfl", 21),
                          "college_home": ("ncaa", 194), "college_away": ("ncaa", 130)}}
DEMO = copy.deepcopy(DEMO_INITIAL)
MADE_UP = {"pro": ({"abbr": "HBR", "name": "Harbor City Hawks"}, {"abbr": "IRN", "name": "Iron Valley Miners"}),
           "college": ({"abbr": "SUM", "name": "Summit State Peaks"}, {"abbr": "CPT", "name": "Capitol Tech Comets"})}


def demo_doc():
    doc = {"kind": DEMO["kind"], "playing": DEMO["kind"] != "off", "madeup": DEMO["madeup"]}
    for kind in ("pro", "college"):
        side = {}
        for end in ("home", "away"):
            lg, tid = DEMO["picks"][kind + "_" + end]
            abbr, name = TEAMS[(lg, tid)]
            side[end] = {"l": lg, "id": tid, "abbr": abbr, "name": name}
        side["madeup_home"], side["madeup_away"] = MADE_UP[kind]
        doc[kind] = side
    return doc


def apply_demo(q):
    for k in ("pro_home", "pro_away", "college_home", "college_away"):
        if k in q:
            lg, _, tid = q[k].partition(":")
            if (lg, int(tid or 0)) in TEAMS:
                DEMO["picks"][k] = (lg, int(tid))
    if "madeup" in q:
        DEMO["madeup"] = q["madeup"] == "1"
    if q.get("do") == "start":
        DEMO["kind"] = "college" if q.get("kind") == "college" else "pro"
    elif q.get("do") == "stop":
        DEMO["kind"] = "off"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, ctype, body):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _json(self, code, doc):
        self._send(code, "application/json", json.dumps(doc))

    def _gone(self, path):
        """True when the panel is set to not answer this path; the connection closes unanswered."""
        if path in ("/_reset", "/_log"):
            return False
        if "down" in FAIL or ("events" in FAIL and path == "/events"):
            self.close_connection = True
            return True
        return False

    def do_GET(self):
        u = urlparse(self.path)
        if self._gone(u.path):
            return
        if u.path == "/":
            js = "/0.js?demo" if "demo" in parse_qs(u.query, keep_blank_values=True) else "/0.js"
            html = ("<!DOCTYPE html><html><head><meta charset=UTF-8><link rel=icon href=data:>"
                    "<link rel=stylesheet href=/0.css></head><body><script src=" + js + "></script></body></html>")
            return self._send(200, "text/html", html)
        if u.path == "/0.js":
            bundle = "bundle-demo.js" if "demo" in parse_qs(u.query, keep_blank_values=True) else "bundle.js"
            return self._send(200, "text/javascript", (WEB / bundle).read_bytes())
        if u.path == "/gameday/demo":
            return self._json(200, demo_doc())
        if u.path == "/0.css":
            return self._send(200, "text/css", (WEB / "app.css").read_bytes())
        if u.path == "/gameday/state":
            if "state" in FAIL:
                return self._send(500, "text/plain", "")
            return self._json(200, state_doc())
        if u.path == "/update/Firmware":
            return self._json(200, FIRMWARE)
        if u.path == "/gameday/wifi":
            if WIFI["list"] is None:
                return self._json(404, {"error": "unknown route"})
            return self._json(200, {"networks": WIFI["list"], "max": 5})
        if u.path == "/events":
            return self._events()
        if u.path == "/_log":
            return self._json(200, LOG)
        self._send(404, "text/plain", "Not Found")

    def _events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            ping = {"title": "Game Day Scoreboard", "comment": STATE["version"]}
            self.wfile.write(("event: ping\ndata: " + json.dumps(ping) + "\n\n").encode())
            for e in ENTITIES:
                self.wfile.write(("event: state\ndata: " + json.dumps(e) + "\n\n").encode())
            self.wfile.flush()
            for tick in itertools.count():
                time.sleep(0.5)
                if "down" in FAIL or "events" in FAIL:
                    return
                if tick % 10 == 9:
                    self.wfile.write(b"event: ping\ndata: \n\n")
                    self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            return

    def do_POST(self):
        u = urlparse(self.path)
        if self._gone(u.path):
            return
        pairs = parse_qs(u.query, keep_blank_values=True)
        if u.path == "/_reset":
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            LOG.clear()
            reset_state(body.get("state") or {})
            FAIL.clear()
            FAIL.update(body.get("fail") or [])
            DEMO.clear()
            DEMO.update(copy.deepcopy(DEMO_INITIAL))
            WIFI["list"] = body["wifi"] if "wifi" in body else copy.deepcopy(WIFI_INITIAL)
            return self._json(200, {"ok": True})
        if u.path == "/gameday/wifi":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if WIFI["list"] is None:
                return self._json(404, {"error": "unknown route"})
            if not q.get("forget"):
                return self._json(400, {"error": "forget is required"})
            keep = [n for n in WIFI["list"] if n["ssid"] != q["forget"]]
            if len(keep) == len(WIFI["list"]):
                return self._json(404, {"error": "not a saved network"})
            WIFI["list"] = keep
            return self._json(200, {"ok": True})
        if u.path == "/gameday/set":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if "set" in FAIL:
                return self._send(500, "text/plain", "")
            kv = set_pairs(q)
            if not kv:
                return self._json(400, {"error": "nothing to set"})
            apply_set(kv)
            return self._json(200, {"ok": True})
        if u.path == "/gameday/action":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not q.get("do"):
                return self._json(400, {"error": "do is required"})
            if q["do"] not in ("refresh", "qc", "power_ok", "anim"):
                return self._json(404, {"error": "unknown action"})
            if q["do"] == "power_ok" and STATE.get("health"):
                STATE["health"]["power"] = "ok"
            return self._json(200, {"ok": True})
        if u.path == "/gameday/demo":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not any(k in q for k in DEMO_KEYS):
                return self._json(400, {"error": "nothing to do"})
            apply_demo(q)
            return self._json(200, {"ok": True})
        if u.path.startswith("/gameday/"):
            return self._json(404, {"error": "unknown route"})
        if u.path.split("/")[1] in ("switch", "number", "select", "button", "update"):
            return self._send(200, "text/plain", "")
        self._send(404, "text/plain", "Not Found")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(0 if selftest() else 1)
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
