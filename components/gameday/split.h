// Split mode's second area; no ESPHome or LVGL includes.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "recap.h"
#include "set_request.h"
#include "watch.h"

namespace espn {

enum class SplitAt : uint8_t { NONE = 0, BOTTOM, RIGHT2, RIGHT1 };

inline SplitAt split_at(int cols, int rows) {
  if (rows == 2 && cols == 2)
    return SplitAt::BOTTOM;
  if (rows == 1 && cols == 4)
    return SplitAt::RIGHT2;
  if (rows == 1 && cols == 3)
    return SplitAt::RIGHT1;
  return SplitAt::NONE;
}

// The state document's split_at: "" for none.
inline const char *split_at_key(SplitAt a) {
  switch (a) {
    case SplitAt::BOTTOM:
      return "bottom";
    case SplitAt::RIGHT2:
      return "right2";
    case SplitAt::RIGHT1:
      return "right1";
    default:
      return "";
  }
}

// 2x2's bottom half has nothing else: OFF is the clock.
inline SplitShow split_effective(uint8_t show, SplitAt at) {
  if (at == SplitAt::NONE)
    return SplitShow::OFF;
  const SplitShow s = show < kSplitCount ? (SplitShow) show : SplitShow::OFF;
  return s == SplitShow::OFF && at == SplitAt::BOTTOM ? SplitShow::CLOCK : s;
}

// Beside an area, the main board is one 128x64 panel.
struct SplitArea {
  bool on{false};
  int16_t main_w{64};
  int16_t x{0}, y{0}, w{0}, h{0};
};

inline SplitArea split_area(int cols, int rows, uint8_t show) {
  SplitArea a;
  a.main_w = (int16_t) (cols > 0 ? cols * 64 : 64);
  const SplitAt at = split_at(cols, rows);
  if (split_effective(show, at) == SplitShow::OFF)
    return a;
  a.on = true;
  a.main_w = 128;
  a.h = 64;
  switch (at) {
    case SplitAt::BOTTOM:
      a.y = 64;
      a.w = 128;
      break;
    case SplitAt::RIGHT2:
      a.x = 128;
      a.w = 128;
      break;
    default:  // RIGHT1, the last case
      a.x = 128;
      a.w = 64;
      break;
  }
  return a;
}

// DATE while the main board shows a clock: never two.
enum class AreaView : uint8_t { NONE = 0, CLOCK, DATE, SCORES, TEAM, LIVE };

inline const char *area_view_key(AreaView v) {
  switch (v) {
    case AreaView::CLOCK:
      return "clock";
    case AreaView::DATE:
      return "date";
    case AreaView::SCORES:
      return "scores";
    case AreaView::TEAM:
      return "team";
    case AreaView::LIVE:
      return "live";
    default:
      return "";
  }
}

struct AreaInputs {
  SplitShow show{SplitShow::OFF};  // what split_effective gives
  bool night{false};               // inside the night hours
  bool main_clock{false};          // the main board shows its clock card
  bool scores{false};              // the scores rotation has a card for the area
  // A second team's game
  bool team_set{false};
  bool team_main{false};  // its game is the main board's
  bool team_card{false};  // read, or ESPN said it has no game
  bool team_on{false};    // game on, or its final still up
  // Rotating live games
  bool rot_games{false};  // other live games to show
  bool jump{false};       // a big play has the area for its stay
};

inline AreaView area_view(const AreaInputs &in) {
  const AreaView clock = in.main_clock ? AreaView::DATE : AreaView::CLOCK;
  switch (in.show) {
    case SplitShow::OFF:
      return AreaView::NONE;
    case SplitShow::TEAM:
      if (!in.team_set || in.team_main || !in.team_card || (in.night && !in.team_on))
        return clock;
      return AreaView::TEAM;
    case SplitShow::LIVE:
      if (in.jump)
        return AreaView::LIVE;
      if (in.night)
        return clock;
      if (in.rot_games)
        return AreaView::LIVE;
      return in.scores ? AreaView::SCORES : clock;
    case SplitShow::SCORES:
      return !in.night && in.scores ? AreaView::SCORES : clock;
    default:
      return clock;
  }
}

// Half a loop apart, so both never show one card.
inline int scores_step(size_t step, size_t cards, bool main_shows) {
  if (cards == 0 || (main_shows && cards < 2))
    return -1;
  return (int) ((step + (main_shows ? cards / 2 : 0)) % cards);
}

// The area's next card with logos, skipping headers; -1 none
inline int scores_next_logos(const std::vector<RecapCard> &cards, size_t step, bool main_shows) {
  const size_t n = cards.size();
  for (size_t k = 1; k <= n; k++) {
    const int s = scores_step(step + k, n, main_shows);
    if (s < 0)
      return -1;
    if (cards[(size_t) s].kind == RecapKind::FINAL || cards[(size_t) s].kind == RecapKind::LATER)
      return s;
  }
  return -1;
}

// Every 15 s while the main game is live too.
inline uint32_t a2_poll_ms(GameState state, int64_t until_start, bool tbd, bool main_live) {
  switch (state) {
    case GameState::IN:
      return main_live ? 15000 : 10000;
    case GameState::POST:
      return 60000;
    case GameState::PRE:
      return until_start <= 3600 && !tbd ? 60000 : 15 * 60000;
    default:
      return 6u * 3600u * 1000u;
  }
}

// ---- Rotating live games ------------------------------------------------------

static const uint32_t kRotCardMs = 8000;
static const uint32_t kJumpPollMs = 15000;

inline bool rot_ok(const WatchGame &g, uint8_t board_league, const std::string &board_event) {
  return g.state == GameState::IN && !(g.league == board_league && board_event == g.event_id);
}

inline size_t rot_count(const std::vector<WatchGame> &games, uint8_t board_league, const std::string &board_event) {
  size_t n = 0;
  for (const auto &g : games)
    n += rot_ok(g, board_league, board_event) ? 1 : 0;
  return n;
}

// Wraps around; -1 for none.
inline int rot_pick(const std::vector<WatchGame> &games, uint8_t cur_league, const std::string &cur_event,
                    uint8_t board_league, const std::string &board_event) {
  int cur = -1;
  for (size_t i = 0; i < games.size() && !cur_event.empty(); i++)
    if (watch_same(games[i], cur_league, cur_event))
      cur = (int) i;
  const size_t n = games.size();
  for (size_t k = 1; k <= n; k++) {
    const size_t i = cur < 0 ? k - 1 : ((size_t) cur + k) % n;
    if (rot_ok(games[i], board_league, board_event))
      return (int) i;
  }
  return -1;
}

// The card whose logos were fetched, if still on
inline int rot_pick_next(const std::vector<WatchGame> &games, uint8_t cur_league, const std::string &cur_event,
                         uint8_t next_league, const std::string &next_event, uint8_t board_league,
                         const std::string &board_event) {
  for (size_t i = 0; i < games.size() && !next_event.empty(); i++)
    if (watch_same(games[i], next_league, next_event) && rot_ok(games[i], board_league, board_event) &&
        !watch_same(games[i], cur_league, cur_event))
      return (int) i;
  return rot_pick(games, cur_league, cur_event, board_league, board_event);
}

inline std::string rot_rgb(const uint8_t (&c)[3]) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", c[0], c[1], c[2]);
  return buf;
}

// "1st", "2nd", ... "11th"; past reg, "OT", "2OT".
inline std::string rot_period(int p, int reg) {
  if (p > reg)
    return p - reg == 1 ? "OT" : std::to_string(p - reg) + "OT";
  const int t = p % 100;
  const char *suf = t >= 11 && t <= 13 ? "th" : p % 10 == 1 ? "st" : p % 10 == 2 ? "nd" : p % 10 == 3 ? "rd" : "th";
  return std::to_string(p) + suf;
}

// The watcher keeps no ESPN status; infer it from clock.
inline void rot_status(GameSnapshot &g, const WatchGame &w) {
  if (w.state != GameState::IN || w.period == 0)
    return;
  bool zero = w.clock[0] != '\0';
  for (const char *c = w.clock; *c != '\0'; c++)
    zero = zero && (*c < '1' || *c > '9');
  const Sport sport = league_sport((League) w.league);
  if (sport == Sport::BASKETBALL) {
    g.nba.phase = zero                  ? (w.period * 2 == w.regulation ? Phase::HALFTIME : Phase::END_PERIOD)
                  : w.clock[0] != '\0' ? Phase::PLAY
                                        : Phase::OTHER;
  } else if (sport == Sport::BASEBALL && w.half != Half::NONE) {
    static const char *const kHalf[] = {"", "Top ", "Mid ", "Bot ", "End "};
    g.short_detail = kHalf[(int) w.half] + rot_period(w.period, 99);
  } else if (sport == Sport::FOOTBALL || sport == Sport::HOCKEY) {
    const int reg = sport == Sport::FOOTBALL ? 4 : 3;
    g.short_detail = !zero                                       ? std::string(w.clock)
                     : sport == Sport::FOOTBALL && w.period == 2 ? std::string("Halftime")
                                                                 : "End of " + rot_period(w.period, reg);
  }
}

// The away side is "team", as in the live modes.
inline GameSnapshot rot_card(const WatchGame &w) {
  GameSnapshot g = watch_prev(w, league_sport((League) w.league));
  // watch_prev's stand-ins for what the big play check reads
  g.down_distance.clear();
  g.play_type.clear();
  g.play_id.clear();
  g.soc.goals.clear();
  rot_status(g, w);
  g.team_id = w.away_id;
  g.opp_id = w.home_id;
  g.team_abbr = w.away_abbr;
  g.opp_abbr = w.home_abbr;
  g.team_rank = w.away_rank;
  g.opp_rank = w.home_rank;
  g.team_color = rot_rgb(w.away_rgb);
  g.opp_color = rot_rgb(w.home_rgb);
  g.team_logo = team_logo_url((League) w.league, w.away_id, w.away_abbr);
  g.opp_logo = team_logo_url((League) w.league, w.home_id, w.home_abbr);
  return g;
}

inline std::string rot_ticker(const std::vector<WatchGame> &games, uint8_t cur_league, const std::string &cur_event,
                              uint8_t board_league, const std::string &board_event) {
  std::string out;
  for (const auto &g : games) {
    if (!rot_ok(g, board_league, board_event) || watch_same(g, cur_league, cur_event))
      continue;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d %s %d", g.away_abbr, g.team_score, g.home_abbr, g.opp_score);
    out += (out.empty() ? "" : " | ") + std::string(buf);
  }
  return out;
}

}  // namespace espn
