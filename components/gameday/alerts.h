// Big-play detection; pure, shared by the board and watcher.
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

// Add new types before SCORE so saved masks hold.
enum class Alert : uint8_t {
  NONE,
  TOUCHDOWN, FIELD_GOAL, EXTRA_POINT, TWO_POINT, SAFETY, INTERCEPTION, FUMBLE,
  RUN, HOME_RUN, GRAND_SLAM, WALK_OFF,
  HOCKEY_GOAL,
  SOCCER_GOAL, PENALTY_GOAL, RED_CARD,
  LEAD_CHANGE, BUZZER_BEATER, OVERTIME, FINAL,
  PLAYER,  // a favorite player scored (players.h); never one of detect_big_play's
  SCORE,   // other football points; shares TOUCHDOWN's switch
};
static_assert((int) Alert::SCORE == kAlertBits + 1, "one switch per alert type (set_request.h)");
constexpr uint32_t kAlertsAll = (1u << kAlertBits) - 1;

inline uint32_t alert_bit(Alert a) {
  return a == Alert::NONE ? 0 : 1u << ((a == Alert::SCORE ? (int) Alert::TOUCHDOWN : (int) a) - 1);
}

// A red card names the side that got it.
enum class AlertSide : uint8_t { TEAM, OPP, GAME };

struct BigPlay {
  Alert type{Alert::NONE};
  AlertSide side{AlertSide::TEAM};
  int points{0};  // what the side scored between the reads
};

// For Favorite teams only; id 0 never matches.
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

// ESPN edits play words and resends plays; prefer ids.
inline bool new_play(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (!prev.play_id.empty() && !cur.play_id.empty())
    return prev.play_id != cur.play_id;
  return !prev.last_play.empty() && !cur.last_play.empty() &&
         cur.last_play.compare(0, prev.last_play.size(), prev.last_play) != 0;
}

// ESPN's play type first, else the NFL's play words.
inline bool interception(const GameSnapshot &s) {
  if (!s.play_type.empty())
    return has_word(s.play_type, "interception");
  return has_word(s.last_play, "intercepted") && !has_word(s.last_play, "reversed") &&
         !has_word(s.last_play, "no play");
}

// NFL capitalizes "RECOVERED by" only for the defense.
inline bool fumble_lost(const GameSnapshot &s) {
  if (!s.play_type.empty())
    return has_word(s.play_type, "fumble") &&
           (has_word(s.play_type, "opp") || has_word(s.play_type, "fumble return"));
  return s.last_play.find("RECOVERED by") != std::string::npos && has_word(s.last_play, "fumble") &&
         !has_word(s.last_play, "reversed") && !has_word(s.last_play, "no play");
}

// Default to 2-point try; safeties need drive evidence.
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

inline Alert first_on(uint32_t mask, std::initializer_list<Alert> candidates) {
  for (Alert a : candidates)
    if (a != Alert::NONE && (mask & alert_bit(a)))
      return a;
  return Alert::NONE;
}

}  // namespace detail

// Priority: overtime, team, other side (theirs), then game.
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
  // Overtime wins even if the read includes a basket.
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
      // At the final, prev must be from the last inning.
      const bool home = (side == 0) == cur.team_home;
      const int last = cur.mlb.innings > 0 ? cur.mlb.innings : 9;
      const bool walk_off = home && us0 <= them0 && us1 > them1 &&
                            (final ? prev.period >= last : cur.period >= last && cur.mlb.half == Half::BOTTOM);
      // Use the homer's own runs; the score may include more.
      const bool slam = hr && (cur.play_points == 4 || has_word(cur.last_play, "grand slam"));
      a = final ? first_on(mask, {walk_off ? Alert::WALK_OFF : Alert::NONE})
                : first_on(mask, {walk_off ? Alert::WALK_OFF : Alert::NONE, slam ? Alert::GRAND_SLAM : Alert::NONE,
                                  hr ? Alert::HOME_RUN : Alert::NONE, Alert::RUN});
    } else if (sport == Sport::BASKETBALL) {
      // Tie or lead with at most a second left.
      const int t0 = detail::clock_tenths(prev.display_clock), t1 = detail::clock_tenths(cur.display_clock);
      const bool buzzer = cur.period == prev.period && us0 <= them0 && us1 >= them1 && t1 >= 0 && t0 > t1 &&
                          (t1 > 0 ? t1 : t0) <= 10;
      // Breaking a tie isn't one: it may restore a lead.
      const int reg = cur.nba.regulation;
      const bool late = cur.period > reg || (cur.period == reg && t1 >= 0 && t1 <= 1200);
      a = first_on(mask, {buzzer ? Alert::BUZZER_BEATER : Alert::NONE,
                          !final && late && us0 < them0 && us1 > them1 ? Alert::LEAD_CHANGE : Alert::NONE});
    } else if (final) {
      continue;  // the win splash says it
    } else if (sport == Sport::HOCKEY) {
      a = first_on(mask, {Alert::HOCKEY_GOAL});
    } else if (sport == Sport::SOCCER) {
      // Own goals count for the other side.
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
    // Either side's red card, even without theirs.
    for (int side = 0; side < 2; side++)
      if ((side ? cur.soc.opp_reds > prev.soc.opp_reds : cur.soc.team_reds > prev.soc.team_reds) &&
          first_on(mask, {Alert::RED_CARD}) != Alert::NONE)
        return BigPlay{Alert::RED_CARD, side ? AlertSide::OPP : AlertSide::TEAM, 0};
  }
  if (sport == Sport::FOOTBALL && detail::new_play(prev, cur)) {
    // ESPN may omit both; then flip the prior possession.
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

// RUN's words depend on the runs, so it's empty.
inline const char *alert_word(Alert a) {
  // In Alert order, a sport a line.
  static const char *const kWords[] = {
      "",
      "TOUCHDOWN", "FIELD GOAL", "EXTRA POINT", "2-PT", "SAFETY", "INTERCEPTION", "FUMBLE RECOVERY",
      "", "HOME RUN", "GRAND SLAM", "WALK-OFF",
      "GOAL",
      "GOAL", "PENALTY GOAL", "RED CARD",
      "LEAD CHANGE", "BUZZER BEATER", "OVERTIME", "FINAL",
      "SCORE",  // PLAYER: placeholder; players.h makes that splash
      "SCORE"};
  static_assert(sizeof(kWords) / sizeof(kWords[0]) == (size_t) Alert::SCORE + 1, "a word per alert type");
  return (size_t) a <= (size_t) Alert::SCORE ? kWords[(int) a] : "";
}

// RUN's words already say runs, so just the number.
inline std::string splash_points(Alert a, int points) {
  if (points <= 0 || points > 99)
    return "";
  switch (a) {
    case Alert::NONE:
    case Alert::INTERCEPTION:
    case Alert::FUMBLE:
    case Alert::RED_CARD:
    case Alert::LEAD_CHANGE:
    case Alert::OVERTIME:
    case Alert::FINAL:
    case Alert::PLAYER:
      return "";
    case Alert::HOME_RUN:
    case Alert::GRAND_SLAM:
    case Alert::WALK_OFF:
      return "+" + std::to_string(points) + (points == 1 ? " RUN" : " RUNS");
    default:
      return "+" + std::to_string(points);
  }
}

// A read may hold earlier scores the words don't count.
inline int own_points(int gain, int play) { return play > 0 && play <= gain ? play : gain; }

inline int homer_points(const BigPlay &p, const GameSnapshot &g) {
  return p.type == Alert::GRAND_SLAM ? own_points(p.points, 4)
         : p.type == Alert::HOME_RUN ? own_points(p.points, g.play_points)
                                     : p.points;
}

// Lines hold 10 chars on one panel, 16 on two.
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
    return Splash{word, color, "", true, 0, ""};
  const std::string &abbr = opp ? g.opp_abbr : g.team_abbr;
  const std::string points = splash_points(p.type, homer_points(p, g));
  if (ours)
    return Splash{word + "!", color, abbr, false, 0, points};
  // Avoids LVGL splitting "NYY WALK-" from "OFF".
  const bool older = p.type <= Alert::EXTRA_POINT || p.type == Alert::RUN || p.type == Alert::HOME_RUN ||
                     p.type == Alert::HOCKEY_GOAL || p.type == Alert::SOCCER_GOAL || p.type == Alert::SCORE ||
                     p.type == Alert::PLAYER;
  const bool own_line = !older && word.find_first_of(" -") != std::string::npos &&
                        abbr.size() + 1 + word.size() > (wide ? 16u : 10u);
  return Splash{abbr + (own_line ? "\n" : " ") + word, color, abbr, false,
                (uint8_t) (abbr.size() < 255 ? abbr.size() + 1 : 0), points};
}

// How long a splash stays up, board and second area
static const uint32_t kSplashMs = 7000;

// "WINS!" needs the "!" so player names never match.
inline bool splash_fireworks(const std::string &text, Sport sport) {
  for (const char *w : {"TOUCHDOWN", "WINS!", "HOME RUN", "GRAND SLAM", "WALK-OFF", "BUZZER"})
    if (text.find(w) != std::string::npos)
      return true;
  return (sport == Sport::HOCKEY || sport == Sport::SOCCER) && text.find("GOAL") != std::string::npos;
}

}  // namespace espn
