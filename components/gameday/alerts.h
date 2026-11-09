// Big-play alerts: what happened between two reads of one game that is worth
// a full-screen splash. Pure, no ESPHome includes: decide_splash
// (espn_parse.cpp) uses it for the game on the board, and it takes nothing
// but the two reads, the switches and which sides count, so a watcher of
// other games can use it the same way.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>

#include "espn_parse.h"
#include "set_request.h"

namespace espn {

// One switch each on the device page, bit (type - 1) of the "alerts" mask in
// the state document and the set request. New types go at the end, before
// SCORE, so saved masks keep their meaning.
enum class Alert : uint8_t {
  NONE,
  TOUCHDOWN, FIELD_GOAL, EXTRA_POINT, TWO_POINT, SAFETY, INTERCEPTION, FUMBLE,  // football
  RUN, HOME_RUN, GRAND_SLAM, WALK_OFF,                                         // baseball
  HOCKEY_GOAL,                                                                  // hockey
  SOCCER_GOAL, PENALTY_GOAL, RED_CARD,                                          // soccer
  LEAD_CHANGE, BUZZER_BEATER, OVERTIME, FINAL,                                  // basketball
  PLAYER,  // a favorite player scored (players.h); never one of detect_big_play's
  SCORE,   // football points that are none of the above (two scores between reads): TOUCHDOWN's switch
};
static_assert((int) Alert::SCORE == kAlertBits + 1, "one switch per alert type (set_request.h)");
constexpr uint32_t kAlertsAll = (1u << kAlertBits) - 1;

inline uint32_t alert_bit(Alert a) {
  return a == Alert::NONE ? 0 : 1u << ((a == Alert::SCORE ? (int) Alert::TOUCHDOWN : (int) a) - 1);
}

// Whose play it is: the snapshot's team, the other side, or the game itself
// (overtime, the final). A red card names the side that got it.
enum class AlertSide : uint8_t { TEAM, OPP, GAME };

struct BigPlay {
  Alert type{Alert::NONE};
  AlertSide side{AlertSide::TEAM};
  int points{0};  // what the side scored between the reads
};

// Favorite teams only: the game (its league and both sides' ESPN ids) has one
// of teams, n (league, id) pairs, on either side.
inline bool game_has_team(uint8_t league, uint32_t a, uint32_t b, const std::pair<uint8_t, uint32_t> *teams,
                          size_t n) {
  for (size_t i = 0; i < n; i++)
    if (teams[i].second != 0 && teams[i].first == league && (teams[i].second == a || teams[i].second == b))
      return true;
  return false;
}

namespace detail {

// s holds word, ignoring case (word in lower case).
inline bool has_word(const std::string &s, const char *word) {
  const size_t n = strlen(word);
  for (size_t i = 0; i + n <= s.size(); i++) {
    size_t k = 0;
    while (k < n && tolower((unsigned char) s[i + k]) == word[k])
      k++;
    if (k == n)
      return true;
  }
  return false;
}

// A basketball clock in tenths of a second: "8:27" is 5070, "12.3" 123, "0.0"
// 0. -1 when it isn't a clock.
inline int clock_tenths(const std::string &c) {
  if (c.empty() || !isdigit((unsigned char) c[0]))
    return -1;
  char *end = nullptr;
  const long a = strtol(c.c_str(), &end, 10);
  if (*end == ':') {
    const long s = isdigit((unsigned char) end[1]) ? strtol(end + 1, nullptr, 10) : -1;
    return a < 1000 && s >= 0 && s < 60 ? (int) (a * 60 + s) * 10 : -1;
  }
  return a < 100000 ? (int) a * 10 + (*end == '.' && isdigit((unsigned char) end[1]) ? end[1] - '0' : 0) : -1;
}

// The last play is one this read hasn't seen: ESPN's id for it changed, so a
// play whose words ESPN corrects (the yards, the tacklers) is not new again.
// Without ids, its words changed, and not by ESPN adding to the old ones (a
// replay review is appended). A read without a last play says nothing, so
// the play after it isn't new either: ESPN leaves the situation out between
// some plays and then sends the same one again.
inline bool new_play(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (!prev.play_id.empty() && !cur.play_id.empty())
    return prev.play_id != cur.play_id;
  return !prev.last_play.empty() && !cur.last_play.empty() &&
         cur.last_play.compare(0, prev.last_play.size(), prev.last_play) != 0;
}

// ESPN's play type when the read has one ("Pass Interception Return",
// "Interception Return Touchdown"), else the NFL's words for one that stood.
inline bool interception(const GameSnapshot &s) {
  if (!s.play_type.empty())
    return has_word(s.play_type, "interception");
  return has_word(s.last_play, "intercepted") && !has_word(s.last_play, "reversed") &&
         !has_word(s.last_play, "no play");
}

// Only a fumble the other team recovered: "Fumble Recovery (Opponent)" (and
// its "Touchdown"), "Sack Opp Fumble Recovery", "Fumble Return Touchdown".
// Never the offense's own, "Fumble Recovery (Own)" or "Fumble Recovery (Own)
// Touchdown", and never a muffed kick ("Punt Team Fumble Recovery"). Without
// a type, the NFL writes the other team's recovery in capitals ("RECOVERED by
// DAL-D.Bland") and the offense's own in lower case.
inline bool fumble_lost(const GameSnapshot &s) {
  if (!s.play_type.empty())
    return has_word(s.play_type, "fumble") &&
           (has_word(s.play_type, "opp") || has_word(s.play_type, "fumble return"));
  return s.last_play.find("RECOVERED by") != std::string::npos && has_word(s.last_play, "fumble") &&
         !has_word(s.last_play, "reversed") && !has_word(s.last_play, "no play");
}

// Two points are a safety only on evidence: the play says so, or nothing
// says touchdown or conversion and the read before was the middle of a
// drive (the ball and a down), which a safety needs and the try after a
// touchdown never is. Anything else is a 2-point conversion, much the more
// common, as when ESPN leaves the situation out around the try.
inline bool safety(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (has_word(cur.last_play, "safety") || has_word(cur.play_type, "safety"))
    return true;
  for (const std::string *s : {&cur.last_play, &cur.play_type, &prev.last_play, &prev.play_type})
    for (const char *w : {"touchdown", "two-point", "two point", "2pt", "conversion"})
      if (has_word(*s, w))
        return false;
  return prev.possession != 0 && !prev.down_distance.empty();
}

inline bool home_run(const GameSnapshot &s) {
  return s.play_type == "Home Run" || s.last_play.find(" homered") != std::string::npos ||
         has_word(s.last_play, "grand slam");
}

// The first of the candidates whose switch is on.
inline Alert first_on(uint32_t mask, std::initializer_list<Alert> candidates) {
  for (Alert a : candidates)
    if (a != Alert::NONE && (mask & alert_bit(a)))
      return a;
  return Alert::NONE;
}

}  // namespace detail

// The big play between two reads of one game, the first switched on in this
// order: basketball's overtime starting, the team's (scores first), the
// other side's when theirs, then the game's. Scores and plays count only
// while the game is on; at the final only a baseball walk-off, a basketball
// buzzer beater and basketball's FINAL. Nothing across a boot, a team change
// or a new game.
inline BigPlay detect_big_play(const GameSnapshot &prev, const GameSnapshot &cur, uint32_t mask, bool theirs) {
  using detail::first_on;
  using detail::has_word;
  BigPlay none;
  if (!detail::same_game(prev, cur))
    return none;
  const bool final = detail::went_final(prev, cur) && cur.completed;
  const bool live = prev.state == GameState::IN && cur.state == GameState::IN;
  if (!final && !live)
    return none;
  const Sport sport = cur.sport;
  // Overtime starting says so first, even when the read has a basket in it.
  if (sport == Sport::BASKETBALL && live && cur.period > cur.nba.regulation && cur.period > prev.period &&
      first_on(mask, {Alert::OVERTIME}) != Alert::NONE)
    return BigPlay{Alert::OVERTIME, AlertSide::GAME, 0};
  for (int side = 0; side < (theirs ? 2 : 1); side++) {
    const int us0 = side ? prev.opp_score : prev.team_score, them0 = side ? prev.team_score : prev.opp_score;
    const int us1 = side ? cur.opp_score : cur.team_score, them1 = side ? cur.team_score : cur.opp_score;
    const int pts = detail::score_gain(us0, us1);
    if (pts <= 0)
      continue;
    Alert a = Alert::NONE;
    if (sport == Sport::BASEBALL) {
      const bool hr = detail::home_run(cur);
      // The home side goes ahead in the bottom of the 9th or later, which
      // ends it. At the final the read before has to be from the 9th or
      // later too, or the lead could have come in the 8th, its reads missed.
      const bool home = (side == 0) == cur.team_home;
      const bool walk_off = home && us0 <= them0 && us1 > them1 &&
                            (final ? prev.period >= 9 : cur.period >= 9 && cur.mlb.half == Half::BOTTOM);
      // A grand slam by the home run's own runs, not the score, which can
      // hold an earlier play's too.
      const bool slam = hr && (cur.play_points == 4 || has_word(cur.last_play, "grand slam"));
      a = final ? first_on(mask, {walk_off ? Alert::WALK_OFF : Alert::NONE})
                : first_on(mask, {walk_off ? Alert::WALK_OFF : Alert::NONE, slam ? Alert::GRAND_SLAM : Alert::NONE,
                                  hr ? Alert::HOME_RUN : Alert::NONE, Alert::RUN});
    } else if (sport == Sport::BASKETBALL) {
      // A basket that ties or takes the lead with a second or less left in
      // a period: the clock ran between the reads and stopped at a second or
      // less, or ran out from a second or less.
      const int t0 = detail::clock_tenths(prev.display_clock), t1 = detail::clock_tenths(cur.display_clock);
      const bool buzzer = cur.period == prev.period && us0 <= them0 && us1 >= them1 && t1 >= 0 && t0 > t1 &&
                          (t1 > 0 ? t1 : t0) <= 10;
      // The side that was behind goes ahead, in the last 2 minutes of
      // regulation or in overtime. Breaking a tie isn't one: the side ahead
      // before the tie may be the one ahead again.
      const int reg = cur.nba.regulation;
      const bool late = cur.period > reg || (cur.period == reg && t1 >= 0 && t1 <= 1200);
      a = first_on(mask, {buzzer ? Alert::BUZZER_BEATER : Alert::NONE,
                          !final && late && us0 < them0 && us1 > them1 ? Alert::LEAD_CHANGE : Alert::NONE});
    } else if (final) {
      continue;  // the win splash says it
    } else if (sport == Sport::HOCKEY) {
      a = first_on(mask, {Alert::HOCKEY_GOAL});
    } else if (sport == Sport::SOCCER) {
      // The newest goal is this side's penalty (an own goal counts for the other side).
      const auto &goals = cur.soc.goals;
      const bool pen = goals.size() > prev.soc.goals.size() && goals.back().penalty && !goals.back().own_goal &&
                       goals.back().ours == (side == 0);
      a = first_on(mask, {pen ? Alert::PENALTY_GOAL : Alert::NONE, Alert::SOCCER_GOAL});
    } else {
      a = pts >= 6 && pts <= 8 ? Alert::TOUCHDOWN
          : pts == 3           ? Alert::FIELD_GOAL
          : pts == 1           ? Alert::EXTRA_POINT
          : pts == 2           ? (detail::safety(prev, cur) ? Alert::SAFETY : Alert::TWO_POINT)
                               : Alert::SCORE;
      a = first_on(mask, {a});
    }
    if (a != Alert::NONE)
      return BigPlay{a, side ? AlertSide::OPP : AlertSide::TEAM, pts};
  }
  if (sport == Sport::BASKETBALL)
    return final && first_on(mask, {Alert::FINAL}) != Alert::NONE ? BigPlay{Alert::FINAL, AlertSide::GAME, 0} : none;
  if (!live)
    return none;
  if (sport == Sport::SOCCER) {
    // A red card names the side that got it, whichever side that is.
    for (int side = 0; side < 2; side++)
      if ((side ? cur.soc.opp_reds > prev.soc.opp_reds : cur.soc.team_reds > prev.soc.team_reds) &&
          first_on(mask, {Alert::RED_CARD}) != Alert::NONE)
        return BigPlay{Alert::RED_CARD, side ? AlertSide::OPP : AlertSide::TEAM, 0};
  }
  if (sport == Sport::FOOTBALL && detail::new_play(prev, cur)) {
    // A turnover is the side that has the ball after it: the play's own end
    // team, else possession, else the side that didn't have it before when
    // ESPN leaves both out.
    const Alert a = first_on(mask, {detail::interception(cur) ? Alert::INTERCEPTION
                                    : detail::fumble_lost(cur) ? Alert::FUMBLE
                                                               : Alert::NONE});
    const int gainer = cur.play_side    ? cur.play_side
                       : cur.possession  ? cur.possession
                       : prev.possession ? 3 - prev.possession
                                         : 0;
    if (a != Alert::NONE && (gainer == 1 || (gainer == 2 && theirs)))
      return BigPlay{a, gainer == 1 ? AlertSide::TEAM : AlertSide::OPP, 0};
  }
  return none;
}

// The splash's words for a play of type a, before any side or "!":
// "TOUCHDOWN", "HOME RUN", "GOAL". A favorite player's splash
// (players.h) uses them too. RUN's depend on the runs, so it has none here.
inline const char *alert_word(Alert a) {
  // In Alert order, a sport a line.
  static const char *const kWords[] = {
      "",
      "TOUCHDOWN", "FIELD GOAL", "EXTRA POINT", "2-PT", "SAFETY", "INTERCEPTION", "FUMBLE RECOVERY",
      "", "HOME RUN", "GRAND SLAM", "WALK-OFF",
      "GOAL",
      "GOAL", "PENALTY GOAL", "RED CARD",
      "LEAD CHANGE", "BUZZER BEATER", "OVERTIME", "FINAL",
      "SCORE",  // PLAYER: players.h makes that splash; this only keeps the table whole
      "SCORE"};
  static_assert(sizeof(kWords) / sizeof(kWords[0]) == (size_t) Alert::SCORE + 1, "a word per alert type");
  return (size_t) a <= (size_t) Alert::SCORE ? kWords[(int) a] : "";
}

// The splash for a big play, its words fit to the panel: 10 characters a line
// on one panel, 16 on two, and never more than three lines. Our side's say
// the play with "!" ("TOUCHDOWN!", "2 RUNS!"); the other side's, and both
// sides' in the live modes (neutral), put the abbreviation first ("PHI
// TOUCHDOWN", "LAD 2 RUNS"), on a line of its own for the newer words when
// they won't fit beside it. A red card always names the side that got it, on
// card red; overtime and the final say only that.
inline Splash big_play_splash(const BigPlay &p, const GameSnapshot &g, bool neutral, bool wide) {
  if (p.type == Alert::NONE)
    return Splash{};
  const bool opp = p.side == AlertSide::OPP;
  const bool ours = !opp && !neutral && p.type != Alert::RED_CARD;
  const uint32_t color = p.type == Alert::RED_CARD ? 0xCC0000 : parse_color(opp ? g.opp_color : g.team_color);
  std::string word = alert_word(p.type);
  if (p.type == Alert::INTERCEPTION && !wide)
    word = "INTER-CEPTION";  // breaks after the hyphen on one panel
  if (p.type == Alert::RUN)
    word = p.points == 1 ? (ours ? "RUN" : "SCORES") : std::to_string(p.points) + " RUNS";
  if (p.side == AlertSide::GAME)
    return Splash{word, color};
  if (ours)
    return Splash{word + "!", color};
  const std::string &abbr = opp ? g.opp_abbr : g.team_abbr;
  // LVGL breaks a line at a space or after a hyphen, so a phrase that won't
  // fit beside the abbreviation could split as "NYY WALK-" and "OFF". The
  // "\n" reaches the label (panel_text keeps it for the splash). The words
  // this firmware splashed before alerts keep LVGL's own breaks.
  const bool older = p.type <= Alert::EXTRA_POINT || p.type == Alert::RUN || p.type == Alert::HOME_RUN ||
                     p.type == Alert::HOCKEY_GOAL || p.type == Alert::SOCCER_GOAL || p.type == Alert::SCORE ||
                     p.type == Alert::PLAYER;
  const bool own_line = !older && word.find_first_of(" -") != std::string::npos &&
                        abbr.size() + 1 + word.size() > (wide ? 16u : 10u);
  return Splash{abbr + (own_line ? "\n" : " ") + word, color};
}

// The splashes the panel draws as fireworks on black, the text in the team's
// color (gameday-live.yaml); the rest are a solid flash. Touchdowns, home
// runs, grand slams, walk-offs, buzzer beaters, hockey and soccer goals and
// the win ("DAL WINS!", "SCHEFFLER WINS!"). A favorite player's splash puts
// their name before the play's words (players.h), and a name holds no "!",
// so "WINSTON 2-PT" isn't a win: it gets the same flash as the team's.
inline bool splash_fireworks(const std::string &text, Sport sport) {
  for (const char *w : {"TOUCHDOWN", "WINS!", "HOME RUN", "GRAND SLAM", "WALK-OFF", "BUZZER"})
    if (text.find(w) != std::string::npos)
      return true;
  return (sport == Sport::HOCKEY || sport == Sport::SOCCER) && text.find("GOAL") != std::string::npos;
}

}  // namespace espn
