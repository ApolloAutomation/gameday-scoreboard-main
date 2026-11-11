// The device page's POST /gameday/set: which keys it takes, what each value
// means and where it is clamped or refused, and the leagues each live mode
// scans.
// Pure, no ESPHome includes, so the host tests cover it and check the same
// table as tests/web/mock_device.py (tests/fixtures/set_cases.json).
#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../panel_layout/layouts.h"
#include "bets.h"
#include "idle.h"
#include "leagues.h"
#include "props.h"
#include "startup.h"
#include "teams.h"
#include "timezones.h"

namespace espn {

// In the order the firmware applies them, whatever order the request uses.
// follow first; deletes, rotate, panels, splitteam precede their partners.
constexpr const char *kSetKeys[] = {"follow",    "team",       "mode",      "rotate",   "rotatesec",  "fav1",
                                    "fav2",      "fav3",       "fav4",      "tz",       "tzauto",     "down",
                                    "play",      "odds",       "opp",       "panels",   "wiring",     "bootaddr",
                                    "lockon",    "release",    "collide",   "favs",     "today",      "live",
                                    "autoupd",   "count",      "match",     "beta",     "alerts",     "alertfav",
                                    "watch",     "watchstay",  "watchmine", "standing", "clocksize",  "textsize",
                                    "idle",      "idledim",    "night",     "nightstart", "nightend", "nightbri",
                                    "betdel",    "bet",        "propdel",   "prop",     "playerdel",  "player",
                                    "splitteam", "split"};

// LIVE (5) follows live games in the leagues of the live mask; modes 1-3 are
// the football-only live modes from before, kept for panels that saved them.
enum class Mode : uint8_t { MY_TEAM = 0, LIVE_NFL = 1, LIVE_NCAA = 2, LIVE_ANY = 3, FAVORITES = 4, LIVE = 5 };

// Mode 0 (My team) to 5 (Live games).
constexpr int kModeCount = 6;
static_assert((int) Mode::LIVE + 1 == kModeCount, "parse_set takes modes 0 to kModeCount - 1");

// Saved as a byte (SplitPrefs): keep the values.
enum class SplitShow : uint8_t { OFF = 0, LIVE = 1, TEAM = 2, SCORES = 3, CLOCK = 4 };
constexpr int kSplitCount = 5;

// Clock size and Text size: 0 Normal, 1 Large.
constexpr int kSizeCount = 2;

// Standing: 0 Record, 1 Seed, 2 Place.
constexpr int kStandingCount = 3;
static_assert((int) Standing::PLACE + 1 == kStandingCount, "parse_set takes standings 0 to kStandingCount - 1");

constexpr int kLockonMinMinutes = 5;
constexpr int kLockonMaxMinutes = 120;
constexpr int kReleaseMinSeconds = 30;
constexpr int kReleaseMaxSeconds = 3600;
// "alerts" mask: bit (type - 1) per alert type.
constexpr int kAlertBits = 20;
// Seconds, saved in tens of seconds.
constexpr int kWatchStayMinSeconds = 30;
constexpr int kWatchStayMaxSeconds = 300;

inline int clamp_watch_stay_seconds(int seconds) {
  const int s = seconds < kWatchStayMinSeconds   ? kWatchStayMinSeconds
                : seconds > kWatchStayMaxSeconds ? kWatchStayMaxSeconds
                                                 : seconds;
  return s / 10 * 10;
}
// Name max: one splash line on one panel.
constexpr int kPlayerSlots = 8;
constexpr int kPlayerNameMax = 10;
static_assert(kPropNameMax == kPlayerNameMax, "a prop names its player as favorites do");

inline int clamp_lockon_minutes(int minutes) {
  return minutes < kLockonMinMinutes ? kLockonMinMinutes : minutes > kLockonMaxMinutes ? kLockonMaxMinutes : minutes;
}

inline int clamp_release_seconds(int seconds) {
  return seconds < kReleaseMinSeconds ? kReleaseMinSeconds : seconds > kReleaseMaxSeconds ? kReleaseMaxSeconds : seconds;
}

// Dim level (0 dark) and night brightness, out of 255.
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
// when nothing is left.
// With nfl_default false, idle scores fall back to the clock.
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
// that follows a team scans none.
// With idle_scores, those modes scan live too.
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
  ROTATE,    // value, seconds: 30, or whole minutes to 1800
  FAV_SLOT,  // slot (1-4), team (id 0 clears the slot)
  TZ,        // value, index into kTimezones
  TZ_AUTO,   // on
  DOWN,      // on, football
  PLAY,      // on
  ODDS,      // on
  OPP,       // on
  PANELS,    // value wide, rows high; PanelLayout refuses bad ones
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
  FOLLOW,    // on: pin the board's game; memory only
  ALERTS,    // mask, the alert types switched on
  ALERTFAV,  // on, alerts only for the team's or favorites' games
  STANDING,  // value, an espn::Standing
  IDLE,         // value, idle.h's IdleShow
  IDLE_DIM,     // value, 0 (dark) to 255
  NIGHT,        // on: the night hours apply
  NIGHT_START,  // value, minutes after local midnight
  NIGHT_END,    // value, minutes after local midnight
  NIGHT_BRI,    // value, 1 to 255
  BET,          // slot (0 first empty), bet: a manual bet
  BET_DEL,      // slot: empties it
  PROP,         // slot (0 first empty), prop: a player prop bet
  PROP_DEL,     // slot: empties it
  WATCH,        // on, switch to big plays in other games
  WATCHSTAY,    // value, seconds on the big play's game
  WATCHMINE,    // on, a favorite's big play can preempt the team
  PLAYER,       // team, athlete, name: a favorite player (players.h)
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
  int rows{1};  // used by PANELS only
  bool on{false};
  uint8_t slot{0};
  uint32_t mask{0};
  TeamRef team;
  std::vector<TeamRef> teams;
  std::vector<std::string> refused;  // favs entries left out, for the log
  Bet bet;                           // BET: what was bet, with no game yet
  Prop prop{};                       // PROP: the prop, with no game yet
  uint32_t athlete{0};               // PLAYER, PLAYER_DEL: ESPN's athlete id
  std::string name;                  // PLAYER: the panel's name for the player (clean_player_name)
};

// "" refuses the request; "St. Brown" becomes "ST. BROWN".
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

// player=nfl,3918298,2,ALLEN; ESPN-roster leagues only.
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

// With free_ok, 0 means the first empty slot.
inline bool parse_bet_slot(const std::string &v, uint8_t &slot, bool free_ok = false) {
  if (v.size() != 1 || v[0] < (free_ok ? '0' : '1') || v[0] > '0' + kBetSlots)
    return false;
  slot = (uint8_t) (v[0] - '0');
  return true;
}

// bet=1,nfl,2,spread,-3.5; slot 0 is the first empty.
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

// prop=0,nfl,3918298,2,ALLEN,passyds,over,250.5; ESPN rosters only.
inline bool parse_prop(const std::string &v, uint8_t &slot, Prop &p) {
  std::string f[8];
  size_t n = 0;
  if (!split_fields(v, f, 8, n) || n != 8 || f[0].size() != 1 || f[0][0] < '0' || f[0][0] > '0' + kPropSlots)
    return false;
  slot = (uint8_t) (f[0][0] - '0');
  p = Prop{};
  const LeagueInfo *info = league_by_key(f[1].c_str());
  if (info == nullptr || is_board_sport(info->sport) || info->source != Source::ESPN ||
      !parse_espn_id(f[2], p.athlete) || !parse_espn_id(f[3], p.team_id) ||
      !known_team((uint8_t) info->league, p.team_id))
    return false;
  const std::string name = clean_player_name(f[4]);
  const int stat = prop_stat_by_key(f[5], info->sport);
  if (name.empty() || stat < 0)
    return false;
  p.league = (uint8_t) info->league;
  p.stat = (uint8_t) stat;
  strncpy(p.name, name.c_str(), kPropNameMax);
  if (kPropStats[stat].yes) {
    p.side = (uint8_t) PropSide::YES;
    return f[6] == "yes" && f[7].empty();
  }
  if (f[6] != "over" && f[6] != "under")
    return false;
  p.side = (uint8_t) (f[6] == "over" ? PropSide::OVER : PropSide::UNDER);
  return parse_bet_line(f[7], false, p.line);
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
  // Old clients' rotate=0 must never mean 30 seconds.
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
    // Check before the byte cast: 257 would wrap to 1.
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
    // atoi would read junk as 0: every switch off.
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
  // Nothing to follow is ignored later, not refused here.
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
    // "22:00", 24-hour; anything else is refused.
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
  if (k == "prop") {
    op.kind = SetKind::PROP;
    return parse_prop(v, op.slot, op.prop);
  }
  if (k == "propdel") {
    op.kind = SetKind::PROP_DEL;
    if (v.size() != 1 || v[0] < '1' || v[0] > '0' + kPropSlots)
      return false;
    op.slot = (uint8_t) (v[0] - '0');
    return true;
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
    // One digit only: the page sends just its choices.
    op.kind = SetKind::SPLIT;
    op.value = v.size() == 1 && v[0] >= '0' && v[0] < '0' + kSplitCount ? v[0] - '0' : -1;
    return op.value >= 0;
  }
  if (k == "clocksize" || k == "textsize") {
    op.kind = k == "clocksize" ? SetKind::CLOCK_SIZE : SetKind::TEXT_SIZE;
    op.value = v.size() == 1 && v[0] >= '0' && v[0] < '0' + kSizeCount ? v[0] - '0' : -1;
    return op.value >= 0;
  }
  if (k == "splitteam") {
    // No golf or racing: the area shows a game.
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
