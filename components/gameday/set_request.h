// The device page's POST /gameday/set: which keys it takes, what each value
// means and where it is clamped or refused, and the leagues each live mode
// scans.
// Pure, no ESPHome includes, so the host tests cover it and check the same
// table as tests/web/mock_device.py (tests/fixtures/set_cases.json).
#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../panel_layout/layouts.h"
#include "bets.h"
#include "idle.h"
#include "leagues.h"
#include "startup.h"
#include "teams.h"
#include "timezones.h"

namespace espn {

// In the order the firmware applies them, whatever order the request uses.
// follow comes first: it pins the game the page showed, and a change later in
// the same request that moves the board lets the pin go again. rotatesec
// comes after rotate, so a request with both keeps rotatesec's. wiring comes
// right after panels, so a request that picks the 2x2 and its wiring saves
// both before the restart either one asks for. betdel comes before bet, so a
// request with both empties a slot and then fills it, and playerdel before
// player the same way. splitteam comes before split, so a request that picks
// A second team's game and its team has the team first.
constexpr const char *kSetKeys[] = {"follow",    "team",       "mode",      "rotate",   "rotatesec",  "fav1",
                                    "fav2",      "fav3",       "fav4",      "tz",       "tzauto",     "down",
                                    "play",      "odds",       "opp",       "panels",   "wiring",     "bootaddr",
                                    "lockon",    "release",    "collide",   "favs",     "today",      "live",
                                    "autoupd",   "count",      "match",     "beta",     "alerts",     "alertfav",
                                    "watch",     "watchstay",  "watchmine", "standing", "clocksize",  "textsize",
                                    "idle",      "idledim",    "night",     "nightstart", "nightend", "nightbri",
                                    "betdel",    "bet",        "playerdel", "player",   "splitteam",  "split"};

// LIVE (5) follows live games in the leagues of the live mask; modes 1-3 are
// the football-only live modes from before, kept for panels that saved them.
enum class Mode : uint8_t { MY_TEAM = 0, LIVE_NFL = 1, LIVE_NCAA = 2, LIVE_ANY = 3, FAVORITES = 4, LIVE = 5 };

// Mode 0 (My team) to 5 (Live games).
constexpr int kModeCount = 6;
static_assert((int) Mode::LIVE + 1 == kModeCount, "parse_set takes modes 0 to kModeCount - 1");

// What the second area shows (split.h), kept as a byte: OFF, Rotating live
// games, A second team's game, Today's scores, Clock and date.
enum class SplitShow : uint8_t { OFF = 0, LIVE = 1, TEAM = 2, SCORES = 3, CLOCK = 4 };
constexpr int kSplitCount = 5;

// Clock size and Text size: 0 Normal, 1 Large (board_layout.h, Sizes).
constexpr int kSizeCount = 2;

// Standing next to pro teams: 0 Record, 1 Seed, 2 Place (espn::Standing).
constexpr int kStandingCount = 3;
static_assert((int) Standing::PLACE + 1 == kStandingCount, "parse_set takes standings 0 to kStandingCount - 1");

constexpr int kLockonMinMinutes = 5;
constexpr int kLockonMaxMinutes = 120;
constexpr int kReleaseMinSeconds = 30;
constexpr int kReleaseMaxSeconds = 3600;
// Big-play alert types (alerts.h), one switch each: "alerts" is the mask of
// the ones on, bit (type - 1).
constexpr int kAlertBits = 20;
// "Stay on a big play" (watch.h), in seconds, kept in tens of seconds.
constexpr int kWatchStayMinSeconds = 30;
constexpr int kWatchStayMaxSeconds = 300;

inline int clamp_watch_stay_seconds(int seconds) {
  const int s = seconds < kWatchStayMinSeconds   ? kWatchStayMinSeconds
                : seconds > kWatchStayMaxSeconds ? kWatchStayMaxSeconds
                                                 : seconds;
  return s / 10 * 10;
}
// Favorite players (players.h): how many the panel keeps, and the longest
// name it shows for one, a line of the splash on one panel.
constexpr int kPlayerSlots = 8;
constexpr int kPlayerNameMax = 10;

inline int clamp_lockon_minutes(int minutes) {
  return minutes < kLockonMinMinutes ? kLockonMinMinutes : minutes > kLockonMaxMinutes ? kLockonMaxMinutes : minutes;
}

inline int clamp_release_seconds(int seconds) {
  return seconds < kReleaseMinSeconds ? kReleaseMinSeconds : seconds > kReleaseMaxSeconds ? kReleaseMaxSeconds : seconds;
}

// The idle display's dim level (0 dark) and the night brightness, out of 255.
constexpr int kIdleDimMin = 0;
constexpr int kIdleDimMax = 255;
constexpr int kNightBriMin = 1;
constexpr int kNightBriMax = 255;

inline int clamp_idle_dim(int level) { return level < kIdleDimMin ? kIdleDimMin : level > kIdleDimMax ? kIdleDimMax : level; }

inline int clamp_night_bri(int level) {
  return level < kNightBriMin ? kNightBriMin : level > kNightBriMax ? kNightBriMax : level;
}

// A number read the way atoi reads one ("12abc" is 12, "abc" and "" are 0),
// except that a value past int's range stops at its end. atoi leaves that
// undefined: the panel's 32-bit library stops at the end, a 64-bit host wraps
// "4294967296" to 0, so the host tests would check another panel than ours.
inline int read_int(const std::string &v) {
  long n = strtol(v.c_str(), nullptr, 10);
  return n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int) n;
}

// "nfl:6" -> league + id. False for "none"/"" (id 0) or garbage. An id past
// 32 bits is garbage too, not wrapped to a small one (on the panel strtoul
// stops at 32 bits; a 64-bit host would wrap "4294967302" to 6).
inline bool parse_team_ref(const std::string &v, uint8_t &league, uint32_t &id) {
  league = 0;
  id = 0;
  size_t colon = v.find(':');
  if (colon == std::string::npos)
    return false;
  std::string lg = v.substr(0, colon);
  unsigned long long n = strtoull(v.c_str() + colon + 1, nullptr, 10);
  id = n > UINT32_MAX ? 0 : (uint32_t) n;
  const LeagueInfo *info = league_by_key(lg.c_str());
  if (info == nullptr)
    return false;
  league = (uint8_t) info->league;
  return id != 0;
}

inline bool known_team(uint8_t league, uint32_t id) {
  for (size_t t = 0; t < kTeamCount; t++)
    if ((uint8_t) kTeams[t].league == league && kTeams[t].espn_id == id)
      return true;
  return false;
}

// "mlb,nhl" -> a bit per league. Unknown keys are skipped, and so are the golf
// tours and racing series (Live games has neither), so a list of only those,
// like "", is 0: back to auto.
inline uint16_t live_mask_from_keys(const std::string &keys) {
  uint16_t mask = 0;
  size_t start = 0;
  while (start < keys.size()) {
    size_t comma = keys.find(',', start);
    std::string key = keys.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    start = comma == std::string::npos ? keys.size() : comma + 1;
    const LeagueInfo *info = league_by_key(key.c_str());
    if (info != nullptr && !is_board_sport(info->sport))
      mask |= (uint16_t) (1u << (uint8_t) info->league);
  }
  return mask;
}

// The leagues Live mode scans, in kLeagues order. A mask of 0 is auto: the
// saved team's league and each favorite's. team_league is the league byte
// the panel keeps, which stays after the team is cleared. Golf and racing are
// never scanned: a tour or a series followed as the team adds nothing. NFL
// when nothing is left, unless nfl_default is false: the idle display's
// scores then have no league and show the clock instead.
inline std::vector<League> live_leagues(uint16_t mask, uint8_t team_league, const std::vector<uint8_t> &fav_leagues,
                                        bool nfl_default = true) {
  if (mask == 0) {
    if (team_league < 16 && !is_leaderboard((League) team_league))
      mask |= (uint16_t) (1u << team_league);
    for (uint8_t l : fav_leagues)
      if (l < 16 && !is_leaderboard((League) l))
        mask |= (uint16_t) (1u << l);
  }
  std::vector<League> out;
  for (const auto &l : kLeagues)
    if (!is_board_sport(l.sport) && (mask & (1u << (uint8_t) l.league)))
      out.push_back(l.league);
  if (out.empty() && nfl_default)
    out.push_back(League::NFL);
  return out;
}

// The leagues a mode's scan reads, in the order it reads them, and so the
// leagues its finals rotation covers. live is Live games' own list
// (live_leagues above). Live anything is also the no-team default. A mode
// that follows a team scans none, unless the idle display shows the scores
// rotation (idle_scores, idle.h): then live, which for those modes is the
// leagues of the team and favorites, or Live games' own list.
inline std::vector<League> scan_leagues(uint8_t mode, const std::vector<League> &live, bool idle_scores = false) {
  switch ((Mode) mode) {
    case Mode::LIVE_NFL:
      return {League::NFL};
    case Mode::LIVE_NCAA:
      return {League::NCAA};
    case Mode::LIVE_ANY:
      return {League::NFL, League::NCAA};
    case Mode::LIVE:
      return live;
    case Mode::MY_TEAM:
    case Mode::FAVORITES:
      return idle_scores ? live : std::vector<League>{};
    default:
      return {};
  }
}

// Live games' leagues went from before to after (its list, or with the list
// on auto the team or favorites it follows). True when the scan now reads
// other leagues, so the board has to drop what the old ones put up and scan
// again; a mode that doesn't read the list never does.
inline bool live_scan_changed(uint8_t mode, const std::vector<League> &before, const std::vector<League> &after) {
  return scan_leagues(mode, before) != scan_leagues(mode, after);
}

enum class SetKind : uint8_t {
  TEAM,      // team, id 0 clears it
  MODE,      // value
  ROTATE,    // value, seconds: 30, or 60 to 1800 in whole minutes (startup.h)
  FAV_SLOT,  // slot (1-4), team (id 0 clears the slot)
  TZ,        // value, index into kTimezones
  TZ_AUTO,   // on
  DOWN,      // on, football
  PLAY,      // on
  ODDS,      // on
  OPP,       // on
  PANELS,    // value (panels wide) and rows, 1-255 each; PanelLayout refuses an arrangement it can't drive
  WIRING,    // value, the 2x2's wiring (panel_layouts::Wiring)
  BOOTADDR,  // on
  LOCKON,    // value, minutes
  RELEASE,   // value, seconds
  COLLIDE,   // on
  FAVS,      // teams, in priority order
  TODAY,     // on
  LIVE,      // mask, 0 = auto
  AUTOUPD,   // on
  COUNT,     // on, baseball
  MATCH,     // on, soccer
  BETA,      // on, release builds only
  FOLLOW,    // on: pin the game on the board (follow.h); memory only
  ALERTS,    // mask, the alert types switched on
  ALERTFAV,  // on, alerts only for games with the team or a favorite
  STANDING,  // value, espn::Standing
  IDLE,         // value, idle.h's IdleShow
  IDLE_DIM,     // value, 0 (dark) to 255
  NIGHT,        // on: the night hours apply
  NIGHT_START,  // value, minutes after local midnight
  NIGHT_END,    // value, minutes after local midnight
  NIGHT_BRI,    // value, 1 to 255
  BET,          // slot (1-kBetSlots), bet: a manual bet into that slot (bets.h)
  BET_DEL,      // slot: empties it
  WATCH,        // on, switch to big plays in other games
  WATCHSTAY,    // value, seconds on the big play's game
  WATCHMINE,    // on, a favorite's big play can take the board from the team's live game
  PLAYER,       // team (league, team id), athlete, name: a favorite player (players.h)
  PLAYER_DEL,   // team.league, athlete: drops that favorite player
  SPLIT,        // value, split.h's SplitShow: what the second area shows
  SPLIT_TEAM,   // team: the second area's team (id 0 clears it)
  CLOCK_SIZE,   // value, 0 Normal or 1 Large: the clock's digits
  TEXT_SIZE,    // value, 0 Normal or 1 Large: the ticker's letters
};

struct TeamRef {
  uint8_t league{0};
  uint32_t id{0};
  bool operator==(const TeamRef &o) const { return league == o.league && id == o.id; }
};

struct SetOp {
  SetKind kind{SetKind::TEAM};
  int value{0};
  int rows{1};  // PANELS only
  bool on{false};
  uint8_t slot{0};
  uint32_t mask{0};
  TeamRef team;
  std::vector<TeamRef> teams;
  std::vector<std::string> refused;  // favs entries left out, for the log
  Bet bet;                           // BET: what was bet, with no game yet
  uint32_t athlete{0};               // PLAYER, PLAYER_DEL: ESPN's athlete id
  std::string name;                  // PLAYER: the panel's name for the player (clean_player_name)
};

// The panel's name for a favorite player, from what the page sends: capital
// letters, digits, spaces and . ' - only (lower case is raised), spaces at
// either end dropped and a run of them inside made one, cut to
// kPlayerNameMax characters, and a cut never left ending in a space or
// . ' -. "" when a character is anything else, or no letter or digit is
// left, which refuses the request. "Allen" is "ALLEN", "J. Hughes"
// "J. HUGHES", "St. Brown" "ST. BROWN".
inline std::string clean_player_name(const std::string &v) {
  auto alnum = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
  std::string out;
  for (char c : v) {
    if (c >= 'a' && c <= 'z')
      c = (char) (c - 'a' + 'A');
    if (!alnum(c) && c != ' ' && c != '.' && c != '\'' && c != '-')
      return "";
    if (c != ' ' || (!out.empty() && out.back() != ' '))
      out += c;
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  if (out.size() > (size_t) kPlayerNameMax) {
    out.resize(kPlayerNameMax);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.' || out.back() == '\'' || out.back() == '-'))
      out.pop_back();
  }
  for (char c : out)
    if (alnum(c))
      return out;
  return "";
}

// An ESPN id as the page sends it: 1 to 10 digits, nothing else, not 0 and
// under 2^32.
inline bool parse_espn_id(const std::string &v, uint32_t &id) {
  if (v.empty() || v.size() > 10)
    return false;
  uint64_t n = 0;
  for (char c : v) {
    if (c < '0' || c > '9')
      return false;
    n = n * 10 + (uint64_t) (c - '0');
  }
  if (n == 0 || n > UINT32_MAX)
    return false;
  id = (uint32_t) n;
  return true;
}

// The comma-separated fields of v, at most max (false for more).
inline bool split_fields(const std::string &v, std::string *f, size_t max, size_t &n) {
  n = 0;
  size_t start = 0;
  while (true) {
    if (n == max)
      return false;
    size_t comma = v.find(',', start);
    f[n++] = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (comma == std::string::npos)
      return true;
    start = comma + 1;
  }
}

// player=<league>,<athlete id>,<team id>,<name>: "nfl,3918298,2,ALLEN". The
// league one the panel follows teams in (not a golf tour or a racing
// series) and reads from ESPN, whose rosters the page picks players from
// (not minor league baseball, the AHL or the ECHL), the team one in its list,
// the athlete's id ESPN's, the name what clean_player_name keeps.
// playerdel=<league>,<athlete id> drops one. Anything else, extra fields
// included, is refused.
inline bool parse_player(const std::string &v, bool del, SetOp &op) {
  std::string f[4];
  size_t n = 0;
  if (!split_fields(v, f, del ? 2 : 4, n) || n != (del ? 2u : 4u))
    return false;
  const LeagueInfo *info = league_by_key(f[0].c_str());
  if (info == nullptr || is_board_sport(info->sport) || info->source != Source::ESPN ||
      !parse_espn_id(f[1], op.athlete))
    return false;
  op.team.league = (uint8_t) info->league;
  if (del)
    return true;
  if (!parse_espn_id(f[2], op.team.id) || !known_team(op.team.league, op.team.id))
    return false;
  op.name = clean_player_name(f[3]);
  return !op.name.empty();
}

// A bet slot as the page sends it: one digit, 1 to kBetSlots, or with
// free_ok 0 for the first empty slot (bet_put).
inline bool parse_bet_slot(const std::string &v, uint8_t &slot, bool free_ok = false) {
  if (v.size() != 1 || v[0] < (free_ok ? '0' : '1') || v[0] > '0' + kBetSlots)
    return false;
  slot = (uint8_t) (v[0] - '0');
  return true;
}

// bet=<slot>,<league>,<team id>,<type>,<line>: "1,nfl,2,spread,-3.5",
// "2,nfl,2,over,47.5", "3,nba,2,ml," (a moneyline has no line). Slot 0 is
// the first empty slot, which the device page sends; 1 to kBetSlots replaces
// the bet in that slot. The type is
// spread, over, under or ml; the team one in the panel's list, not a golf
// tour or a racing series; the line whole or half points (parse_bet_line).
// Anything else, extra spaces and fields included, is refused.
inline bool parse_bet(const std::string &v, uint8_t &slot, Bet &b) {
  std::string f[5];
  size_t n = 0, start = 0;
  while (n < 5) {
    size_t comma = v.find(',', start);
    f[n++] = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (comma == std::string::npos)
      break;
    start = comma + 1;
    if (n == 5)
      return false;  // a sixth field
  }
  if (n != 5 || !parse_bet_slot(f[0], slot, true))
    return false;
  b = Bet{};
  const LeagueInfo *info = league_by_key(f[1].c_str());
  if (info == nullptr || is_board_sport(info->sport) || f[2].empty() || f[2].size() > 10)
    return false;
  uint64_t id = 0;
  for (char c : f[2]) {
    if (c < '0' || c > '9')
      return false;
    id = id * 10 + (uint64_t) (c - '0');
  }
  if (id == 0 || id > UINT32_MAX || !known_team((uint8_t) info->league, (uint32_t) id))
    return false;
  b.league = (uint8_t) info->league;
  b.team_id = (uint32_t) id;
  const std::string &type = f[3];
  if (type == "ml") {
    b.type = (uint8_t) BetType::MONEYLINE;
    return f[4].empty();
  }
  if (type == "spread") {
    b.type = (uint8_t) BetType::SPREAD;
    return parse_bet_line(f[4], true, b.line);
  }
  if (type != "over" && type != "under")
    return false;
  b.type = (uint8_t) BetType::TOTAL;
  b.side = (uint8_t) (type == "over" ? BetSide::OVER : BetSide::UNDER);
  return parse_bet_line(f[4], false, b.line);
}

// Decodes one key=value from the request. False when the key is unknown or
// the value is refused: the firmware logs it and changes nothing. Switches
// are on for "1" only; numbers read like atoi (read_int), so "abc" is 0.
inline bool parse_set(const std::string &k, const std::string &v, SetOp &op, size_t fav_max) {
  op = SetOp{};
  auto on = [&](SetKind kind) {
    op.kind = kind;
    op.on = v == "1";
    return true;
  };
  if (k == "team") {
    op.kind = SetKind::TEAM;
    if (v == "none")
      return true;
    return parse_team_ref(v, op.team.league, op.team.id) && known_team(op.team.league, op.team.id);
  }
  if (k == "mode") {
    op.kind = SetKind::MODE;
    op.value = read_int(v);
    return op.value >= 0 && op.value < kModeCount;
  }
  if (k == "standing") {
    op.kind = SetKind::STANDING;
    op.value = read_int(v);
    return op.value >= 0 && op.value < kStandingCount;
  }
  // Switch every (Live games) and Alternate every (Favorite teams), one
  // setting. rotate is whole minutes, 1 to 30, as before 30 seconds: 0 and
  // anything lower is still 1 minute, so a page, app or script from before
  // never sets 30 seconds by accident. rotatesec, which nothing from before
  // sends, is seconds: under a minute is 30 seconds, a minute or more whole
  // minutes, rounded down, at most 30. The state document has both: "rotate"
  // in minutes (1 for 30 seconds) and "rotatesec" in seconds.
  if (k == "rotate") {
    op.kind = SetKind::ROTATE;
    op.value = clamp_rotate_minutes(read_int(v)) * 60;
    return true;
  }
  if (k == "rotatesec") {
    op.kind = SetKind::ROTATE;
    op.value = clamp_rotate_seconds(read_int(v));
    return true;
  }
  if (k == "lockon") {
    op.kind = SetKind::LOCKON;
    op.value = clamp_lockon_minutes(read_int(v));
    return true;
  }
  if (k == "release") {
    op.kind = SetKind::RELEASE;
    op.value = clamp_release_seconds(read_int(v));
    return true;
  }
  if (k == "panels") {
    // The panels wide ("2"), or wide by high ("2x2"). PanelLayout takes each
    // as a byte and refuses an arrangement it can't drive; a value that
    // doesn't fit would wrap first (257 to one panel).
    op.kind = SetKind::PANELS;
    op.value = read_int(v);
    const size_t x = v.find('x');
    op.rows = x == std::string::npos ? 1 : read_int(v.substr(x + 1));
    return op.value >= 1 && op.value <= UINT8_MAX && op.rows >= 1 && op.rows <= UINT8_MAX;
  }
  if (k == "wiring") {
    op.kind = SetKind::WIRING;
    op.value = read_int(v);
    return op.value >= 0 && op.value < panel_layouts::kWiringCount;
  }
  if (k == "tz") {
    op.kind = SetKind::TZ;
    op.value = timezone_index(v.c_str());
    return op.value >= 0;
  }
  if (k == "live") {
    op.kind = SetKind::LIVE;
    op.mask = live_mask_from_keys(v);
    return true;
  }
  if (k == "alerts") {
    // A bit past the known types is refused, not dropped: the page sends
    // only what it knows. So is a value with no number in it, which atoi
    // would read as 0 and so turn every switch off.
    op.kind = SetKind::ALERTS;
    char *end = nullptr;
    const long n = strtol(v.c_str(), &end, 10);
    op.mask = (uint32_t) n;
    return end != v.c_str() && n >= 0 && n < (1L << kAlertBits);
  }
  if (k == "favs") {
    // "mlb:15,nfl:6,..." in priority order; "" clears the list. Unknown teams,
    // golf tours and racing series (not a favorite yet) and repeats drop out;
    // the rest keep their order up to fav_max.
    op.kind = SetKind::FAVS;
    size_t start = 0;
    while (start < v.size() && op.teams.size() < fav_max) {
      size_t comma = v.find(',', start);
      std::string ref = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      start = comma == std::string::npos ? v.size() : comma + 1;
      TeamRef t;
      if (!parse_team_ref(ref, t.league, t.id) || !known_team(t.league, t.id) || is_leaderboard((League) t.league)) {
        op.refused.push_back(ref);
        continue;
      }
      bool dup = false;
      for (const auto &f : op.teams)
        dup = dup || f == t;
      if (!dup)
        op.teams.push_back(t);
    }
    return true;
  }
  if (k.size() == 4 && k.compare(0, 3, "fav") == 0 && k[3] >= '1' && k[3] <= '4') {
    // One of the remote's four slots; "none" or "" clears it. A golf tour or
    // a racing series can't be a favorite yet.
    op.kind = SetKind::FAV_SLOT;
    op.slot = (uint8_t) (k[3] - '0');
    if (parse_team_ref(v, op.team.league, op.team.id) && known_team(op.team.league, op.team.id) &&
        !is_leaderboard((League) op.team.league))
      return true;
    op.team = TeamRef{};
    return v == "none" || v.empty();
  }
  if (k == "tzauto")
    return on(SetKind::TZ_AUTO);
  if (k == "down")
    return on(SetKind::DOWN);
  if (k == "count")
    return on(SetKind::COUNT);
  if (k == "match")
    return on(SetKind::MATCH);
  if (k == "play")
    return on(SetKind::PLAY);
  if (k == "odds")
    return on(SetKind::ODDS);
  if (k == "opp")
    return on(SetKind::OPP);
  if (k == "bootaddr")
    return on(SetKind::BOOTADDR);
  if (k == "collide")
    return on(SetKind::COLLIDE);
  if (k == "today")
    return on(SetKind::TODAY);
  if (k == "autoupd")
    return on(SetKind::AUTOUPD);
  if (k == "beta")
    return on(SetKind::BETA);
  // A follow=1 with nothing to follow is the firmware's to ignore, not a
  // refused value.
  if (k == "follow")
    return on(SetKind::FOLLOW);
  if (k == "alertfav")
    return on(SetKind::ALERTFAV);
  if (k == "idle") {
    op.kind = SetKind::IDLE;
    op.value = read_int(v);
    return op.value >= 0 && op.value < kIdleCount;
  }
  if (k == "idledim") {
    op.kind = SetKind::IDLE_DIM;
    op.value = clamp_idle_dim(read_int(v));
    return true;
  }
  if (k == "night")
    return on(SetKind::NIGHT);
  if (k == "nightstart" || k == "nightend") {
    // "22:00", 24-hour: anything else is refused, not read as some other time.
    op.kind = k == "nightstart" ? SetKind::NIGHT_START : SetKind::NIGHT_END;
    op.value = parse_hhmm(v);
    return op.value >= 0;
  }
  if (k == "nightbri") {
    op.kind = SetKind::NIGHT_BRI;
    op.value = clamp_night_bri(read_int(v));
    return true;
  }
  if (k == "bet") {
    op.kind = SetKind::BET;
    return parse_bet(v, op.slot, op.bet);
  }
  if (k == "betdel") {
    op.kind = SetKind::BET_DEL;
    return parse_bet_slot(v, op.slot);
  }
  if (k == "watch")
    return on(SetKind::WATCH);
  if (k == "watchmine")
    return on(SetKind::WATCHMINE);
  if (k == "watchstay") {
    op.kind = SetKind::WATCHSTAY;
    op.value = clamp_watch_stay_seconds(read_int(v));
    return true;
  }
  if (k == "split") {
    // One digit, nothing else: the page sends only the choices it shows.
    op.kind = SetKind::SPLIT;
    op.value = v.size() == 1 && v[0] >= '0' && v[0] < '0' + kSplitCount ? v[0] - '0' : -1;
    return op.value >= 0;
  }
  if (k == "clocksize" || k == "textsize") {
    // One digit, nothing else, as for split.
    op.kind = k == "clocksize" ? SetKind::CLOCK_SIZE : SetKind::TEXT_SIZE;
    op.value = v.size() == 1 && v[0] >= '0' && v[0] < '0' + kSizeCount ? v[0] - '0' : -1;
    return op.value >= 0;
  }
  if (k == "splitteam") {
    // "none" or "" clears it. Not a golf tour or a racing series, as for a
    // favorite: the area shows a game.
    op.kind = SetKind::SPLIT_TEAM;
    if (v == "none" || v.empty())
      return true;
    return parse_team_ref(v, op.team.league, op.team.id) && known_team(op.team.league, op.team.id) &&
           !is_leaderboard((League) op.team.league);
  }
  if (k == "player" || k == "playerdel") {
    op.kind = k == "player" ? SetKind::PLAYER : SetKind::PLAYER_DEL;
    return parse_player(v, k == "playerdel", op);
  }
  return false;
}

}  // namespace espn
