#!/usr/bin/env python3
"""Regenerate components/gameday/teams.h from ESPN.

Run this at release time. The header is committed so firmware builds never
need network access.

    build_teams.py                 rebuild NFL and college football
    build_teams.py --league mlb    rebuild one league (repeatable)

Leagues not named are copied from the current teams.h unchanged, so adding a
sport can never move or rename a football row (Home Assistant's team select
lists teams in this order). Leagues come from kLeagues in leagues.h.

NFL and the other pro leagues come from the league team list. FBS membership is not exposed cleanly
by the teams endpoint (it returns FCS and D3 schools too), so the college
list is collected from a season's worth of FBS scoreboards and filtered by
conference id.

Men's college basketball's team list is Division I only, but it can trail a
school that just finished moving up; see mcbb_teams().

A golf tour (PGA, LPGA) is followed like a team: its one row is the tour
itself, with ESPN's league id as the team id; see golf_tour(). A racing
series (F1, the NASCAR Cup Series) is the same; see racing_series().

Minor league baseball (MILB) is not on ESPN: its teams come from the MLB
Stats API's team list for Triple-A, Double-A, High-A and Single-A, with the
MLB Stats team id as the id and the level's sport id (11 to 14) as the group;
see milb_teams(). --season picks the season (default: this year, or next
year from October, once the next season's teams are listed).

The AHL and the ECHL are not on ESPN either: their teams come from
HockeyTech's feed, the leagues' own stats service, with HockeyTech's team id
as the id, and the feed's site key and client code from the substitutions in
firmware/gameday-common.yaml. Their group says which logo file the panel
asks for; see hockeytech_teams(). --season picks the season id there
(default: the newest regular season).
"""

import argparse
import datetime
import json
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

SITE_ROOT = "https://site.api.espn.com/apis/site/v2/sports"
STANDINGS_ROOT = "https://site.api.espn.com/apis/v2/sports"
D1_GROUP = "50"  # NCAA Division I, the parent of every D1 basketball conference
SITE = f"{SITE_ROOT}/football"
MLB_STATS = "https://statsapi.mlb.com/api/v1"
# Triple-A to Single-A MLB Stats sport ids, kept as group.
MILB_LEVELS = (11, 12, 13, 14)

# ESPN conference group ids that make up the FBS.
FBS_GROUPS = {
    1: "ACC",
    4: "Big 12",
    5: "Big Ten",
    8: "SEC",
    9: "Pac-12",
    12: "Conference USA",
    15: "MAC",
    17: "Mountain West",
    18: "FBS Independents",
    37: "Sun Belt",
    151: "American",
}

COMPONENT = Path(__file__).resolve().parent.parent / "components" / "gameday"
OUT = COMPONENT / "teams.h"
LEAGUES_H = COMPONENT / "leagues.h"
COMMON_YAML = Path(__file__).resolve().parent.parent / "firmware" / "gameday-common.yaml"
HOCKEYTECH = "https://lscluster.hockeytech.com/feed/index.php?feed=modulekit"
HOCKEYTECH_LOGOS = "https://assets.leaguestat.com"
# Logos near the cache's 128 KB cap draw the badge.
HT_NO_SMALL_LOGO = 1 << 16
HT_LOGO_MAX = 120 * 1024

# Fewest teams a fresh list may have before the run is treated as a bad read.
MIN_TEAMS = {"NFL": 32, "NCAA": 120, "MLB": 30, "NBA": 30, "NHL": 32, "WNBA": 13, "MLS": 29, "EPL": 20, "MCBB": 350,
             "PGA": 1, "LPGA": 1, "F1": 1, "NASCAR": 1, "MILB": 120, "AHL": 30, "ECHL": 25}

# A golf tour is followed like a team: one row, ESPN's league id as the id.
# ESPN calls the LPGA "Ladies Pro Golf Association"; the panel says "LPGA Tour".
GOLF_TOURS = {"PGA": "PGA Tour", "LPGA": "LPGA Tour"}

# A racing series is followed the same way: (abbreviation, name). ESPN's own
# abbreviation for the Cup Series is "NASCAR-PREMIER".
RACING_SERIES = {"F1": ("F1", "Formula 1"), "NASCAR": ("NASCAR", "NASCAR Cup Series")}


# ESPN returns 403 to most unfamiliar agents but accepts ones that start with "curl/".
def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "curl/8.0 gameday-scoreboard"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def nfl_teams():
    data = get(f"{SITE}/nfl/teams?limit=100")
    teams = []
    for entry in data["sports"][0]["leagues"][0]["teams"]:
        t = entry["team"]
        teams.append(("NFL", int(t["id"]), t["abbreviation"], t["displayName"], 0))
    return teams


def ncaa_teams():
    found = {}
    for week in range(1, 16):
        url = f"{SITE}/college-football/scoreboard?groups=80&limit=400&week={week}"
        try:
            data = get(url)
        except Exception as ex:  # a missing week is not fatal
            print(f"week {week}: {ex}", file=sys.stderr)
            continue
        for ev in data.get("events", []):
            for comp in ev["competitions"][0]["competitors"]:
                t = comp["team"]
                conf = int(t.get("conferenceId", 0) or 0)
                if conf not in FBS_GROUPS:
                    continue
                found[int(t["id"])] = ("NCAA", int(t["id"]), t["abbreviation"], t["displayName"], conf)
    return list(found.values())


def pro_teams(league, path):
    data = get(f"{SITE_ROOT}/{path}/teams?limit=1000")
    teams = []
    for entry in data["sports"][0]["leagues"][0]["teams"]:
        t = entry["team"]
        teams.append((league, int(t["id"]), t["abbreviation"], t["displayName"], 0))
    return teams


def mcbb_teams(path):
    """ESPN's men's college basketball list holds only Division I teams but
    can miss a school that just finished moving up. The D1 standings (one
    table per conference) name every member. A team in the standings but not
    in the list is kept only when its team endpoint places it under Division
    I, so a school that has since left D1 stays out."""
    found = {row[1]: row for row in pro_teams("MCBB", path)}
    standings = get(f"{STANDINGS_ROOT}/{path}/standings?group={D1_GROUP}")
    for conf in standings.get("children", []):
        for entry in conf.get("standings", {}).get("entries", []):
            tid = int(entry["team"]["id"])
            if tid in found:
                continue
            t = get(f"{SITE_ROOT}/{path}/teams/{tid}")["team"]
            parent = ((t.get("groups") or {}).get("parent") or {}).get("id")
            if str(parent) == D1_GROUP:
                print(f"MCBB: adding {t['displayName']} ({tid}), missing from the team list")
                found[tid] = ("MCBB", tid, t["abbreviation"], t["displayName"], 0)
    return list(found.values())


def golf_tour(lg, path):
    slug = path.split("/")[1]
    data = get(f"https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league={slug}")
    league = data["sports"][0]["leagues"][0]
    return [(lg, int(league["id"]), league["abbreviation"], GOLF_TOURS[lg], 0)]


def racing_series(lg, path):
    slug = path.split("/")[1]
    data = get(f"https://site.api.espn.com/apis/v2/scoreboard/header?sport=racing&league={slug}")
    league = data["sports"][0]["leagues"][0]
    abbr, name = RACING_SERIES[lg]
    return [(lg, int(league["id"]), abbr, name, 0)]


def milb_teams(season):
    """Every active team at the four full-season levels, from the MLB Stats
    API (the panel reads their games there too, source_mlbstats.cpp). The
    group is the level's sport id, which the panel's far read asks for."""
    data = get(f"{MLB_STATS}/teams?sportIds={','.join(map(str, MILB_LEVELS))}&season={season}")
    teams = []
    for t in data.get("teams", []):
        level = int((t.get("sport") or {}).get("id", 0))
        if level not in MILB_LEVELS or not t.get("active", True):
            continue
        teams.append(("MILB", int(t["id"]), t["abbreviation"], t["name"], level))
    return teams


def milb_season(asked):
    """The season to list: the one asked for, else this year, or from October
    next year's when it is already listed in full."""
    if asked:
        return asked
    today = datetime.date.today()
    if today.month >= 10:
        try:
            if len(milb_teams(today.year + 1)) >= MIN_TEAMS["MILB"]:
                return today.year + 1
        except Exception as ex:  # next season not listed yet: use this one
            print(f"MILB {today.year + 1}: {ex}", file=sys.stderr)
    return today.year


def hockeytech_feed(lg):
    """The league's site key and client code, from the substitutions the
    firmware uses (hockeytech_ahl_key, hockeytech_ahl_client)."""
    text = COMMON_YAML.read_text(encoding="utf-8")
    found = {}
    for part in ("key", "client"):
        m = re.search(rf'^\s*hockeytech_{lg.lower()}_{part}:\s*"([^"]+)"', text, re.M)
        if not m:
            sys.exit(f"gameday-common.yaml has no hockeytech_{lg.lower()}_{part}")
        found[part] = m.group(1)
    return found["key"], found["client"]


def small_logo_size(url):
    """Bytes in a logo file, 0 when the server has none."""
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "curl/8.0 gameday-scoreboard"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return int(r.headers.get("Content-Length") or 0)
    except urllib.error.HTTPError as ex:
        if ex.code == 404:
            return 0
        raise


def hockeytech_teams(lg, season):
    """Every team in the league's season (the newest regular season unless
    one is asked for), from HockeyTech's feed (the panel reads their games
    there too, source_hockeytech.cpp). The group is the season the team's
    logo file is named for (402_94.png: 94; 0 for 440.png), plus
    HT_NO_SMALL_LOGO when the 50 px copy the panel asks for is missing or
    too large; the panel draws that team's badge."""
    key, client = hockeytech_feed(lg)
    feed = f"{HOCKEYTECH}&key={key}&client_code={client}&fmt=json&lang=en"
    if not season:
        seasons = get(f"{feed}&view=seasons")["SiteKit"]["Seasons"]
        regular = [x for x in seasons if x.get("playoff") == "0" and x.get("career") == "1"]
        if not regular:
            sys.exit(f"{lg}: no regular season listed")
        season = int(regular[0]["season_id"])
    print(f"{lg}: season {season}")
    teams = []
    for t in get(f"{feed}&view=teamsbyseason&season_id={season}")["SiteKit"]["Teamsbyseason"]:
        tid = int(t["id"])
        file = (t.get("team_logo_url") or "").rsplit("/", 1)[-1]
        m = re.fullmatch(rf"{tid}(?:_(\d+))?\.png", file)
        group = int(m.group(1)) if m and m.group(1) else 0
        size = small_logo_size(f"{HOCKEYTECH_LOGOS}/{client}/logos/50x50/{file or f'{tid}.png'}")
        if not m or size == 0 or size > HT_LOGO_MAX:
            print(f"{lg}: {t['name']} has no logo small enough ({size} bytes): the panel draws its badge")
            group |= HT_NO_SMALL_LOGO
        teams.append((lg, tid, t["code"], t["name"], group))
    return teams


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def unescape(s):
    return re.sub(r"\\(.)", r"\1", s)


def league_table():
    """(enum name, ESPN path) in kLeagues order."""
    text = LEAGUES_H.read_text(encoding="utf-8")
    return re.findall(r'\{League::(\w+), "\w+", "([^"]+)",', text)


def current_rows():
    if not OUT.exists():
        return []
    rows = re.findall(r'\{League::(\w+), (\d+), "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)", (\d+)\}',
                      OUT.read_text(encoding="utf-8"))
    return [(lg, int(tid), unescape(a), unescape(n), int(g)) for lg, tid, a, n, g in rows]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--league", action="append", default=[], help="league to rebuild, e.g. mlb (repeatable)")
    ap.add_argument("--rewrite", action="store_true", help="rebuild nothing, only re-emit the header")
    ap.add_argument("--season", type=int, default=0,
                    help="MILB: the season whose teams to list; AHL, ECHL: the season id")
    args = ap.parse_args()
    table = league_table()
    paths = dict(table)
    rebuild = [] if args.rewrite else [x.upper() for x in args.league] or ["NFL", "NCAA"]
    for lg in rebuild:
        if lg not in paths:
            sys.exit(f"{lg} is not in kLeagues (leagues.h)")

    by_league = {}
    for row in current_rows():
        by_league.setdefault(row[0], []).append(row)
    for lg in rebuild:
        if lg == "NFL":
            fresh = nfl_teams()
        elif lg == "NCAA":
            fresh = ncaa_teams()
        elif lg == "MCBB":
            fresh = mcbb_teams(paths[lg])
        elif paths[lg].startswith("golf/"):
            fresh = golf_tour(lg, paths[lg])
        elif paths[lg].startswith("racing/"):
            fresh = racing_series(lg, paths[lg])
        elif lg == "MILB":
            season = milb_season(args.season)
            print(f"MILB: season {season}")
            fresh = milb_teams(season)
        elif paths[lg].startswith("hockeytech/"):
            fresh = hockeytech_teams(lg, args.season)
        else:
            fresh = pro_teams(lg, paths[lg])
        print(f"{lg}: {len(fresh)} teams")
        if len(fresh) < MIN_TEAMS.get(lg, 1):
            sys.exit(f"expected at least {MIN_TEAMS[lg]} {lg} teams; is it the off season?")
        by_league[lg] = sorted(fresh, key=lambda r: r[3])

    lines = [
        "// Generated by scripts/build_teams.py. Do not edit by hand.",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        '#include "leagues.h"',
        "",
        "namespace espn {",
        "",
        "struct Team {",
        "  League league;",
        "  uint32_t espn_id;",
        "  const char *abbr;",
        "  const char *name;",
        "  // college: the conference group id for the scoreboard; minor league",
        "  // baseball: the level's MLB Stats sport id (11 Triple-A to 14 Single-A);",
        "  // the AHL and the ECHL: the season their logo file is named for, plus",
        "  // kHtNoSmallLogo when the panel draws their badge (source_hockeytech.h);",
        "  // 0 otherwise",
        "  uint32_t group;",
        "};",
        "",
        "// inline: one copy in the firmware, however many files read it (each",
        "// would otherwise keep its own, about 17 KB of flash).",
        "inline constexpr Team kTeams[] = {",
    ]
    for lg, _path in table:
        for league, tid, abbr, name, group in by_league.get(lg, []):
            lines.append(f"    {{League::{league}, {tid}, {cstr(abbr)}, {cstr(name)}, {group}}},")
    lines += [
        "};",
        "",
        "constexpr size_t kTeamCount = sizeof(kTeams) / sizeof(kTeams[0]);",
        "",
        "}  // namespace espn",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
