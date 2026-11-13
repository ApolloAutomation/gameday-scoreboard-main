// Big-play watcher for other games; pure, so host-tested.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "alerts.h"

namespace espn {

// Reads past kWatchStaleCycles are too stale to compare.
static const size_t kWatchMax = 8;
static const uint32_t kWatchCycleMs = 45 * 1000;
static const uint32_t kWatchStaleCycles = 2;
// Expected read times until a read has been timed.
static const uint32_t kWatchBoardMs = 5000, kWatchEventMs = 2500, kWatchMarginMs = 1000, kWatchTimedMaxMs = 60000;
// Outlasts the splash and its scene (scene.h's kAnimMaxMs).
static const uint32_t kWatchSplashMs = kSplashMs + 4800 + 1000;
// Idle day-scoreboard wait bounds, until the next wanted start.
static const uint32_t kWatchWaitMinMs = 2 * 60 * 1000, kWatchWaitMaxMs = 3 * 60 * 60 * 1000;

// College day scoreboards run 750 KB to 1.4 MB.
inline bool watch_by_event(League l) { return l == League::NCAA || l == League::MCBB; }

// Copied as a block; the away side is "team".
struct WatchGame {
  char event_id[16]{};
  uint8_t league{0};
  bool fav{false};    // the panel's team or a favorite plays
  uint8_t fails{0};   // its own reads failed in a row (watch_failed)
  uint32_t group{0};  // the source it came from (WatchSource)
  uint32_t away_id{0}, home_id{0};
  uint32_t read_cycle{0};   // the cycle of its last good read
  uint32_t tried_cycle{0};  // cycle of its last read attempt, good or not
  GameState state{GameState::NOT_FOUND};  // NOT_FOUND: not read yet
  bool completed{false}, team_home{false};
  bool down{false};       // football: a down to play
  bool try_words{false};  // football: last play said touchdown or conversion (safety)
  uint8_t period{0}, regulation{4}, possession{0}, bases{0}, goals{0}, team_reds{0}, opp_reds{0};
  int8_t outs{-1};
  Half half{Half::NONE};
  int16_t team_score{0}, opp_score{0};
  char clock[8]{};
  uint32_t play{0};  // ESPN's id for the last play, hashed (watch_hash); 0 none
  char away_abbr[6]{}, home_abbr[6]{};
  uint8_t away_rgb[3]{}, home_rgb[3]{};
  uint8_t away_rank{0}, home_rank{0};
};
static_assert(std::is_trivially_copyable<WatchGame>::value, "watched games copy as a block");
static_assert(sizeof(WatchGame) == 92, "eight watched games keep under 800 bytes of internal RAM");

// ESPN's hex colors come without '#' ("C8102E").
inline void watch_rgb(uint8_t (&out)[3], const std::string &hex) {
  const uint32_t c = hex.size() == 6 ? (uint32_t) strtoul(hex.c_str(), nullptr, 16) : 0xFFFFFF;
  out[0] = (uint8_t) (c >> 16);
  out[1] = (uint8_t) (c >> 8);
  out[2] = (uint8_t) c;
}

inline void watch_teams(WatchGame &w, const std::string &away, const std::string &home) {
  snprintf(w.away_abbr, sizeof(w.away_abbr), "%s", away.c_str());
  snprintf(w.home_abbr, sizeof(w.home_abbr), "%s", home.c_str());
}

inline bool watch_same(const WatchGame &g, uint8_t league, const std::string &event_id) {
  return g.league == league && event_id == g.event_id;
}

// Play ids are only compared, so hash them.
inline uint32_t watch_hash(const std::string &s) {
  if (s.empty())
    return 0;
  uint32_t h = 2166136261u;
  for (unsigned char c : s)
    h = (h ^ c) * 16777619u;
  return h != 0 ? h : 1;
}

inline int16_t watch_score(int v) { return (int16_t) (v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
inline uint8_t watch_byte(long v) { return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v); }

inline void watch_store(WatchGame &w, const GameSnapshot &g) {
  w.state = g.state;
  w.completed = g.completed;
  w.team_home = g.team_home;
  w.down = !g.down_distance.empty();
  w.try_words = false;
  for (const std::string *s : {&g.last_play, &g.play_type})
    for (const char *word : {"touchdown", "two-point", "two point", "2pt", "conversion"})
      w.try_words = w.try_words || detail::has_word(*s, word);
  w.period = watch_byte(g.period);
  w.regulation = g.nba.regulation;
  w.possession = watch_byte(g.possession);
  w.bases = g.mlb.bases;
  w.outs = g.mlb.outs;
  w.half = g.mlb.half;
  w.goals = watch_byte((long) g.soc.goals.size());
  w.team_reds = watch_byte(g.soc.team_reds);
  w.opp_reds = watch_byte(g.soc.opp_reds);
  w.team_score = watch_score(g.team_score);
  w.opp_score = watch_score(g.opp_score);
  snprintf(w.clock, sizeof(w.clock), "%s", g.display_clock.c_str());
  w.play = watch_hash(g.play_id);
}

// Play words dropped; detect_big_play needs only ids and flags.
inline GameSnapshot watch_prev(const WatchGame &w, Sport sport) {
  GameSnapshot p;
  p.valid = true;
  p.event_id = w.event_id;
  p.sport = sport;
  p.state = w.state;
  p.completed = w.completed;
  p.team_home = w.team_home;
  if (w.down)
    p.down_distance = "-";
  if (w.try_words)
    p.play_type = "touchdown";
  if (w.play != 0)
    p.play_id = std::to_string(w.play);
  p.period = w.period;
  p.nba.regulation = w.regulation;
  p.possession = w.possession;
  p.mlb.bases = w.bases;
  p.mlb.outs = w.outs;
  p.mlb.half = w.half;
  p.soc.goals.resize(w.goals);
  p.soc.team_reds = w.team_reds;
  p.soc.opp_reds = w.opp_reds;
  p.team_score = w.team_score;
  p.opp_score = w.opp_score;
  p.display_clock = w.clock;
  return p;
}

// Infers homers: day scoreboards often show a later play.
inline int watch_homer(const WatchGame &prev, const GameSnapshot &cur) {
  const Half h = cur.mlb.half;
  if (cur.state != GameState::IN || prev.state != GameState::IN || cur.period != prev.period || h != prev.half ||
      (h != Half::TOP && h != Half::BOTTOM) || prev.outs < 0 || cur.mlb.outs != prev.outs || cur.mlb.bases != 0)
    return 0;
  const bool team_bats = (h == Half::TOP) != cur.team_home;
  const int gain = detail::score_gain(team_bats ? prev.team_score : prev.opp_score,
                                      team_bats ? cur.team_score : cur.opp_score);
  const int on = (prev.bases & 1) + ((prev.bases >> 1) & 1) + ((prev.bases >> 2) & 1);
  return gain > 0 && gain == on + 1 ? gain : 0;
}

// Never FINAL: a finished game has nothing to switch to.
inline BigPlay watch_detect(const WatchGame &prev, GameSnapshot cur, uint32_t mask) {
  if (prev.state == GameState::NOT_FOUND)
    return BigPlay{};
  const GameSnapshot p = watch_prev(prev, cur.sport);
  cur.play_id = cur.play_id.empty() ? std::string() : std::to_string(watch_hash(cur.play_id));
  if (cur.sport == Sport::BASEBALL && !detail::home_run(cur)) {
    const int runs = watch_homer(prev, cur);
    if (runs > 0) {
      cur.play_type = "Home Run";
      cur.play_points = runs;
    }
  }
  return detect_big_play(p, cur, mask & ~alert_bit(Alert::FINAL), true);
}

inline Splash watch_splash(const WatchGame &prev, const GameSnapshot &cur, uint32_t mask, bool wide) {
  return big_play_splash(watch_detect(prev, cur, mask), cur, true, wide);
}

// Adopt the second area's read so plays never splash twice.
inline void watch_sync(WatchGame &w, const GameSnapshot &cur, uint32_t cycle) {
  watch_store(w, cur);
  w.read_cycle = w.tried_cycle = cycle;
  w.fails = 0;
}

// Spans rely on the sport-grouped Alert order.
inline uint32_t watch_sport_mask(Sport s) {
  auto span = [](Alert a, Alert b) { return (alert_bit(b) << 1) - alert_bit(a); };
  switch (s) {
    case Sport::FOOTBALL:
      return span(Alert::TOUCHDOWN, Alert::FUMBLE);
    case Sport::BASEBALL:
      return span(Alert::RUN, Alert::WALK_OFF);
    case Sport::HOCKEY:
      return alert_bit(Alert::HOCKEY_GOAL);
    case Sport::SOCCER:
      return span(Alert::SOCCER_GOAL, Alert::RED_CARD);
    case Sport::BASKETBALL:
      return span(Alert::LEAD_CHANGE, Alert::OVERTIME);
    default:
      return 0;
  }
}

// all: keep every live game for the second area.
struct WatchCtx {
  bool live{false};
  bool all{false};
  bool fav_only{false};
  uint32_t read_mask{0};
  uint8_t a2_league{0};
  std::string a2_event;
  std::pair<uint8_t, uint32_t> team{0, 0};         // the panel's team, id 0 for none
  std::vector<std::pair<uint8_t, uint32_t>> favs;  // league and ESPN id
  uint8_t board_league{0}, visit_league{0};
  std::string board_event, visit_event;
  bool board_live{false};  // the mode's game is on (watch_live)
  uint32_t mask{0};        // the alert types switched on (alerts.h)
  bool wide{false};        // splash words for two panels
};

// 0 skip, 1 watch, 2 favorite; ESPN leagues only.
inline int watch_wants_teams(const WatchCtx &c, uint8_t league, uint32_t away, uint32_t home) {
  if (((c.all ? c.read_mask : c.mask) & watch_sport_mask(league_sport((League) league))) == 0 ||
      !from_espn((League) league))
    return 0;
  const bool fav = game_has_team(league, away, home, c.favs.data(), c.favs.size()) ||
                   ((c.live || c.all) && game_has_team(league, away, home, &c.team, 1));
  if (fav)
    return 2;
  return (c.live && !c.fav_only) || c.all ? 1 : 0;
}

inline bool watch_may_alert(const WatchCtx &c, bool fav) { return fav || !c.fav_only; }

// Same codes; games already on screen are never watched.
inline int watch_wants(const WatchCtx &c, uint8_t league, const std::string &event_id, uint32_t away, uint32_t home) {
  if (event_id.empty() || (league == c.board_league && event_id == c.board_event) ||
      (league == c.visit_league && event_id == c.visit_event) ||
      (league == c.a2_league && event_id == c.a2_event))
    return 0;
  return watch_wants_teams(c, league, away, home);
}

// Lower is better: favorites, then close and late games.
inline int watch_rank(const WatchGame &g) {
  const int base = g.fav ? 0 : 1000;
  if (g.state == GameState::NOT_FOUND)
    return base + 425;
  const Sport s = league_sport((League) g.league);
  const int unit = s == Sport::FOOTBALL ? 8 : s == Sport::BASKETBALL ? 3 : 1;
  const int margin = std::abs(g.team_score - g.opp_score);
  const int scores = std::min((margin + unit - 1) / unit, 9);
  const int late = s == Sport::FOOTBALL ? 4 : s == Sport::BASKETBALL ? g.regulation : s == Sport::BASEBALL ? 7
                   : s == Sport::HOCKEY ? 3 : 2;
  return base + scores * 100 + (g.period >= late ? 0 : 150);
}

// NONE: the board's scan only adds games.
struct WatchScope {
  enum Kind : uint8_t { NONE, SOURCE, GAME } kind{NONE};
  uint8_t league{0};
  uint32_t group{0};
  const char *event_id{""};
  bool covers(const WatchGame &g) const {
    return kind == SOURCE ? g.league == league && g.group == group : kind == GAME && watch_same(g, league, event_id);
  }
};

// Capped at kWatchMax: vectors live in internal RAM.
inline void watch_keep(std::vector<WatchGame> &games, const WatchGame &g) {
  if (games.size() >= kWatchMax) {
    size_t worst = 0;
    for (size_t i = 1; i < games.size(); i++)
      if (watch_rank(games[i]) >= watch_rank(games[worst]))
        worst = i;
    if (watch_rank(g) >= watch_rank(games[worst]))
      return;
    games.erase(games.begin() + worst);
  }
  games.push_back(g);
}

// Unlisted covered games are over or no longer wanted.
inline void watch_merge(std::vector<WatchGame> &games, const std::vector<WatchGame> &fresh, const WatchScope &scope) {
  size_t keep = 0;
  for (size_t i = 0; i < games.size(); i++) {
    const WatchGame *f = nullptr;
    for (const auto &x : fresh)
      if (watch_same(x, games[i].league, games[i].event_id))
        f = &x;
    if ((f != nullptr ? f->state : games[i].state) != GameState::POST && (f != nullptr || !scope.covers(games[i])))
      games[keep++] = f != nullptr ? *f : games[i];
  }
  games.erase(games.begin() + keep, games.end());
  for (const auto &f : fresh) {
    bool known = false;
    for (const auto &g : games)
      known = known || watch_same(g, f.league, f.event_id);
    if (!known && f.state != GameState::POST)
      watch_keep(games, f);
  }
  for (size_t i = 1; i < games.size(); i++)  // stable, and never more than kWatchMax
    for (size_t k = i; k > 0 && watch_rank(games[k - 1]) > watch_rank(games[k]); k--)
      std::swap(games[k - 1], games[k]);
}

// Two failures drop it; its source re-finds live games.
inline void watch_failed(std::vector<WatchGame> &games, uint8_t league, const std::string &event_id) {
  for (size_t i = 0; i < games.size(); i++)
    if (watch_same(games[i], league, event_id) && ++games[i].fails >= 2) {
      games.erase(games.begin() + i);
      return;
    }
}

// fed: Live games' college games come from the board's scan.
struct WatchSource {
  uint8_t league{0};
  uint32_t group{0};
  bool by_event{false};
  bool fed{false};
  uint32_t read_cycle{0};  // the cycle that last read its scoreboard
  uint32_t wait_until{0};  // millis(): no scoreboard read before this (watch_wait_until); 0 none
};

// ESPN has no scoreboard for minors, AHL or ECHL.
inline std::vector<WatchSource> watch_sources(bool live, const std::vector<League> &leagues,
                                              const std::vector<const Team *> &favs, uint32_t mask) {
  std::vector<WatchSource> out;
  auto add = [&](League l, uint32_t group) {
    if ((mask & watch_sport_mask(league_sport(l))) == 0 || !from_espn(l))
      return;
    for (const auto &s : out)
      if (s.league == (uint8_t) l && s.group == group)
        return;
    WatchSource s;
    s.league = (uint8_t) l;
    s.group = group;
    s.by_event = !live || watch_by_event(l);
    s.fed = live && watch_by_event(l);
    out.push_back(s);
  };
  if (live) {
    for (League l : leagues)
      add(l, 0);
  } else {
    for (const Team *t : favs)
      if (t != nullptr)
        add(t->league, watch_by_event(t->league) ? t->group : 0);
  }
  return out;
}

// game -1 reads the day scoreboard; oldest-tried goes first.
struct WatchRead {
  int source{-1};
  int game{-1};
  bool waiting{false};
};

template<typename Fits>
inline WatchRead watch_next(const std::vector<WatchSource> &src, const std::vector<WatchGame> &games, uint32_t cycle,
                            uint32_t now, Fits fits) {
  WatchRead best, none;
  uint32_t best_age = 0;
  auto consider = [&](size_t s, int game, uint32_t tried, bool event) {
    const uint32_t age = cycle - tried;
    if (best.source >= 0 && age <= best_age)
      return;  // one as old or older, earlier in order, fits already
    if (!fits(src[s], event)) {
      none.waiting = true;
      return;
    }
    best = WatchRead{(int) s, game, false};
    best_age = age;
  };
  for (size_t s = 0; s < src.size(); s++) {
    const WatchSource &w = src[s];
    // Only its oldest-tried game; others take as long.
    int oldest = -1;
    for (size_t i = 0; w.by_event && i < games.size(); i++)
      if (games[i].league == w.league && games[i].group == w.group && games[i].tried_cycle != cycle &&
          (oldest < 0 || cycle - games[i].tried_cycle > cycle - games[oldest].tried_cycle))
        oldest = (int) i;
    if (oldest >= 0)
      consider(s, oldest, games[oldest].tried_cycle, true);
    if (!w.fed && w.read_cycle != cycle && (w.wait_until == 0 || (int32_t) (now - w.wait_until) >= 0))
      consider(s, -1, w.read_cycle, false);
  }
  return best.source >= 0 ? best : none;
}

inline WatchRead watch_next(const std::vector<WatchSource> &src, const std::vector<WatchGame> &games, uint32_t cycle,
                            uint32_t now) {
  return watch_next(src, games, cycle, now, [](const WatchSource &, bool) { return true; });
}

// Returns 0 to read every cycle; never 0 otherwise.
inline uint32_t watch_wait_until(const WatchSource &s, bool on, uint32_t now, int64_t now_epoch, int64_t next_start) {
  if (on && !s.by_event)
    return 0;
  const uint32_t at = now + no_live_rescan_ms(now_epoch, next_start, kWatchWaitMinMs, kWatchWaitMaxMs);
  return at != 0 ? at : 1;
}

inline uint32_t watch_expect_ms(uint32_t last_ms, bool event) {
  return last_ms != 0 ? last_ms + kWatchMarginMs : event ? kWatchEventMs : kWatchBoardMs;
}

// Never delay the board's own next poll.
inline bool watch_read_fits(uint32_t now, uint32_t next_board_ms, uint32_t expect_ms) {
  return (int32_t) (next_board_ms - now) >= (int32_t) expect_ms;
}

// A hung read's time says nothing about good reads.
static const uint8_t kWatchSlowFailsMax = 3;
struct WatchTimes {
  static_assert(kLeagueCount < 32, "a bit per kind of read");
  uint16_t ms[kLeagueCount + 1]{};
  uint8_t slow_fails[kLeagueCount + 1]{};  // such failures in a row, at most kWatchSlowFailsMax
  uint8_t held[kLeagueCount + 1]{};        // cycles still to wait, this one counted
  uint32_t skipped{0};
  static size_t kind(uint8_t league, bool event) { return event ? kLeagueCount : league < kLeagueCount ? league : 0; }
  // Held kinds aren't marked skipped, so never sped up.
  bool fits(uint32_t now, uint32_t due, uint8_t league, bool event) {
    const size_t k = kind(league, event);
    if (this->held[k] != 0)
      return false;
    if (watch_read_fits(now, due, watch_expect_ms(this->ms[k], event)))
      return true;
    this->skipped |= 1u << k;
    return false;
  }
  void started(uint8_t league, bool event) { this->skipped &= ~(1u << kind(league, event)); }
  // Slow failures hold the kind 1, 3, then 7 cycles.
  void took(uint8_t league, bool event, uint32_t took_ms, bool ok) {
    const size_t k = kind(league, event);
    const uint16_t t = (uint16_t) std::min(took_ms, kWatchTimedMaxMs);
    if (ok) {
      this->ms[k] = t;
      this->slow_fails[k] = 0;
      return;
    }
    if (t <= watch_expect_ms(this->ms[k], event))
      return;
    if (this->slow_fails[k] < kWatchSlowFailsMax)
      this->slow_fails[k]++;
    this->held[k] = (uint8_t) ((1u << this->slow_fails[k]) - 1);
  }
  // Shrink skipped kinds' estimate so slow reads aren't starved.
  void cycle() {
    for (size_t k = 0; k <= kLeagueCount; k++) {
      if ((this->skipped >> k) & 1)
        this->ms[k] -= this->ms[k] / 8;
      if (this->held[k] != 0)
        this->held[k]--;
    }
    this->skipped = 0;
  }
};

// Waits out MLB or HockeyTech backoff; left 0 means none.
inline uint32_t watch_after_wait(uint32_t at, uint32_t now, uint32_t left) {
  return left != 0 && (int32_t) (at - (now + left)) < 0 ? now + left : at;
}

// Live scans ESPN anyway; Favorites backs off per team.
inline bool watch_refusal_holds_board(bool favorites, bool live, uint8_t board_league) {
  return !favorites && (live || from_espn((League) board_league));
}

// Favorites ticks each second but rarely reads ESPN.
inline uint32_t watch_fav_due(uint32_t now, bool locked, uint32_t poll_ms, uint32_t poll_every, uint32_t refresh_at,
                              uint32_t poll_left = 0) {
  if (!locked)
    return refresh_at;
  const uint32_t poll = watch_after_wait(poll_ms == 0 ? now : poll_ms + poll_every, now, poll_left);
  return (int32_t) (poll - refresh_at) < 0 ? poll : refresh_at;
}

// Games kept off last cycle; hashed ids, 0 none.
struct WatchKept {
  uint8_t board_league{0}, visit_league{0};
  uint32_t board{0}, visit{0};
};

// A live game leaving the board clears its league's wait.
inline void watch_board_moved(std::vector<WatchSource> &src, WatchKept &kept, const WatchCtx &c) {
  WatchKept now;
  if (c.board_live && !c.board_event.empty()) {
    now.board_league = c.board_league;
    now.board = watch_hash(c.board_event);
  }
  now.visit_league = c.visit_league;
  now.visit = watch_hash(c.visit_event);
  auto left = [&](uint8_t league, uint32_t event) {
    if (event == 0 || (league == now.board_league && event == now.board) ||
        (league == now.visit_league && event == now.visit))
      return;
    for (auto &s : src)
      if (s.league == league)
        s.wait_until = 0;
  };
  left(kept.board_league, kept.board);
  left(kept.visit_league, kept.visit);
  kept = now;
}

struct WatchAlert {
  bool valid{false};
  bool fav{false};
  std::string event_id;
  uint32_t away_id{0}, home_id{0};
  std::string away_abbr, home_abbr;
  Splash splash;
};

// Bets accept either side as "team" (bets.h).
inline bool watch_bet_game(const GameSnapshot &g, uint8_t league, BetGame &out) {
  if (!g.valid || g.state == GameState::NOT_FOUND || is_board_sport(g.sport) ||
      league >= kLeagueCount || league_sport((League) league) != g.sport)
    return false;
  out = BetGame{};
  out.event = bet_event_id(g.event_id);
  if (out.event == 0)
    return false;
  out.league = league;
  out.team_id = g.team_id;
  out.opp_id = g.opp_id;
  out.team_score = g.team_score;
  out.opp_score = g.opp_score;
  out.kickoff = g.kickoff_epoch;
  out.phase = g.state == GameState::PRE  ? BetPhase::PRE
              : g.state == GameState::IN ? BetPhase::LIVE
              : g.completed              ? BetPhase::FINAL
                                         : BetPhase::OFF;
  return true;
}

// Filled on the main loop, read on the fetch task.
struct WatchJob {
  uint8_t league{0};
  uint32_t group{0};
  std::string event_id;  // one game; empty reads the source's day scoreboard
  uint32_t away_id{0};
  uint32_t cycle{0};
  WatchCtx ctx;
  std::vector<WatchGame> games;
  std::vector<uint32_t> bet_events;
  bool ok{false};
  uint32_t took_ms{0};
  std::vector<WatchGame> fresh;
  std::vector<BetGame> bets;
  int64_t next_start{0};  // the earliest wanted game still to start, 0 none
  WatchAlert alert;
};

// Just-ended games too: last play may be a walk-off.
inline bool watch_want_day(WatchJob &job, const DayGame &d) {
  if (d.state == GameState::PRE) {
    if (!d.event_id.empty() && watch_wants_teams(job.ctx, job.league, d.away_id, d.home_id) != 0 && !d.time_tbd &&
        d.kickoff_epoch > 0 && (job.next_start == 0 || d.kickoff_epoch < job.next_start))
      job.next_start = d.kickoff_epoch;
    return false;
  }
  if (watch_wants(job.ctx, job.league, d.event_id, d.away_id, d.home_id) == 0)
    return false;
  if (d.state == GameState::IN)
    return true;
  for (const auto &g : job.games)
    if (watch_same(g, job.league, d.event_id))
      return d.state == GameState::POST && g.state == GameState::IN;
  return false;
}

// A favorite's big play beats others in one read.
inline void watch_take(WatchJob &job, GameSnapshot &cur) {
  const int wants = watch_wants(job.ctx, job.league, cur.event_id, cur.team_id, cur.opp_id);
  if (wants == 0)
    return;
  WatchGame g;
  snprintf(g.event_id, sizeof(g.event_id), "%s", cur.event_id.c_str());
  g.league = job.league;
  g.group = job.group;
  g.away_id = cur.team_id;
  g.home_id = cur.opp_id;
  g.fav = wants == 2;
  g.read_cycle = g.tried_cycle = job.cycle;
  watch_store(g, cur);
  watch_teams(g, cur.team_abbr, cur.opp_abbr);
  watch_rgb(g.away_rgb, cur.team_color);
  watch_rgb(g.home_rgb, cur.opp_color);
  g.away_rank = cur.team_rank;
  g.home_rank = cur.opp_rank;
  for (const auto &prev : job.games) {
    if (!watch_same(prev, job.league, cur.event_id) || job.cycle - prev.read_cycle > kWatchStaleCycles ||
        !watch_may_alert(job.ctx, g.fav) || (job.alert.valid && (job.alert.fav || !g.fav)))
      continue;
    const BigPlay p = watch_detect(prev, cur, job.ctx.mask);
    if (p.type == Alert::NONE)
      continue;
    job.alert.valid = true;
    job.alert.fav = g.fav;
    job.alert.event_id = cur.event_id;
    job.alert.away_id = cur.team_id;
    job.alert.home_id = cur.opp_id;
    job.alert.away_abbr = cur.team_abbr;
    job.alert.home_abbr = cur.opp_abbr;
    job.alert.splash = big_play_splash(p, cur, true, job.ctx.wide);
  }
  watch_keep(job.fresh, g);
  const uint32_t event = bet_event_id(cur.event_id);
  BetGame b;
  if (event != 0 && std::find(job.bet_events.begin(), job.bet_events.end(), event) != job.bet_events.end() &&
      job.bets.size() < kBetSlots && watch_bet_game(cur, job.league, b))
    job.bets.push_back(b);
}

template<typename TInput> bool watch_read_day(TInput &input, WatchJob &job) {
  const Sport sport = league_sport((League) job.league);
  JsonDocument filter(detail::json_allocator());
  detail::fill_game_filter(filter["events"].add<JsonObject>(), sport);
  JsonDocument doc(detail::json_allocator());
  if (deserializeJson(doc, input, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40)))
    return false;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    const DayGame d = detail::day_game(ev);
    GameSnapshot g;
    if (d.away_id != 0 && watch_want_day(job, d) && detail::game_from_event(ev, sport, d.away_id, g))
      watch_take(job, g);
  }
  return true;
}

// Live from kickoff even while ESPN still says pre.
inline bool watch_live(const GameSnapshot &g, int64_t now_epoch) {
  if (g.valid && g.sport == Sport::GOLF)
    return g.golf.phase == GolfPhase::LIVE;
  if (g.valid && g.sport == Sport::RACING)
    return racing_on_track(g, now_epoch);
  return g.valid && (g.state == GameState::IN || (g.state == GameState::PRE && !g.time_tbd && g.kickoff_epoch > 0 &&
                                                  now_epoch >= g.kickoff_epoch));
}

// Leaderboards name no team, so the leaderboard counts.
inline bool watch_own_game(const GameSnapshot &g, uint8_t league, uint32_t team_id) {
  if (!g.valid || team_id == 0 || league >= kLeagueCount)
    return false;
  if (is_leaderboard((League) league))
    return is_board_sport(g.sport) && league_sport((League) league) == g.sport;
  return g.team_id == team_id || g.opp_id == team_id;
}

// Skip missed plays, but still splash a missed final.
inline GameSnapshot watch_back_prev(GameSnapshot before, const GameSnapshot &cur) {
  if (!detail::same_game(before, cur))
    return GameSnapshot{};
  if (is_board_sport(cur.sport))
    return before;
  if (!detail::went_final(before, cur))
    return GameSnapshot{};
  GameSnapshot p = cur;
  p.state = before.state;
  p.completed = before.completed;
  return p;
}

struct WatchSwitch {
  bool allowed{true};     // not held by Follow this game (follow.h)
  bool splash{false};     // the board's own splash is up
  bool own_live{false};   // the mode's game is the panel's team, live (watch_live)
  bool alert_fav{false};  // big play's game has the team or a favorite
  bool from_mine{false};  // "Switch from my team"
  bool away_long{false};  // away from the mode's game a full stay already
};

// away_long: the mode's game returns within two stays.
inline bool watch_may_switch(const WatchSwitch &s) {
  if (!s.allowed || s.splash || s.away_long)
    return false;
  return !s.own_live || (s.alert_fav && s.from_mine);
}

// Setting stored in tens of seconds; 0 means default.
inline uint32_t watch_stay_ms(uint8_t tens) { return (tens != 0 ? tens : 6) * 10000u; }

inline bool watch_visit_over(uint32_t now, uint32_t since_ms, uint32_t stay_ms, const WatchSwitch &s) {
  return !s.allowed || now - since_ms >= stay_ms || (s.own_live && !(s.alert_fav && s.from_mine));
}

// Follow pins the mode's game, never a visit's.
inline const GameSnapshot &watch_mode_game(const GameSnapshot &board, const GameSnapshot *saved) {
  return saved != nullptr ? *saved : board;
}

}  // namespace espn
