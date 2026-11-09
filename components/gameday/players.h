// Favorite players: up to kPlayerSlots players the owner picks on the device
// page, kept in flash; each one's stat line in the ticker while the game on
// the board is theirs and ESPN names them a game leader; and a splash with
// their name when they score.
// Nothing here reads more from ESPN. The game read the board already makes
// keeps the favorites' leader lines and the last play's athletes, and only
// while a favorite is in that game's league (parse_event's PlayerWatch), so
// a panel without favorite players reads and keeps what it always did.
// What ESPN gives differs by sport, and so does what can splash:
// - Football and baseball: the last play's athletes and its points, at each
//   read. A favorite in a play that scored, between two reads, splashes.
// - Basketball: the last play is only the latest at each read (10 s apart),
//   so most baskets are never seen. Only a late lead change or a buzzer
//   beater (alerts.h) credited to the favorite splashes.
// - Soccer: every goal and its scorer.
// - Hockey: ESPN names no athletes, only the play's words ("Brady Tkachuk
//   Goal (10) Snap Shot, assists: ..."). The scorer's name is matched with
//   the favorites on the scoring team, and splashes only when exactly one
//   matches.
// ESPN's leaders: football's for the whole game (passing, rushing,
// receiving), basketball's per team (points, rebounds, assists, a summary
// line), baseball's both. Hockey and soccer have none, so no stat line.
// Pure, no ESPHome includes, so the host tests cover it
// (tests/test_players.cpp).
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "alerts.h"
#include "espn_parse.h"
#include "set_request.h"

namespace espn {

static_assert(PlayerWatch::kMax == kPlayerSlots, "a game read can watch every favorite player");

// One favorite player, laid out as the saved blob keeps it: fixed size, no
// padding. Zero (FavPlayer{}) is an empty slot.
struct FavPlayer {
  uint32_t athlete;               // ESPN's athlete id; 0 = empty slot
  uint32_t team_id;               // the team ESPN listed them on when picked
  uint8_t league;                 // League
  char name[kPlayerNameMax + 1];  // the panel's name for them, NUL-terminated: "ALLEN", "J. HUGHES"
};
static_assert(sizeof(FavPlayer) == 20, "FavPlayer is the saved blob's layout: keep its size");

// The favorite players, one per slot, saved as one blob (gameday_players_v1).
// Its size is fixed: a blob that changes size fails to load and loses every
// player in it, so more players would need a new blob.
struct PlayerBlob {
  FavPlayer players[kPlayerSlots];
};
static_assert(sizeof(PlayerBlob) == 160, "PlayerBlob is the saved blob: keep its size");

inline bool player_empty(const FavPlayer &p) { return p.athlete == 0; }

// What a slot read back from flash must be, or it is emptied: a league the
// panel follows teams in, a team in its list, and a name the set request
// would have kept.
inline bool player_valid(const FavPlayer &p) {
  if (p.athlete == 0 || p.league >= kLeagueCount || is_leaderboard((League) p.league) ||
      !known_team(p.league, p.team_id) || memchr(p.name, 0, sizeof(p.name)) == nullptr)
    return false;
  const std::string n(p.name);
  return !n.empty() && clean_player_name(n) == n;
}

inline FavPlayer make_player(uint8_t league, uint32_t athlete, uint32_t team_id, const std::string &name) {
  FavPlayer p{};
  p.athlete = athlete;
  p.team_id = team_id;
  p.league = league;
  strncpy(p.name, name.c_str(), kPlayerNameMax);
  return p;
}

// Puts p in the list: the same player (league and athlete) again keeps
// their slot and takes the new team and name, anyone else the first empty slot.
// The slot, 1 to kPlayerSlots, or 0 when every slot is in use.
inline uint8_t player_put(FavPlayer *list, const FavPlayer &p) {
  int free_slot = -1;
  for (int i = 0; i < kPlayerSlots; i++) {
    if (!player_empty(list[i]) && list[i].league == p.league && list[i].athlete == p.athlete) {
      list[i] = p;
      return (uint8_t) (i + 1);
    }
    if (free_slot < 0 && player_empty(list[i]))
      free_slot = i;
  }
  if (free_slot < 0)
    return 0;
  list[free_slot] = p;
  return (uint8_t) (free_slot + 1);
}

// Drops the player; false when they aren't in the list.
inline bool player_drop(FavPlayer *list, uint8_t league, uint32_t athlete) {
  for (int i = 0; i < kPlayerSlots; i++)
    if (!player_empty(list[i]) && list[i].league == league && list[i].athlete == athlete) {
      list[i] = FavPlayer{};
      return true;
    }
  return false;
}

// The favorites in league, for a read of one of its games. n is 0 when there
// are none: the read is then the same as without favorite players.
inline PlayerWatch player_watch(const FavPlayer *list, uint8_t league) {
  PlayerWatch w;
  for (int i = 0; i < kPlayerSlots; i++)
    if (!player_empty(list[i]) && list[i].league == league && w.n < PlayerWatch::kMax)
      w.ids[w.n++] = list[i].athlete;
  return w;
}

// ---- the stat line --------------------------------------------------------------

// The longest stat after the name in the ticker: ESPN's longest football
// line, "27/43, 295 YDS, 2 TD, 2 INT", fits.
constexpr size_t kPlayerStatMax = 28;

// s in at most max characters, cut after the last whole ", " piece that
// fits; a first piece longer than max is cut where it reaches it.
inline std::string stat_trim(const std::string &s, size_t max) {
  if (s.size() <= max)
    return s;
  size_t cut = std::string::npos;
  for (size_t at = s.find(", "); at != std::string::npos && at <= max; at = s.find(", ", at + 1))
    cut = at;
  std::string out = s.substr(0, cut == std::string::npos ? max : cut);
  while (!out.empty() && (out.back() == ' ' || out.back() == ','))
    out.pop_back();
  return out;
}

// The player's line from the game's leaders, "" when they don't name the
// player. Basketball: ESPN's summary line for the team ("14 PTS, 7 AST")
// when the player is its leader there, else the categories they lead ("9
// REB"). Elsewhere the first category that names them: football's in ESPN's order (passing,
// rushing, receiving), baseball's all the same batting line.
inline std::string player_stat(const GameSnapshot &g, uint32_t athlete) {
  if (g.sport == Sport::BASKETBALL) {
    static const char *const kCats[][2] = {{"points", " PTS"}, {"rebounds", " REB"}, {"assists", " AST"}};
    std::string rating, parts;
    for (const auto &c : kCats)
      for (const Leader &l : g.leaders)
        if (l.athlete == athlete && l.cat == c[0] && !l.value.empty()) {
          parts += (parts.empty() ? "" : ", ") + l.value + c[1];
          break;
        }
    for (const Leader &l : g.leaders)
      if (l.athlete == athlete && l.cat == "rating" && !l.value.empty())
        rating = l.value;
    return stat_trim(rating.empty() ? parts : rating, kPlayerStatMax);
  }
  for (const Leader &l : g.leaders)
    if (l.athlete == athlete && !l.value.empty())
      return stat_trim(l.value, kPlayerStatMax);
  return "";
}

// "ALLEN 21/34, 241 YDS, 2 TD" for each favorite in league that ESPN names a
// leader of g, in slot order, while g is on. None before the start or after
// the final.
inline std::vector<std::string> player_lines(const GameSnapshot &g, uint8_t league, const FavPlayer *list) {
  std::vector<std::string> out;
  if (!g.valid || g.state != GameState::IN || g.leaders.empty() || league_sport((League) league) != g.sport)
    return out;
  for (int i = 0; i < kPlayerSlots; i++) {
    const FavPlayer &p = list[i];
    if (player_empty(p) || p.league != league)
      continue;
    const std::string stat = player_stat(g, p.athlete);
    if (!stat.empty())
      out.push_back(std::string(p.name) + " " + stat);
  }
  return out;
}

// ---- scoring ------------------------------------------------------------------

// A favorite who scored between two reads: their slot (0 based, -1 none),
// the splash's words for the play and the side they play for.
struct PlayerScore {
  int slot{-1};
  std::string word;
  AlertSide side{AlertSide::TEAM};
};

namespace detail {

// The scorer of an NHL goal in ESPN's words, "Brady Tkachuk Goal (10) Snap
// Shot, assists: ...": the words before " Goal", plain ASCII in capitals
// ("BRADY TKACHUK"). "" when the words are no goal, or a fresh goal's that
// came before its scorer (", assists: N. Pionk (1)").
inline std::string nhl_scorer(const std::string &text) {
  size_t at = text.find(" Goal (");
  if (at == std::string::npos && text.size() >= 5 && text.compare(text.size() - 5, 5, " Goal") == 0)
    at = text.size() - 5;
  if (at == std::string::npos || at == 0 || text[0] == ',')
    return "";
  std::string out = ascii_fold(text.substr(0, at));
  for (char &c : out)
    if (c >= 'a' && c <= 'z')
      c = (char) (c - 'a' + 'A');
  return out;
}

// name, the panel's name for a favorite ("TKACHUK", or "B. TKACHUK" when a
// teammate shares the last name), is the scorer: the scorer's name ends with
// it as whole words, and an initial is the first name's.
inline bool nhl_name_is(const std::string &scorer, const std::string &name) {
  std::string last = name;
  char initial = 0;
  if (name.size() > 3 && name[1] == '.' && name[2] == ' ') {
    initial = name[0];
    last = name.substr(3);
  }
  if (last.empty() || scorer.size() < last.size() || scorer.compare(scorer.size() - last.size(), last.size(), last) != 0)
    return false;
  const size_t before = scorer.size() - last.size();
  if (before != 0 && scorer[before - 1] != ' ')
    return false;
  return initial == 0 || scorer[0] == initial;
}

// The athlete is in the play for team: the team ESPN lists them on there,
// else the one they were picked on.
inline bool credited(const GameSnapshot &cur, const FavPlayer &p, uint32_t team) {
  for (const PlayAthlete &a : cur.play_athletes)
    if (a.id == p.athlete)
      return (a.team != 0 ? a.team : p.team_id) == team;
  return false;
}

}  // namespace detail

// The favorite in league who scored between two reads of one game, the
// board's game read in league. Only plays while the game is on, and at the
// final only the play that ended it (a walk-off, a buzzer beater), as
// detect_big_play sees them with every switch on and both sides counting:
// the favorite's own switches are the Favorite player one, not the team's.
// Football and baseball: they are one of the last play's athletes, on the side
// that scored, the play is new and ESPN gives it points. Basketball: the
// same, for a late lead change or a buzzer beater only. Soccer: the newest
// goal is theirs, not an own goal. Hockey: the scorer's name is theirs and no
// other favorite's on that team, in words new since the read before.
inline PlayerScore player_scored(const GameSnapshot &prev, const GameSnapshot &cur, uint8_t league,
                                 const FavPlayer *list) {
  PlayerScore none;
  if (league_sport((League) league) != cur.sport)
    return none;
  const BigPlay p = detect_big_play(prev, cur, kAlertsAll, true);
  if (p.type == Alert::NONE || p.side == AlertSide::GAME)
    return none;
  const bool live = prev.state == GameState::IN && cur.state == GameState::IN;
  if (!live && p.type != Alert::WALK_OFF && p.type != Alert::BUZZER_BEATER)
    return none;
  const uint32_t team = p.side == AlertSide::TEAM ? cur.team_id : cur.opp_id;
  PlayerScore out;
  out.side = p.side;
  if (cur.sport == Sport::SOCCER) {
    if (cur.soc.goals.size() <= prev.soc.goals.size())
      return none;
    const SoccerGoal &g = cur.soc.goals.back();
    if (g.own_goal || g.athlete == 0 || g.ours != (p.side == AlertSide::TEAM))
      return none;
    for (int i = 0; i < kPlayerSlots && out.slot < 0; i++)
      if (!player_empty(list[i]) && list[i].league == league && list[i].athlete == g.athlete)
        out.slot = i;
    out.word = alert_word(p.type);  // GOAL, PENALTY GOAL
    return out.slot >= 0 ? out : none;
  }
  if (cur.sport == Sport::HOCKEY) {
    // A goal's words ESPN hasn't replaced yet are an earlier goal's.
    const std::string scorer = detail::new_play(prev, cur) ? detail::nhl_scorer(cur.last_play) : std::string();
    if (scorer.empty())
      return none;
    int hits = 0;
    for (int i = 0; i < kPlayerSlots; i++)
      if (!player_empty(list[i]) && list[i].league == league && list[i].team_id == team &&
          detail::nhl_name_is(scorer, list[i].name)) {
        out.slot = i;
        hits++;
      }
    out.word = alert_word(Alert::HOCKEY_GOAL);
    return hits == 1 ? out : none;
  }
  if (cur.sport != Sport::FOOTBALL && cur.sport != Sport::BASEBALL && cur.sport != Sport::BASKETBALL)
    return none;
  if (cur.play_points <= 0 || !detail::new_play(prev, cur))
    return none;
  if (cur.sport == Sport::BASKETBALL && p.type != Alert::LEAD_CHANGE && p.type != Alert::BUZZER_BEATER)
    return none;
  for (int i = 0; i < kPlayerSlots && out.slot < 0; i++)
    if (!player_empty(list[i]) && list[i].league == league && detail::credited(cur, list[i], team))
      out.slot = i;
  if (out.slot < 0)
    return none;
  if (cur.sport == Sport::FOOTBALL) {
    // The play's own points say what it was: a touchdown can be followed
    // by its try within one read.
    const int pts = cur.play_points;
    out.word = alert_word(pts == 6   ? Alert::TOUCHDOWN
                          : pts == 3 ? Alert::FIELD_GOAL
                          : pts == 1 ? Alert::EXTRA_POINT
                          : pts == 2 ? (p.type == Alert::SAFETY ? Alert::SAFETY : Alert::TWO_POINT)
                                     : p.type);
  } else if (p.type == Alert::RUN) {
    out.word = cur.play_points == 1 ? std::string("RBI") : std::to_string(cur.play_points) + " RBI";
  } else {
    out.word = alert_word(p.type);  // HOME RUN, GRAND SLAM, WALK-OFF, LEAD CHANGE, BUZZER BEATER
  }
  return out;
}

// The splash for a favorite's score: their name and the play's words, beside
// each other when they fit a line (10 characters on one panel, 16 on two),
// else the words under the name, in their side's color. The words are the
// team splashes' own, so the panel gives the same ones fireworks.
inline Splash player_splash(const PlayerScore &s, const GameSnapshot &g, const FavPlayer *list, bool wide) {
  if (s.slot < 0 || s.slot >= kPlayerSlots || player_empty(list[s.slot]) || s.word.empty())
    return Splash{};
  const std::string name = list[s.slot].name;
  const bool one_line = name.size() + 1 + s.word.size() <= (wide ? 16u : 10u);
  return Splash{name + (one_line ? " " : "\n") + s.word,
                parse_color(s.side == AlertSide::OPP ? g.opp_color : g.team_color)};
}

// What the board splashes for two reads of its game: a favorite player's
// score while the Favorite player switch is on in alerts, in place of the
// team's splash for that play, never as well; else decide_splash's big play
// or win. alerts is the mask after Favorite teams only, so a game that gets
// no big plays gets no player's either.
inline Splash board_splash_with_players(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes,
                                        bool neutral, uint32_t alerts, bool wide, uint8_t league,
                                        const FavPlayer *list) {
  if ((alerts & alert_bit(Alert::PLAYER)) != 0) {
    const PlayerScore ps = player_scored(prev, cur, league, list);
    if (ps.slot >= 0)
      return player_splash(ps, cur, list, wide);
  }
  return decide_splash(prev, cur, opponent_splashes, neutral, alerts, wide);
}

}  // namespace espn
