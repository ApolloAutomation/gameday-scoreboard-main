"""Mock of the scoreboard's web server for the device page tests.

Serves firmware/web/bundle.js and app.css at /0.js and /0.css (where ESPHome's
web_server puts js_include and css_include), the ESPHome /events stream for
the entities the page builds controls from, and the gameday component's
routes with the response shapes from components/gameday/gameday.cpp:

  GET  /gameday/state    the document rebuild_state_() writes
  POST /gameday/set      only the keys in SET_KEYS; 400 when none match
  POST /gameday/action   400 without do=, 404 for a name the firmware does not know;
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
components/gameday/set_request.h, and the bet limits from bets.h. `python mock_device.py --selftest` runs
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
                                             + (REPO / "components/gameday/bets.h").read_text(encoding="utf-8"))}
FAV_MAX = int(re.search(r"FAV_MAX = (\d+);", (REPO / "components/gameday/gameday.h").read_text(encoding="utf-8"))[1])
# PanelLayout refuses a count past this (gameday-common.yaml).
MAX_COLS = int(re.search(r"^\s*max_cols: (\d+)", (REPO / "firmware/gameday-common.yaml").read_text(encoding="utf-8"),
                         re.M)[1])
FIXTURE = REPO / "tests/fixtures/set_cases.json"


def team(lg, tid):
    """put_team(): league, id, abbr and name, or an empty object."""
    if (lg, tid) not in TEAMS:
        return {}
    abbr, name = TEAMS[(lg, tid)]
    return {"l": lg, "id": tid, "abbr": abbr, "name": name}


INITIAL = {
    "name": "gameday-2f6a70", "version": "v1.5.4", "setup": True, "panels": 2,
    "team": team("nfl", 6),
    "mode": 0, "rotate": 1, "lockon": 15, "release": 30, "collide": 0,
    "favs": [team("nfl", 6), team("nfl", 34), team("ncaa", 145), {}],
    "favmax": FAV_MAX, "today": False, "live": ["nfl", "ncaa"], "live_auto": True,
    "tz": 1, "tz_name": "US Central", "tz_auto": True,
    "down": True, "count": True, "match": True, "play": True, "odds": True, "opp": True, "bootaddr": True,
    "autoupd": False, "beta": False, "standing": 0,
    # Big-play alerts (components/gameday/alerts.h): the types switched on, bit (type - 1).
    "alerts": (1 << LIMITS["kAlertBits"]) - 1, "alertfav": False,
    # The big-play watcher (components/gameday/watch.h): stay in seconds.
    "watch": True, "watchstay": 60, "watchmine": False,
    "misses": 0, "captive": False, "stale_s": 4, "fallback": False, "recap": False, "follow": False,
    # Another game's big play has the board for a moment (watch.h), and
    # whether Follow this game could still follow the mode's own game then.
    "visit": False, "visit_follow": False,
    # The idle display and the night hours (components/gameday/idle.h), and
    # what they do now: idle_view "" (the mode's board), "clock", "scores" or
    # "today"; night_now inside the night hours; dark while Off has it dark.
    "idle": 0, "idledim": 0, "night": False, "nightstart": "22:00", "nightend": "07:00", "nightbri": 20,
    "idle_view": "", "night_now": False, "dark": False,
    "espn_kb": 412, "espn_reads": 23,
    # Manual bets (components/gameday/bets.h), filled slots only, and how many fit.
    "bets": [], "betmax": LIMITS["kBetSlots"],
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


def reset_state(overrides):
    STATE.clear()
    STATE.update(copy.deepcopy(INITIAL))
    STATE.update(overrides)
    # A null override drops the key, like firmware that doesn't send it.
    for k in [k for k, v in STATE.items() if v is None]:
        del STATE[k]
    TEAM_LEAGUE[0] = (STATE.get("team") or {}).get("l", "nfl")


# The firmware's C library reads ASCII digits and spaces only; Python's \d and
# \s and str.isdigit() also take other scripts' digits, like "\u0663".
def atoi(v):
    m = re.match(r"\s*[+-]?\d+", v, re.ASCII)
    return int(m.group()) if m else 0


def clamp(v, lo, hi):
    return min(LIMITS[hi], max(LIMITS[lo], v))


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


def state_doc():
    """rebuild_state_() works the auto leagues out each time from the team and favorites."""
    if STATE.get("live_auto"):
        STATE["live"] = live_auto_leagues()
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


def parse_bet(v):
    """parse_bet() (set_request.h): (slot, league, team id, type, half) or None. Slot 0 is the first empty one."""
    f = v.split(",")
    if len(f) != 5 or len(f[0]) != 1 or not "0" <= f[0] <= str(LIMITS["kBetSlots"]):
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


def apply_set(kv):
    """GamedayComponent::apply_set_ through set_request.h's parse_set."""
    for k, v in kv:
        if k == "follow":
            if v != "1":
                let_go()
            elif can_follow():
                # The mode's own game, and the board goes straight back to it.
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
            STATE["rotate"] = clamp(atoi(v), "kRotateMinMinutes", "kRotateMaxMinutes")
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
        elif k in ("down", "count", "match", "play", "odds", "opp", "bootaddr", "autoupd", "beta", "alertfav", "watch",
                   "watchmine"):
            STATE[k] = v == "1"
            if k == "watch" and v != "1":
                end_visit()
        elif k == "alerts":
            # A bit past the known types, a negative number or no number at
            # all (which atoi would read as every switch off) is refused.
            if re.match(r"\s*[+-]?\d", v, re.ASCII) and 0 <= atoi(v) < 1 << LIMITS["kAlertBits"]:
                STATE["alerts"] = atoi(v)
        elif k == "betdel":
            if len(v) == 1 and "1" <= v <= str(LIMITS["kBetSlots"]):
                put_bet(int(v), None)
        elif k == "bet":
            bet = parse_bet(v)
            if bet:
                put_bet(bet[0], bet_on_board(bet_doc(*bet)))
        elif k == "watchstay":
            # Clamped, then kept in tens of seconds (clamp_watch_stay_seconds).
            STATE["watchstay"] = clamp(atoi(v), "kWatchStayMinSeconds", "kWatchStayMaxSeconds") // 10 * 10
        elif k == "panels":
            if 1 <= atoi(v) <= MAX_COLS:
                STATE["panels"] = atoi(v)
        elif k == "standing":
            if 0 <= atoi(v) < LIMITS["kStandingCount"]:
                STATE["standing"] = atoi(v)
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


def selftest():
    """Runs tests/fixtures/set_cases.json through apply_set and state_doc."""
    fx = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def to_team(ref):
        return team(*parse_team_ref(ref)) if ref else {}

    def compact(key, value):
        if key == "team":
            return f'{value["l"]}:{value["id"]}' if value else ""
        if key == "favs":
            return [compact("team", t) for t in value]
        if key == "bets":
            # The bet as entered; its game and status are the firmware's.
            return [{k: b[k] for k in ("slot", "l", "id", "type", "half")} for b in value]
        return value

    bad = 0
    for case in fx["cases"]:
        start = {**fx["base"], **case.get("start", {})}
        team_league = start.pop("team_league", "nfl")
        start["team"] = to_team(start["team"])
        start["favs"] = [to_team(r) for r in start["favs"]]
        # board: false stands for a board with nothing of the mode's own to follow,
        # final: true for its game having ended. During a visit that game is the
        # mode's own, the one visit_follow says can be followed.
        if start.get("visit"):
            start["visit_follow"] = start.get("board", True) and not start.get("final", False)
        if start.pop("final", False):
            start["game"] = {**INITIAL["game"], "s": "POST"}
        if not start.pop("board", True):
            start["game"] = {"s": "NOT_FOUND"}
        start["bets"] = [bet_doc(b["slot"], b["l"], b["id"], b["type"], b["half"]) for b in start.get("bets", [])]
        reset_state(start)
        if not STATE["team"]:
            TEAM_LEAGUE[0] = team_league
        kv = [(k, v) for key in SET_KEYS for k, v in case["set"] if k == key]
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
            kv = [(k, q[k]) for k in SET_KEYS if k in q]
            if not kv:
                return self._json(400, {"error": "nothing to set"})
            apply_set(kv)
            return self._json(200, {"ok": True})
        if u.path == "/gameday/action":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not q.get("do"):
                return self._json(400, {"error": "do is required"})
            if q["do"] not in ("refresh", "qc", "power_ok"):
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
