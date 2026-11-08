// The idle display: what the panel shows when none of the games it follows
// is live, and overnight in the owner's night hours. The owner picks one of
// IdleShow; the night hours have their own brightness. A followed game that
// starts takes the panel back by itself, and so does its final for the
// mode's usual wait after it (busy below). Pure, with no ESPHome includes,
// so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// The owner's choice, kept as a byte (gameday.h, IdlePrefs). NEXT is what each
// mode showed with nothing live before there was a choice: the next game's
// card in My team, the favorites' playlist, Live games' team card or recent
// scores. A panel that never chose keeps it.
enum class IdleShow : uint8_t { NEXT = 0, CLOCK = 1, SCORES = 2, TODAY = 3, OFF = 4 };
constexpr int kIdleCount = 5;

constexpr int kDayMinutes = 24 * 60;
// The night hours a panel starts with, off until the owner turns them on.
constexpr int kNightStartDefault = 22 * 60;  // 10 PM
constexpr int kNightEndDefault = 7 * 60;     // 7 AM
// The WizMote's NIGHT level (pages/wizmote.yaml), out of 255
constexpr int kNightBrightnessDefault = 20;

// What the board shows. BOARD is the mode's own; CLOCK the scoreboard's clock
// card (offseason.h); SCORES the finals and later-today rotation (recap.h);
// TODAY the team's game today or its final, else the clock.
enum class IdleView : uint8_t { BOARD = 0, CLOCK, SCORES, TODAY };

struct IdleState {
  IdleView view{IdleView::BOARD};
  bool dim{false};    // Off or dim: the dim level (0 dark) caps the brightness
  bool night{false};  // inside the night hours
  bool operator==(const IdleState &o) const { return view == o.view && dim == o.dim && night == o.night; }
  bool operator!=(const IdleState &o) const { return !(*this == o); }
};

struct IdleInputs {
  uint8_t show{0};          // IdleShow
  bool clock_valid{false};  // nothing can be decided before the clock is set
  int minute{0};            // the local minute of the day, 0 to kDayMinutes - 1
  bool night_on{false};
  int night_start{kNightStartDefault};
  int night_end{kNightEndDefault};
  // A followed game is in progress, its final is still up for the mode's wait
  // after it (My team's half hour, Favorite teams' After the final), Favorite
  // teams has taken over for a game, or Follow this game holds.
  bool busy{false};
};

// Inside the night hours, start included and end not. An end before the start
// runs across midnight; start == end is no hours at all.
inline bool in_night(int minute, int start, int end) {
  if (start == end)
    return false;
  if (start < end)
    return minute >= start && minute < end;
  return minute >= start || minute < end;
}

// What the panel shows now. At night the scores rotation and the mode's own
// board give way to the clock; My team today and Off or dim stay as chosen.
inline IdleState idle_state(const IdleInputs &in) {
  IdleState s;
  if (!in.clock_valid)
    return s;
  s.night = in.night_on && in_night(in.minute, in.night_start, in.night_end);
  if (in.busy)
    return s;
  switch ((IdleShow) in.show) {
    case IdleShow::CLOCK:
      s.view = IdleView::CLOCK;
      break;
    case IdleShow::SCORES:
      s.view = s.night ? IdleView::CLOCK : IdleView::SCORES;
      break;
    case IdleShow::TODAY:
      s.view = IdleView::TODAY;
      break;
    case IdleShow::OFF:
      s.view = IdleView::CLOCK;
      s.dim = true;
      break;
    default:
      s.view = s.night ? IdleView::CLOCK : IdleView::BOARD;
      break;
  }
  return s;
}

// The brightness for the owner's setting (1 to 255) under s, before the heat
// and power guard: the night brightness in the night hours, unless the owner
// changed the brightness since they began (override), and no more than the
// dim level when idle is Off or dim. 0 is dark.
inline uint8_t idle_brightness(uint8_t want, const IdleState &s, uint8_t night_bri, uint8_t dim, bool override) {
  uint8_t b = s.night && !override && night_bri > 0 ? night_bri : want;
  if (s.dim && dim < b)
    b = dim;
  return b;
}

// "in 2h 15m", "in 2h", "in 45m": the time to a kickoff, rounded up to the
// minute. "Starting" once it is due (ESPN can keep a game waiting a while).
inline std::string countdown_text(int64_t seconds) {
  if (seconds <= 0)
    return "Starting";
  int64_t mins = (seconds + 59) / 60;
  long h = (long) (mins / 60), m = (long) (mins % 60);
  char buf[24];
  if (h == 0)
    snprintf(buf, sizeof(buf), "in %ldm", m);
  else if (m == 0)
    snprintf(buf, sizeof(buf), "in %ldh", h);
  else
    snprintf(buf, sizeof(buf), "in %ldh %ldm", h, m);
  return buf;
}

// "22:00" for 22 * 60, the form the page's time fields use.
inline std::string clock_hhmm(int minute) {
  if (minute < 0 || minute >= kDayMinutes)
    minute = 0;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", minute / 60, minute % 60);
  return buf;
}

// "22:00" or "7:05" to minutes after midnight, -1 for anything else.
inline int parse_hhmm(const std::string &v) {
  size_t colon = v.find(':');
  if (colon == std::string::npos || colon < 1 || colon > 2 || v.size() != colon + 3)
    return -1;
  int h = 0, m = 0;
  for (size_t i = 0; i < v.size(); i++) {
    if (i == colon)
      continue;
    if (v[i] < '0' || v[i] > '9')
      return -1;
    if (i < colon)
      h = h * 10 + (v[i] - '0');
    else
      m = m * 10 + (v[i] - '0');
  }
  if (h > 23 || m > 59)
    return -1;
  return h * 60 + m;
}

// Today in the panel's time zone: from the last local midnight to the next,
// given the local time of day now.
struct LocalDay {
  int64_t start{0}, end{0};
};
inline LocalDay local_day(int64_t now, int hour, int minute, int second) {
  LocalDay d;
  d.start = now - ((int64_t) hour * 3600 + minute * 60 + second);
  d.end = d.start + 24 * 3600;
  return d;
}

// A game My team today can show. A game with no time yet is on the day of its
// placeholder (tbd_day_epoch).
struct TodayGame {
  bool valid{false};
  GameState state{GameState::NOT_FOUND};
  int64_t kickoff{0};
  bool tbd{false};
};

inline bool today_has(const TodayGame &g, const LocalDay &day) {
  if (!g.valid || g.kickoff <= 0)
    return false;
  int64_t at = g.tbd ? tbd_day_epoch(g.kickoff) : g.kickoff;
  return at >= day.start && at < day.end;
}

// Which game My team today shows: the first one today that hasn't ended, so a
// doubleheader's second game counts down once the first is over; else the
// final of the last one today, up until local midnight; -1 for the clock.
inline int today_pick(const std::vector<TodayGame> &games, const LocalDay &day) {
  int pick = -1;
  for (size_t i = 0; i < games.size(); i++) {
    const TodayGame &g = games[i];
    if (!today_has(g, day) || g.state == GameState::POST || g.state == GameState::NOT_FOUND)
      continue;
    if (pick < 0 || g.kickoff < games[pick].kickoff)
      pick = (int) i;
  }
  if (pick >= 0)
    return pick;
  for (size_t i = 0; i < games.size(); i++) {
    const TodayGame &g = games[i];
    if (!today_has(g, day) || g.state != GameState::POST)
      continue;
    if (pick < 0 || g.kickoff > games[pick].kickoff)
      pick = (int) i;
  }
  return pick;
}

// The page's name for what the panel shows: "" for the mode's own board.
inline const char *idle_view_key(IdleView v) {
  switch (v) {
    case IdleView::CLOCK:
      return "clock";
    case IdleView::SCORES:
      return "scores";
    case IdleView::TODAY:
      return "today";
    default:
      return "";
  }
}

}  // namespace espn
