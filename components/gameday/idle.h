// Idle display decisions; pure, so host tests compile it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// Saved as a byte; 0 is the pre-choice behavior.
enum class IdleShow : uint8_t { NEXT = 0, CLOCK = 1, SCORES = 2, TODAY = 3, OFF = 4 };
constexpr int kIdleCount = 5;

constexpr int kDayMinutes = 24 * 60;
// Defaults; night hours stay off until the owner enables.
constexpr int kNightStartDefault = 22 * 60;
constexpr int kNightEndDefault = 7 * 60;
// The WizMote's NIGHT level (pages/wizmote.yaml), out of 255
constexpr int kNightBrightnessDefault = 20;

// Views map to offseason.h (CLOCK) and recap.h (SCORES).
enum class IdleView : uint8_t { BOARD = 0, CLOCK, SCORES, TODAY };

struct IdleState {
  IdleView view{IdleView::BOARD};
  bool dim{false};    // Off or dim: dim level caps brightness
  bool night{false};  // inside the night hours
  bool operator==(const IdleState &o) const { return view == o.view && dim == o.dim && night == o.night; }
  bool operator!=(const IdleState &o) const { return !(*this == o); }
};

struct IdleInputs {
  uint8_t show{0};          // an IdleShow value
  bool clock_valid{false};  // nothing can be decided before the clock is set
  int minute{0};            // local minute of the day
  bool night_on{false};
  int night_start{kNightStartDefault};
  int night_end{kNightEndDefault};
  // The mode's own board must stay up (idle_busy).
  bool busy{false};
  // First board since boot or team/mode change is up.
  bool board_ready{false};
  // Setup screens (hotspot QR) are never dimmed offline.
  bool connected{false};
  // Second area busy: Off or dim lets the panel light.
  bool area2_busy{false};
};

inline bool in_night(int minute, int start, int end) {
  if (start == end)
    return false;
  if (start < end)
    return minute >= start && minute < end;
  return minute >= start || minute < end;
}

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

// Night needs only the time, so it survives restarts.
inline IdleState idle_state(const IdleInputs &in) {
  IdleState s;
  if (!in.clock_valid)
    return s;
  s.night = in.night_on && in_night(in.minute, in.night_start, in.night_end);
  if (in.busy)
    return s;
  s = idle_choice(in.show, s.night);
  if (!in.connected || in.area2_busy)
    s.dim = false;
  if (!in.board_ready)
    s.view = IdleView::BOARD;
  return s;
}

// Before the heat and power guard; 0 is dark.
inline uint8_t idle_brightness(uint8_t want, const IdleState &s, uint8_t night_bri, uint8_t dim, bool override) {
  uint8_t b = s.night && !override && night_bri > 0 ? night_bri : want;
  if (s.dim && dim < b)
    b = dim;
  return b;
}

// Remote buttons skip saving steps that wouldn't show.
inline bool idle_caps(uint8_t want, const IdleState &s, uint8_t night_bri, uint8_t dim, bool override) {
  if (!s.dim)
    return false;
  IdleState lit = s;
  lit.dim = false;
  return idle_brightness(want, s, night_bri, dim, override) < idle_brightness(want, lit, night_bri, dim, override);
}

// Only a known time can end a night override.
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

// Restarts forget final times; older finals aren't news.
constexpr int64_t kFinalFreshSeconds = 4 * 3600;
// leagues.h gives leaderboards no final wait of their own.
constexpr uint32_t kBoardFinalWaitMs = 30u * 60u * 1000u;

struct BusyInputs {
  bool hold{false};           // demo game, Follow this game, or a big play
  bool live_mode{false};      // Live games (or a football-only live mode)
  bool recap{false};          // Live games' finals rotation: no game followed
  bool live_fallback{false};  // Live games shows the team's card
  bool favorites{false};      // Favorite teams with favorites set
  bool fav_locked{false};     // a favorite took the panel
  bool fav_live{false};       // a favorite's game is in progress
  // Scan saw it live; its own read is pending.
  bool picked{false};
  // The board's game: My team's or Live games' pick.
  bool valid{false};
  GameState state{GameState::NOT_FOUND};
  bool leaderboard{false};  // a golf tour or a racing series
  bool on_course{false};    // a leaderboard's round or session is on (not between them)
  int64_t kickoff{0};
  int64_t now{0};
  // saw_play: seen before its final this boot; lingered: waited.
  bool post_seen{false};
  uint32_t post_age_ms{0};
  uint32_t linger_ms{0};
  bool saw_play{false};
  bool lingered{false};
};

// Leaderboards aren't busy overnight between rounds.
inline bool idle_busy(const BusyInputs &b) {
  if (b.hold)
    return true;
  if (b.live_mode && b.recap)
    return false;
  if (b.live_mode && !b.live_fallback)
    // No idle flash between games while the pick loads.
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

// Rounds up; ESPN can leave a due game waiting.
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

// record_row_chars sizes the record rows beside this.
inline std::string board_clock_text(const GameSnapshot &g, bool countdown, int64_t now) {
  if (!g.valid)
    return "";
  if (g.state == GameState::IN)
    return clock_text(g);
  if (countdown && g.state == GameState::PRE && !g.time_tbd && g.kickoff_epoch > 0 && now > 0)
    return countdown_text(g.kickoff_epoch - now);
  return "";
}

// The format the page's time fields use.
inline std::string clock_hhmm(int minute) {
  if (minute < 0 || minute >= kDayMinutes)
    minute = 0;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", minute / 60, minute % 60);
  return buf;
}

// "22:00" or "7:05" to minutes; -1 if invalid.
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

// libc TZ is unset on the panel; DST days vary.
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
    // Fallback when zone rules give no sensible midnight.
    d.start = now - ((int64_t) local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec);
    d.end = d.start + 86400;
  }
  return d;
}

// TBD games count on their placeholder's day.
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

// Doubleheaders: game 2 counts down once game 1 ends.
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

// ESPN team reads skip past a final to the next.
inline bool want_today_final(int64_t since, bool next_final, int64_t kickoff, int64_t have) {
  return since > 0 && next_final && kickoff >= since && kickoff > have;
}

// The page's names for these views.
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
