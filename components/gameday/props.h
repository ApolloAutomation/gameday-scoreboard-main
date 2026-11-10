// Player prop bets the owner enters; graded from box scores.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "bets.h"
#include "espn_parse.h"

namespace espn {

// Blob size: changing it loses every saved prop.
constexpr int kPropSlots = 8;
constexpr int kPropNameMax = 10;
// Half units: lines from 0.5 to 999.5.
constexpr int kPropMaxLineHalf = kBetMaxTotalHalf;
constexpr uint8_t kNoAtom = 0xFF;

enum class PropSide : uint8_t { OVER = 1, UNDER = 2, YES = 3 };
// VOID: the box score never lists the player.
enum class PropResult : uint8_t { NONE = 0, WON = 1, LOST = 2, PUSH = 3, VOID = 4 };

// Box score columns by sport, indexes into BoxLine::v.
namespace atom {
constexpr uint8_t PASS_YDS = 0, PASS_TD = 1, CMP = 2, INT = 3, RUSH_YDS = 4, RUSH_ATT = 5, RUSH_TD = 6, REC = 7,
                  REC_YDS = 8, REC_TD = 9, OTHER_TD = 10, TACKLES = 11, SACKS = 12, KICK_PTS = 13;
constexpr uint8_t PTS = 0, REB = 1, AST = 2, THREES = 3, STL = 4, BLK = 5;
constexpr uint8_t H = 0, R = 1, RBI = 2, HR = 3, K = 4, HA = 5, ER = 6, OUTS = 7;
constexpr uint8_t G = 0, A = 1, SOG = 2, BS = 3, SV = 4;
// Soccer: goals from the game's own goal list.
constexpr uint8_t GOALS = 0;
}  // namespace atom

// scripts read rows by regex: one per line.
struct PropStatInfo {
  const char *key;    // the page's word, unique within a sport
  const char *label;  // the panel's words
  Sport sport;
  bool yes;           // a yes prop: no line
  uint8_t atoms[3];   // summed; kNoAtom unused
};

// Saved as the index: append only.
inline constexpr PropStatInfo kPropStats[] = {
    {"passyds", "PASS YDS", Sport::FOOTBALL, false, {atom::PASS_YDS, kNoAtom, kNoAtom}},
    {"passtd", "PASS TD", Sport::FOOTBALL, false, {atom::PASS_TD, kNoAtom, kNoAtom}},
    {"cmp", "CMP", Sport::FOOTBALL, false, {atom::CMP, kNoAtom, kNoAtom}},
    {"int", "INT", Sport::FOOTBALL, false, {atom::INT, kNoAtom, kNoAtom}},
    {"rushyds", "RUSH YDS", Sport::FOOTBALL, false, {atom::RUSH_YDS, kNoAtom, kNoAtom}},
    {"rushatt", "RUSH ATT", Sport::FOOTBALL, false, {atom::RUSH_ATT, kNoAtom, kNoAtom}},
    {"recyds", "REC YDS", Sport::FOOTBALL, false, {atom::REC_YDS, kNoAtom, kNoAtom}},
    {"rec", "REC", Sport::FOOTBALL, false, {atom::REC, kNoAtom, kNoAtom}},
    {"rryds", "RUSH+REC YDS", Sport::FOOTBALL, false, {atom::RUSH_YDS, atom::REC_YDS, kNoAtom}},
    {"td", "ANY TD", Sport::FOOTBALL, true, {atom::RUSH_TD, atom::REC_TD, atom::OTHER_TD}},
    {"tackles", "TACKLES", Sport::FOOTBALL, false, {atom::TACKLES, kNoAtom, kNoAtom}},
    {"sacks", "SACKS", Sport::FOOTBALL, false, {atom::SACKS, kNoAtom, kNoAtom}},
    {"kickpts", "KICK PTS", Sport::FOOTBALL, false, {atom::KICK_PTS, kNoAtom, kNoAtom}},
    {"pts", "PTS", Sport::BASKETBALL, false, {atom::PTS, kNoAtom, kNoAtom}},
    {"reb", "REB", Sport::BASKETBALL, false, {atom::REB, kNoAtom, kNoAtom}},
    {"ast", "AST", Sport::BASKETBALL, false, {atom::AST, kNoAtom, kNoAtom}},
    {"threes", "3PM", Sport::BASKETBALL, false, {atom::THREES, kNoAtom, kNoAtom}},
    {"stl", "STL", Sport::BASKETBALL, false, {atom::STL, kNoAtom, kNoAtom}},
    {"blk", "BLK", Sport::BASKETBALL, false, {atom::BLK, kNoAtom, kNoAtom}},
    {"pra", "PTS+REB+AST", Sport::BASKETBALL, false, {atom::PTS, atom::REB, atom::AST}},
    {"pr", "PTS+REB", Sport::BASKETBALL, false, {atom::PTS, atom::REB, kNoAtom}},
    {"pa", "PTS+AST", Sport::BASKETBALL, false, {atom::PTS, atom::AST, kNoAtom}},
    {"ra", "REB+AST", Sport::BASKETBALL, false, {atom::REB, atom::AST, kNoAtom}},
    {"hits", "HITS", Sport::BASEBALL, false, {atom::H, kNoAtom, kNoAtom}},
    {"runs", "RUNS", Sport::BASEBALL, false, {atom::R, kNoAtom, kNoAtom}},
    {"rbi", "RBI", Sport::BASEBALL, false, {atom::RBI, kNoAtom, kNoAtom}},
    {"hrr", "H+R+RBI", Sport::BASEBALL, false, {atom::H, atom::R, atom::RBI}},
    {"hr", "HR", Sport::BASEBALL, true, {atom::HR, kNoAtom, kNoAtom}},
    {"k", "K", Sport::BASEBALL, false, {atom::K, kNoAtom, kNoAtom}},
    {"hitsallowed", "HITS ALLOWED", Sport::BASEBALL, false, {atom::HA, kNoAtom, kNoAtom}},
    {"er", "ER", Sport::BASEBALL, false, {atom::ER, kNoAtom, kNoAtom}},
    {"outs", "OUTS", Sport::BASEBALL, false, {atom::OUTS, kNoAtom, kNoAtom}},
    {"pts", "PTS", Sport::HOCKEY, false, {atom::G, atom::A, kNoAtom}},
    {"goal", "ANY GOAL", Sport::HOCKEY, true, {atom::G, kNoAtom, kNoAtom}},
    {"ast", "AST", Sport::HOCKEY, false, {atom::A, kNoAtom, kNoAtom}},
    {"sog", "SOG", Sport::HOCKEY, false, {atom::SOG, kNoAtom, kNoAtom}},
    {"blk", "BLK", Sport::HOCKEY, false, {atom::BS, kNoAtom, kNoAtom}},
    {"saves", "SAVES", Sport::HOCKEY, false, {atom::SV, kNoAtom, kNoAtom}},
    {"goal", "ANY GOAL", Sport::SOCCER, true, {atom::GOALS, kNoAtom, kNoAtom}},
};
constexpr size_t kPropStatCount = sizeof(kPropStats) / sizeof(kPropStats[0]);
static_assert(kPropStatCount < 255, "a stat is saved as a byte");

inline const PropStatInfo *prop_stat_info(uint8_t stat) {
  return stat < kPropStatCount ? &kPropStats[stat] : nullptr;
}

// -1 when the sport has no such stat.
inline int prop_stat_by_key(const std::string &key, Sport sport) {
  for (size_t i = 0; i < kPropStatCount; i++)
    if (kPropStats[i].sport == sport && key == kPropStats[i].key)
      return (int) i;
  return -1;
}

// Football, basketball, baseball, hockey read a box score.
inline bool prop_reads_box(Sport sport) {
  return sport == Sport::FOOTBALL || sport == Sport::BASKETBALL || sport == Sport::BASEBALL || sport == Sport::HOCKEY;
}

// Saved blob layout: fixed size, no padding.
struct Prop {
  uint32_t athlete;    // ESPN's athlete id; 0 empty
  uint32_t team_id;    // the player's team when picked
  uint32_t placed;     // entered at, epoch seconds; 0 if clock unset
  uint32_t event;      // ESPN game id; 0 until that game is read
  uint32_t opp_id;     // that game's other team
  uint32_t kickoff;    // that game's start, epoch seconds
  uint32_t expires;    // local midnight after the final, 0 before
  int16_t line;        // half units; 0 for a yes prop
  int16_t final_half;  // the stat at the final, half units
  uint8_t league;      // a League value
  uint8_t stat;        // a kPropStats index
  uint8_t side;        // a PropSide value
  uint8_t result;      // a PropResult value
  uint8_t moved;       // 1: ESPN moved its game; prop stays on it
  char name[kPropNameMax + 1];  // the panel's name, NUL-terminated
};
static_assert(sizeof(Prop) == 48, "Prop is the saved blob's layout: keep its size");

struct PropBlob {
  Prop props[kPropSlots];
};
static_assert(sizeof(PropBlob) == 384, "PropBlob is the saved blob: keep its size");

inline bool prop_empty(const Prop &p) { return p.athlete == 0; }

inline bool prop_name_ok(const char *name, size_t size) {
  if (memchr(name, 0, size) == nullptr || name[0] == 0)
    return false;
  for (const char *c = name; *c; c++)
    if (!((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '.' || *c == '\'' || *c == '-'))
      return false;
  return true;
}

// Flash slots failing these checks are emptied.
inline bool prop_valid(const Prop &p) {
  if (p.athlete == 0 || p.league >= kLeagueCount || is_leaderboard((League) p.league) ||
      !from_espn((League) p.league) || p.result > (uint8_t) PropResult::VOID || p.moved > 1 ||
      !prop_name_ok(p.name, sizeof(p.name)))
    return false;
  const PropStatInfo *s = prop_stat_info(p.stat);
  if (s == nullptr || s->sport != league_sport((League) p.league))
    return false;
  bool known = false;
  for (size_t t = 0; t < kTeamCount && !known; t++)
    known = (uint8_t) kTeams[t].league == p.league && kTeams[t].espn_id == p.team_id;
  if (!known)
    return false;
  if (s->yes)
    return p.side == (uint8_t) PropSide::YES && p.line == 0;
  return (p.side == (uint8_t) PropSide::OVER || p.side == (uint8_t) PropSide::UNDER) && p.line >= 1 &&
         p.line <= kPropMaxLineHalf;
}

inline const char *prop_side_key(const Prop &p) {
  return p.side == (uint8_t) PropSide::YES ? "yes" : p.side == (uint8_t) PropSide::UNDER ? "under" : "over";
}

// e.g. "ALLEN O 250.5 PASS YDS", "ALLEN ANY TD".
inline std::string prop_label(const Prop &p) {
  const PropStatInfo *s = prop_stat_info(p.stat);
  const char *label = s != nullptr ? s->label : "";
  std::string out(p.name, strnlen(p.name, sizeof(p.name)));
  if (p.side == (uint8_t) PropSide::YES)
    return out + " " + label;
  return out + (p.side == (uint8_t) PropSide::UNDER ? " U " : " O ") + bet_points(p.line, false) + " " + label;
}

// Sum of the stat's atoms, half units.
inline int prop_value(const Prop &p, const BoxLine &line) {
  const PropStatInfo *s = prop_stat_info(p.stat);
  int v = 0;
  for (int i = 0; s != nullptr && i < 3; i++)
    if (s->atoms[i] < kBoxAtoms)
      v += line.v[s->atoms[i]];
  return v;
}

// Share of regulation played, in thousandths; -1 unknown.
inline int game_progress(const GameSnapshot &g, uint8_t league) {
  if (g.state == GameState::POST)
    return 1000;
  if (g.state != GameState::IN || g.period < 1)
    return -1;
  if (g.sport == Sport::BASEBALL) {
    const int innings = g.mlb.innings > 0 ? g.mlb.innings : 9;
    if (g.period > innings)
      return 1000;
    int halves = (g.period - 1) * 2;
    const Half h = g.mlb.half;
    halves += h == Half::MID || h == Half::BOTTOM ? 1 : h == Half::END ? 2 : 0;
    int thirds = halves * 3 + ((h == Half::TOP || h == Half::BOTTOM) && g.mlb.outs > 0 ? g.mlb.outs : 0);
    return thirds * 1000 / (innings * 6);
  }
  int periods = 4, secs = 900;
  if (g.sport == Sport::BASKETBALL) {
    periods = g.nba.regulation > 0 ? g.nba.regulation : 4;
    secs = periods == 2 ? 1200 : league == (uint8_t) League::WNBA ? 600 : 720;
  } else if (g.sport == Sport::HOCKEY) {
    periods = 3;
    secs = 1200;
  } else if (g.sport != Sport::FOOTBALL) {
    return -1;
  }
  if (g.period > periods)
    return 1000;
  const int tenths = detail::clock_tenths(g.display_clock);
  const int left = tenths < 0 ? secs : tenths / 10 > secs ? secs : tenths / 10;
  return ((g.period - 1) * secs + secs - left) * 1000 / (periods * secs);
}

// Order matches prop_status_key's names.
enum class PropStatus : uint8_t {
  WAITING,    // no game read yet for the team
  PRE,        // its game hasn't started
  UNREAD,     // started, but the panel isn't reading it
  LIVE,       // on, no number yet or too early to tell
  ON_TRACK,
  OFF_TRACK,
  HIT,        // an over or a yes is already there
  NOT_YET,    // a yes prop still waiting
  WON,
  LOST,
  PUSH,
  VOID,       // no line for the player, or the game ended unplayed
};

inline const char *prop_status_key(PropStatus s) {
  static const char *const kNames[] = {"waiting",  "pre", "unread",  "live", "on_track", "off_track",
                                       "hit",      "not_yet", "won", "lost", "push",     "void"};
  return kNames[(uint8_t) s];
}

inline const char *prop_word(PropStatus s) {
  static const char *const kWords[] = {"", "", "", "", "ON TRACK", "OFF TRACK", "HIT", "NOT YET",
                                       "WON", "LOST", "PUSH", "VOID"};
  return kWords[(uint8_t) s];
}

// Pace counts from a sixteenth of the game on.
constexpr int kPropPaceFrom = 62;

// value: half units, -1 unknown; progress: game_progress.
inline PropStatus prop_status(const Prop &p, const BetGame *g, int value, int progress, int64_t now) {
  switch ((PropResult) p.result) {
    case PropResult::WON:
      return PropStatus::WON;
    case PropResult::LOST:
      return PropStatus::LOST;
    case PropResult::PUSH:
      return PropStatus::PUSH;
    case PropResult::VOID:
      return PropStatus::VOID;
    case PropResult::NONE:
      break;
  }
  if (p.event == 0)
    return PropStatus::WAITING;
  if (g == nullptr || g->event != p.event)
    return (int64_t) p.kickoff > now ? PropStatus::PRE : PropStatus::UNREAD;
  if (g->phase == BetPhase::PRE)
    return PropStatus::PRE;
  if (g->phase == BetPhase::OFF)
    return PropStatus::VOID;
  if (value < 0)
    return PropStatus::LIVE;
  if (p.side == (uint8_t) PropSide::YES)
    return value >= 2 ? PropStatus::HIT : PropStatus::NOT_YET;
  const bool over = p.side == (uint8_t) PropSide::OVER;
  if (value > p.line)
    return over ? PropStatus::HIT : PropStatus::OFF_TRACK;
  if (g->phase == BetPhase::FINAL)
    progress = 1000;
  if (progress < kPropPaceFrom)
    return PropStatus::LIVE;
  const int64_t pace = (int64_t) value * 1000 / progress;
  return (over ? pace >= p.line : pace <= p.line) ? PropStatus::ON_TRACK : PropStatus::OFF_TRACK;
}

// e.g. "ALLEN O 250.5 PASS YDS 187 ON TRACK".
inline std::string prop_ticker(const Prop &p, PropStatus s, int value) {
  const std::string label = prop_label(p);
  if (s == PropStatus::WAITING || s == PropStatus::PRE || s == PropStatus::UNREAD)
    return "PROP " + label;
  if (s == PropStatus::WON || s == PropStatus::LOST || s == PropStatus::PUSH)
    value = p.final_half;
  std::string out = label;
  if (value >= 0 && p.side != (uint8_t) PropSide::YES && s != PropStatus::VOID)
    out += " " + bet_points(value, false);
  const char *word = prop_word(s);
  return *word ? out + " " + word : out;
}

// Grades at the final; played false voids it.
inline bool prop_grade(Prop &p, bool played, int value, int64_t now, int64_t midnight) {
  PropResult r = PropResult::VOID;
  if (played) {
    const int m = p.side == (uint8_t) PropSide::YES ? (value >= 2 ? 1 : -1)
                  : p.side == (uint8_t) PropSide::UNDER ? p.line - value
                                                        : value - p.line;
    r = m > 0 ? PropResult::WON : m < 0 ? PropResult::LOST : PropResult::PUSH;
  }
  const int16_t v = (int16_t) (value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
  if (p.result == (uint8_t) r && p.final_half == v)
    return false;
  if (p.result == (uint8_t) PropResult::NONE)
    p.expires = midnight > now && midnight <= (int64_t) UINT32_MAX ? (uint32_t) midnight : (uint32_t) (now + 1);
  p.result = (uint8_t) r;
  p.final_half = v;
  return true;
}

// Ties to the player's team's game, as bet_observe does.
inline bool prop_observe(Prop &p, const BetGame &g, int64_t now, int64_t midnight) {
  if (prop_empty(p) || !bet_in_game(p, g))
    return false;
  bool changed = false;
  if (!bet_tie(p, g, now, changed))
    return false;
  if (g.phase == BetPhase::OFF && p.result == (uint8_t) PropResult::NONE) {
    prop_grade(p, false, 0, now, midnight);
    return true;
  }
  return changed;
}

// Unfollowed reads never tie or move a prop.
inline bool prop_update(Prop &p, const BetGame &g, int64_t now, int64_t midnight) {
  if (prop_empty(p) || p.event == 0 || p.event != g.event || p.league != g.league)
    return false;
  return prop_observe(p, g, now, midnight);
}

inline bool prop_expired(const Prop &p, int64_t now) {
  if (prop_empty(p))
    return false;
  if (p.result != (uint8_t) PropResult::NONE)
    return now >= (int64_t) p.expires;
  return p.event != 0 && p.kickoff != 0 && now - (int64_t) p.kickoff >= kBetUnreadSeconds;
}

// Slot 0 takes a free slot; 0 when full.
inline uint8_t prop_put(Prop (&props)[kPropSlots], uint8_t slot, const Prop &p) {
  if (slot > kPropSlots)
    return 0;
  for (uint8_t i = 0; slot == 0 && i < kPropSlots; i++)
    if (prop_empty(props[i]))
      slot = i + 1;
  if (slot == 0)
    return 0;
  props[slot - 1] = p;
  return slot;
}

// Empties slots from newer firmware or unknown leagues.
inline void props_loaded(PropBlob &b) {
  for (auto &p : b.props)
    if (!prop_empty(p) && !prop_valid(p))
      p = Prop{};
}

// A soccer player's goals in the game, half units.
inline int soccer_goals(const GameSnapshot &g, uint32_t athlete) {
  int n = 0;
  for (const SoccerGoal &goal : g.soc.goals)
    if (goal.athlete == athlete && !goal.own_goal)
      n += 2;
  return n;
}

// Soccer prop players join the game read's watch.
inline void prop_watch_add(PlayerWatch &w, const Prop *props, uint8_t league) {
  if (league_sport((League) league) != Sport::SOCCER)
    return;
  for (int i = 0; i < kPropSlots; i++)
    if (!prop_empty(props[i]) && props[i].league == league && !w.has(props[i].athlete) && w.n < PlayerWatch::kMax)
      w.ids[w.n++] = props[i].athlete;
}

// The prop players a box score read keeps.
inline PlayerWatch prop_box_watch(const Prop *props, uint8_t league) {
  PlayerWatch w;
  for (int i = 0; i < kPropSlots; i++)
    if (!prop_empty(props[i]) && props[i].league == league && !w.has(props[i].athlete) && w.n < PlayerWatch::kMax)
      w.ids[w.n++] = props[i].athlete;
  return w;
}

inline const BoxLine *box_line(const std::vector<BoxLine> &lines, uint32_t athlete) {
  for (const BoxLine &l : lines)
    if (l.athlete == athlete)
      return &l;
  return nullptr;
}

}  // namespace espn
