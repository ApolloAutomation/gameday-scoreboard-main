// Split mode: a second area beside the main board, with its own content and
// its own ticker. It is the bottom half of two rows of two, the right two
// panels of four in a row, or the right panel of three in a row (seen from
// the front; display x runs left to right). The owner picks what it shows
// (SplitShow); on three and four wide Off keeps the board stretched across
// the row, and one or two panels have no second area. Where it goes
// (split_area), what it shows now (area_view) and the rotation of other live
// games it can show (rot_pick, rot_card, rot_ticker). Pure, no ESPHome or
// LVGL includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "set_request.h"
#include "watch.h"

namespace espn {

// The owner's choice is set_request.h's SplitShow, kept as a byte (gameday.h,
// SplitPrefs). OFF on two rows of two is the clock, since the bottom half has
// nothing else to show.

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

// What the saved choice comes to on this arrangement: OFF where there is no
// second area, the clock for OFF (or a byte that makes no sense) on two rows
// of two.
inline SplitShow split_effective(uint8_t show, SplitAt at) {
  if (at == SplitAt::NONE)
    return SplitShow::OFF;
  const SplitShow s = show < kSplitCount ? (SplitShow) show : SplitShow::OFF;
  return s == SplitShow::OFF && at == SplitAt::BOTTOM ? SplitShow::CLOCK : s;
}

// Where the main board and the second area go, in display pixels. With a
// second area the main board is 128 wide, laid out as on one 128x64 panel.
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
    default:  // RIGHT1
      a.x = 128;
      a.w = 64;
      break;
  }
  return a;
}

// What the second area shows now. CLOCK is the time, the date and the
// area's own line; DATE the same without the time, while the main board
// shows its own clock card, so there is never a second clock.
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
  SplitShow show{SplitShow::OFF};  // split_effective
  bool night{false};               // inside the night hours
  bool main_clock{false};          // the main board shows its clock card
  bool scores{false};              // the scores rotation has a card for the area
  // A second team's game
  bool team_set{false};
  bool team_main{false};  // its game is the main board's
  bool team_card{false};  // a game read for it, or ESPN said it has none
  bool team_on{false};    // its game on now, or its final up for the league's wait
  // Rotating live games
  bool rot_games{false};  // other live games to show
  bool jump{false};       // a big play has the area for its stay
};

// The rules: at night the scores and the rotation's cards give way to the
// clock, while a second team's game that is on and a big play still show;
// no team picked, or its game on the main board, is the clock; the rotation
// with nothing else live falls back to the scores, else the clock.
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

// The scores rotation's card for the area, step being the main board's:
// half a loop apart while the main board shows the rotation too, so the two
// never show the same card; the same card otherwise. -1 when there is no
// card the main board isn't showing.
inline int scores_step(size_t step, size_t cards, bool main_shows) {
  if (cards == 0 || (main_shows && cards < 2))
    return -1;
  return (int) ((step + (main_shows ? cards / 2 : 0)) % cards);
}

// How often the second team's game is read: as the main board reads its own
// (10 s live, a minute after the final or in the hour before the start, else
// every 15 minutes, and every 6 hours with no game), but every 15 s while
// the main board's own game is live too. until_start: seconds to the start.
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

// Each card stays up this long.
static const uint32_t kRotCardMs = 8000;
// A big play's game is read this often while it has the area.
static const uint32_t kJumpPollMs = 15000;

// A watched game the rotation can show: read as on, and not the main board's.
inline bool rot_ok(const WatchGame &g, uint8_t board_league, const std::string &board_event) {
  return g.state == GameState::IN && !(g.league == board_league && board_event == g.event_id);
}

inline size_t rot_count(const std::vector<WatchGame> &games, uint8_t board_league, const std::string &board_event) {
  size_t n = 0;
  for (const auto &g : games)
    n += rot_ok(g, board_league, board_event) ? 1 : 0;
  return n;
}

// The card after the one up (league and event, "" for none): the next game
// rot_ok in list order, around to the start; the same one when it is the
// only one; -1 for none.
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

inline std::string rot_rgb(const uint8_t (&c)[3]) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02X%02X%02X", c[0], c[1], c[2]);
  return buf;
}

// A rotation card from a watched game's last read: logos, abbreviations with
// a college rank, scores, the period and clock, and baseball's outs and
// runners. The away side is "team", as in the live modes.
inline GameSnapshot rot_card(const WatchGame &w) {
  GameSnapshot g = watch_prev(w, league_sport((League) w.league));
  // watch_prev's stand-ins for what the big play check reads
  g.down_distance.clear();
  g.play_type.clear();
  g.play_id.clear();
  g.soc.goals.clear();
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

// The area's ticker under a rotation card: the other live games' scores,
// "BOS 3 NYY 2 | ...", leaving out the card's own game and the main board's.
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
