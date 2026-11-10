// Favorite players; reads nothing extra from ESPN; host-tested.
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

// Saved blob layout: fixed size, no padding.
struct FavPlayer {
  uint32_t athlete;               // ESPN's athlete id; 0 = empty slot
  uint32_t team_id;               // the team ESPN listed them on when picked
  uint8_t league;                 // a League value
  char name[kPlayerNameMax + 1];  // the panel's name for them, NUL-terminated: "ALLEN", "J. HUGHES"
};
static_assert(sizeof(FavPlayer) == 20, "FavPlayer is the saved blob's layout: keep its size");

// Resizing fails to load and loses every saved player.
struct PlayerBlob {
  FavPlayer players[kPlayerSlots];
};
static_assert(sizeof(PlayerBlob) == 160, "PlayerBlob is the saved blob: keep its size");

inline bool player_empty(const FavPlayer &p) { return p.athlete == 0; }

// Flash slots failing these checks are emptied.
inline bool player_valid(const FavPlayer &p) {
  if (p.athlete == 0 || p.league >= kLeagueCount || is_leaderboard((League) p.league) ||
      !from_espn((League) p.league) ||
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

// Returns the 1-based slot, or 0 when full.
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

inline bool player_drop(FavPlayer *list, uint8_t league, uint32_t athlete) {
  for (int i = 0; i < kPlayerSlots; i++)
    if (!player_empty(list[i]) && list[i].league == league && list[i].athlete == athlete) {
      list[i] = FavPlayer{};
      return true;
    }
  return false;
}

// n 0 leaves the read unchanged.
inline PlayerWatch player_watch(const FavPlayer *list, uint8_t league) {
  PlayerWatch w;
  for (int i = 0; i < kPlayerSlots; i++)
    if (!player_empty(list[i]) && list[i].league == league && w.n < PlayerWatch::kMax)
      w.ids[w.n++] = list[i].athlete;
  return w;
}

// ---- the stat line --------------------------------------------------------------

// Fits ESPN's longest football stat line.
constexpr size_t kPlayerStatMax = 28;

// Cuts at the last whole ", " piece that fits.
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

// Basketball prefers ESPN's summary line, else led categories.
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

// e.g. "ALLEN 21/34, 241 YDS, 2 TD", live only.
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

// slot is 0-based, -1 for none.
struct PlayerScore {
  int slot{-1};
  std::string word;
  AlertSide side{AlertSide::TEAM};
  Alert type{Alert::NONE};  // the play, for its points (splash_points)
  int points{0};            // what the play in word scored (own_points)
};

namespace detail {

// A fresh goal can lack its scorer: ", assists: ...".
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

// A page-cut name like "NUGENT-HOP" may end mid-word.
inline bool name_ends(const std::string &scorer, const std::string &last, bool cut) {
  if (last.empty())
    return false;
  for (size_t at = scorer.find(last); at != std::string::npos; at = scorer.find(last, at + 1)) {
    if (at != 0 && scorer[at - 1] != ' ')
      continue;
    size_t end = at + last.size();
    if (cut)
      while (end < scorer.size() && ((scorer[end] >= 'A' && scorer[end] <= 'Z') || scorer[end] == '-' || scorer[end] == '\''))
        end++;
    if (end == scorer.size())
      return true;
  }
  return false;
}

// Near-max names may be cut; the page drops a separator.
inline bool nhl_name_is(const std::string &scorer, const std::string &name) {
  const bool cut = name.size() + 2 >= (size_t) kPlayerNameMax;
  if (name_ends(scorer, name, cut))
    return true;
  // "ST. IVANY" is a last name too, tried above.
  const size_t dot = name.find(". ");
  if (dot == std::string::npos || dot == 0 || dot > 3)
    return false;
  for (size_t i = 0; i < dot; i++)
    if (name[i] < 'A' || name[i] > 'Z')
      return false;
  return scorer.compare(0, dot, name, 0, dot) == 0 && name_ends(scorer, name.substr(dot + 2), cut);
}

// ESPN's college try: negative play id, touchdown's athletes.
inline bool espn_made_try(const GameSnapshot &s) { return !s.play_id.empty() && s.play_id[0] == '-'; }

// NFL touchdown plays embed the try, typed as touchdown.
inline bool football_try(const GameSnapshot &s, bool safety) {
  if (espn_made_try(s) || s.play_points == 1 || (s.play_points == 2 && !safety))
    return true;
  for (const char *w : {"extra point", "conversion", "two point", "two-point", "2pt"})
    if (has_word(s.play_type, w))
      return true;
  return false;
}

inline bool credited(const GameSnapshot &cur, const FavPlayer &p, uint32_t team) {
  for (const PlayAthlete &a : cur.play_athletes)
    if (a.id == p.athlete)
      return (a.team != 0 ? a.team : p.team_id) == team;
  return false;
}

}  // namespace detail

// Live only: ESPN's final document has no last play.
inline PlayerScore player_scored(const GameSnapshot &prev, const GameSnapshot &cur, uint8_t league,
                                 const FavPlayer *list) {
  PlayerScore none;
  if (league_sport((League) league) != cur.sport)
    return none;
  if (prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  const BigPlay p = detect_big_play(prev, cur, kAlertsAll, true);
  if (p.type == Alert::NONE || p.side == AlertSide::GAME)
    return none;
  const uint32_t team = p.side == AlertSide::TEAM ? cur.team_id : cur.opp_id;
  PlayerScore out;
  out.side = p.side;
  out.type = p.type;
  out.points = p.points;
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
    // Stale ESPN goal words are an earlier goal's.
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
  if (!detail::new_play(prev, cur))
    return none;
  // A try credits the touchdown's athletes, never the kicker.
  const bool is_try = cur.sport == Sport::FOOTBALL && detail::football_try(cur, p.type == Alert::SAFETY);
  if (is_try && !(detail::espn_made_try(cur) && p.points >= 6 && p.points <= 8))
    return none;
  if (!is_try && cur.play_points <= 0)
    return none;
  if (cur.sport == Sport::BASKETBALL && p.type != Alert::LEAD_CHANGE && p.type != Alert::BUZZER_BEATER)
    return none;
  for (int i = 0; i < kPlayerSlots && out.slot < 0; i++)
    if (!player_empty(list[i]) && list[i].league == league && detail::credited(cur, list[i], team))
      out.slot = i;
  if (out.slot < 0)
    return none;
  // Points come from the words' play, so they agree.
  if (cur.sport == Sport::FOOTBALL) {
    // One read can hold a field goal and a touchdown.
    const int pts = cur.play_points;
    out.word = alert_word(is_try || pts == 6 ? Alert::TOUCHDOWN
                          : pts == 3         ? Alert::FIELD_GOAL
                          : pts == 2         ? Alert::SAFETY  // a 2-point conversion is a try
                                             : p.type);
    if (!is_try && (pts == 6 || pts == 3 || pts == 2))
      out.points = own_points(p.points, pts);
  } else if (p.type == Alert::RUN) {
    out.word = cur.play_points == 1 ? std::string("RBI") : std::to_string(cur.play_points) + " RBI";
    out.points = own_points(p.points, cur.play_points);
  } else {
    out.word = alert_word(p.type);  // HOME RUN, GRAND SLAM, WALK-OFF, LEAD CHANGE, BUZZER BEATER
    out.points = homer_points(p, cur);
  }
  return out;
}

// Reuses team splash words so fireworks match.
inline Splash player_splash(const PlayerScore &s, const GameSnapshot &g, const FavPlayer *list, bool wide) {
  if (s.slot < 0 || s.slot >= kPlayerSlots || player_empty(list[s.slot]) || s.word.empty())
    return Splash{};
  const std::string name = list[s.slot].name;
  const bool one_line = name.size() + 1 + s.word.size() <= (wide ? 16u : 10u);
  const bool opp = s.side == AlertSide::OPP;
  return Splash{name + (one_line ? " " : "\n") + s.word, parse_color(opp ? g.opp_color : g.team_color),
                opp ? g.opp_abbr : g.team_abbr, false, 0, splash_points(s.type, s.points)};
}

// A player's splash replaces the team's, never both.
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

// The first poll has no prior read; reuse brought.
inline Splash visit_splash(bool first, const Splash &brought, const GameSnapshot &prev, const GameSnapshot &cur,
                           uint32_t alerts, bool wide, uint8_t league, const FavPlayer *list) {
  if (first)
    return brought;
  return board_splash_with_players(prev, cur, true, true, alerts, wide, league, list);
}

}  // namespace espn
