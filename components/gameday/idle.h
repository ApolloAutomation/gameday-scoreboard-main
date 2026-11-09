// The idle display: what the panel shows when none of the games it follows
// is live, and overnight in the owner's night hours. The owner picks one of
// IdleShow; the night hours have their own brightness. A followed game that
// starts takes the panel back by itself, and so does its final for the
// mode's usual wait after it (busy below). Pure, with no ESPHome includes,
// so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
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
  // teams has taken over for a game, Follow this game holds, or another
  // game's big play has the board for a moment (idle_busy).
  bool busy{false};
  // The first board since boot or a change of team or mode is up. Until then
  // the boot screen and that board stay up, at the night brightness or dark
  // as the night hours and Off or dim say.
  bool board_ready{false};
  // On Wi-Fi. Off it, the panel's own screens about getting connected (the
  // hotspot's QR code among them) are never dark or dimmed by the idle display.
  bool connected{false};
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

// The owner's choice with nothing followed on, in or out of the night hours.
// At night the scores rotation and the mode's own board give way to the
// clock; My team today and Off or dim stay as chosen.
inline IdleState idle_choice(uint8_t show, bool night) {
  IdleState s;
  s.night = night;
  switch ((IdleShow) show) {
    case IdleShow::CLOCK:
      s.view = IdleView::CLOCK;
      break;
    case IdleShow::SCORES:
      s.view = night ? IdleView::CLOCK : IdleView::SCORES;
      break;
    case IdleShow::TODAY:
      s.view = IdleView::TODAY;
      break;
    case IdleShow::OFF:
      s.view = IdleView::CLOCK;
      s.dim = true;
      break;
    default:
      s.view = night ? IdleView::CLOCK : IdleView::BOARD;
      break;
  }
  return s;
}

// What the panel shows now. The night hours and Off or dim need only the time
// of day, so they hold through a restart in the night (the clock survives
// one) and a change of team or mode; the view waits for the first board.
inline IdleState idle_state(const IdleInputs &in) {
  IdleState s;
  if (!in.clock_valid)
    return s;
  s.night = in.night_on && in_night(in.minute, in.night_start, in.night_end);
  if (in.busy)
    return s;
  s = idle_choice(in.show, s.night);
  if (!in.connected)
    s.dim = false;
  if (!in.board_ready)
    s.view = IdleView::BOARD;
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

// Off or dim holds the panel below what it would have now: the remote's
// brightness buttons then leave the saved Brightness alone, since a step
// wouldn't show (pages/wizmote.yaml).
inline bool idle_caps(uint8_t want, const IdleState &s, uint8_t night_bri, uint8_t dim, bool override) {
  if (!s.dim)
    return false;
  IdleState lit = s;
  lit.dim = false;
  return idle_brightness(want, s, night_bri, dim, override) < idle_brightness(want, lit, night_bri, dim, override);
}

// The owner's Brightness change in the night hours holds until they end, and
// the next night starts at the night brightness again. Only a known time can
// end them: a moment without one doesn't, and neither does the first board
// after a change of team or mode.
struct NightOverride {
  bool on{false};
  bool was_night{false};  // in the night hours, when the time was last known
  void brightness_changed(bool night) {
    if (night)
      this->on = true;
  }
  void update(bool clock_valid, bool night) {
    if (!clock_valid)
      return;
    if (this->was_night && !night)
      this->on = false;
    this->was_night = night;
  }
};

// A restart forgets when a final came in. A final the panel only ever read as
// one gets the mode's wait while it can still be news: up to this long after
// its start, about a game and its wait (a cached game this old is over too,
// startup.h). Later, the idle display is back at once, as after an overnight
// update's restart between 3 and 5 AM.
constexpr int64_t kFinalFreshSeconds = 4 * 3600;
// A tournament's or a race's final stays up as long as a team's before the
// idle display takes over (leagues.h gives leaderboards no wait of their own).
constexpr uint32_t kBoardFinalWaitMs = 30u * 60u * 1000u;

// What the mode follows now, for idle_busy(): plain values from the component.
struct BusyInputs {
  bool hold{false};           // the demo's scripted game, Follow this game, or another game's big play (watch.h)
  bool live_mode{false};      // Live games (or a football-only live mode)
  bool recap{false};          // Live games' finals rotation: no game followed
  bool live_fallback{false};  // Live games shows the team's card
  bool favorites{false};      // Favorite teams with favorites set
  bool fav_locked{false};     // a favorite took the panel (Take over, its game, After the final)
  bool fav_live{false};       // a favorite's game is in progress
  // Live games picked a game its scan saw in progress and hasn't read it yet:
  // the board waits for that read, which a short Switch every comes to often.
  bool picked{false};
  // The game on the board: My team's, Live games' pick or its team card.
  bool valid{false};
  GameState state{GameState::NOT_FOUND};
  bool leaderboard{false};  // a golf tour or a racing series
  bool on_course{false};    // a leaderboard's round or session is on (not between them)
  int64_t kickoff{0};
  int64_t now{0};
  // Its final: post_seen once the panel has it as one (post_age_ms since),
  // saw_play when the panel had the game before it was final this boot, and
  // lingered when this final already had its wait (My team moved on once).
  bool post_seen{false};
  uint32_t post_age_ms{0};
  uint32_t linger_ms{0};
  bool saw_play{false};
  bool lingered{false};
};

// Busy keeps the mode's own board up, whatever the idle display is: a
// followed game in progress, its final for the mode's wait after it, a
// favorite that took over, Follow this game, or another game's big play on
// the board (hold: at the night brightness in the night hours, which busy
// leaves on, and over Off or dim). A leaderboard is busy while a
// round or session is on, not overnight between rounds, and for the wait
// after its final.
inline bool idle_busy(const BusyInputs &b) {
  if (b.hold)
    return true;
  if (b.live_mode && b.recap)
    return false;
  if (b.live_mode && !b.live_fallback)
    // A game Live games picked, until its next look moves on after the final,
    // and from the pick on: the scan saw it in progress, so the idle display
    // doesn't come up between two games while the next one is read.
    return b.picked || (b.valid && (b.state == GameState::IN || b.state == GameState::POST));
  if (b.favorites)
    return b.fav_locked || b.fav_live;
  if (!b.valid)
    return false;
  if (b.state == GameState::IN)
    return !b.leaderboard || b.on_course;
  if (b.state != GameState::POST || !b.post_seen || b.lingered || b.post_age_ms >= b.linger_ms)
    return false;
  return b.saw_play || (b.kickoff > 0 && b.now > 0 && b.now - b.kickoff < kFinalFreshSeconds);
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

// The board's clock row for a game: the game clock while it's on and, on My
// team today's card (countdown), the countdown to a kickoff that has a time,
// from now (a Unix time); "" otherwise. The record rows under the logos are
// sized beside it (record_row_chars), a pro team's standing too.
inline std::string board_clock_text(const GameSnapshot &g, bool countdown, int64_t now) {
  if (!g.valid)
    return "";
  if (g.state == GameState::IN)
    return clock_text(g);
  if (countdown && g.state == GameState::PRE && !g.time_tbd && g.kickoff_epoch > 0 && now > 0)
    return countdown_text(g.kickoff_epoch - now);
  return "";
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

// Today in the panel's time zone: from the last local midnight to the next.
// local is the time now in that zone. Each midnight comes from its date,
// turned into epoch seconds by midnight(year, month, day): the time
// component's own zone rules on the panel, which leave libc's TZ unset, and
// mktime under TZ in the host tests. So a day the clocks change on is 23 or
// 25 hours long, where counting back the time of day would be an hour off.
struct LocalDay {
  int64_t start{0}, end{0};
};
inline int month_days(int month, int year) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    return 29;
  return month >= 1 && month <= 12 ? kDays[month - 1] : 31;
}
template<typename Midnight> LocalDay local_day(int64_t now, const struct tm &local, Midnight midnight) {
  LocalDay d;
  int y = local.tm_year + 1900, m = local.tm_mon + 1, day = local.tm_mday;
  d.start = midnight(y, m, day);
  if (++day > month_days(m, y)) {
    day = 1;
    if (++m > 12) {
      m = 1;
      y++;
    }
  }
  d.end = midnight(y, m, day);
  if (d.start > now || d.end <= now) {
    // Zone rules that make no sense of a midnight: 24 hours from the time of
    // day, as before.
    d.start = now - ((int64_t) local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
    d.end = d.start + 86400;
  }
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
// final of the last one today, up until local midnight; -1 for the clock. A
// game 2 with no start time (day_order) comes after game 1.
inline int today_pick(const std::vector<TodayGame> &games, const LocalDay &day) {
  auto order = [&games](int i) { return day_order(games[i].kickoff, games[i].tbd); };
  int pick = -1;
  for (size_t i = 0; i < games.size(); i++) {
    const TodayGame &g = games[i];
    if (!today_has(g, day) || g.state == GameState::POST || g.state == GameState::NOT_FOUND)
      continue;
    if (pick < 0 || order((int) i) < order(pick))
      pick = (int) i;
  }
  if (pick >= 0)
    return pick;
  for (size_t i = 0; i < games.size(); i++) {
    const TodayGame &g = games[i];
    if (!today_has(g, day) || g.state != GameState::POST)
      continue;
    if (pick < 0 || order((int) i) > order(pick))
      pick = (int) i;
  }
  return pick;
}

// My team today after a restart, in a league whose team read skips past a
// final to the next game (MLB, the NBA, the WNBA, men's college basketball,
// the NHL): that final is read too when it started today (since: the local
// day's start, 0 when My team today isn't the idle display) and isn't the one
// kept already (have: its start, as day_order gives it, 0 for none; a later
// one replaces it). Minor league baseball's, the AHL's and the ECHL's team
// reads list today's final themselves, which is read the same way.
inline bool want_today_final(int64_t since, bool next_final, int64_t kickoff, int64_t have) {
  return since > 0 && next_final && kickoff >= since && kickoff > have;
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
