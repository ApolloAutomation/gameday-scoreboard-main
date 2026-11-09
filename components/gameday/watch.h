// The big-play watcher ("Switch to big plays in other games"): other live
// games, read between the board's own polls, whose big plays take the board
// for a while. Pure, no ESPHome includes, so the host tests cover it: which
// games are watched, what one read of a game says against the last, when a
// read may run, and when the board switches and goes back. The reads are
// gameday.cpp's (start_watch_job_, run_watch_job_, apply_watch_job_).
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

// At most this many games are watched, and each is read about once a cycle.
// A game's last read from more cycles back than kWatchStaleCycles is too old
// to tell a big play from: the read after it only starts over.
static const size_t kWatchMax = 8;
static const uint32_t kWatchCycleMs = 45 * 1000;
static const uint32_t kWatchStaleCycles = 2;
// What a read is expected to take until one has been timed: a league's day
// scoreboard, or one game on its own. A timed one gets the margin on top, and
// its time is kept to the minute.
static const uint32_t kWatchBoardMs = 5000, kWatchEventMs = 2500, kWatchMarginMs = 1000, kWatchTimedMaxMs = 60000;
// The board's own splash is up 4 s (tt_hide_splash in gameday-live.yaml): no
// switch while it is.
static const uint32_t kWatchSplashMs = 5000;
// A day scoreboard read that has nothing more to give waits for the next
// start of a wanted game, between these (watch_wait_until).
static const uint32_t kWatchWaitMinMs = 2 * 60 * 1000, kWatchWaitMaxMs = 3 * 60 * 60 * 1000;

// A college league's day scoreboard is large (750 KB to 1.4 MB with every
// FBS game): its games are read one by one instead.
inline bool watch_by_event(League l) { return l == League::NCAA || l == League::MCBB; }

// One watched game, as its last read left it: who plays, where it was found,
// and what detect_big_play needs from the read before (watch_prev rebuilds
// it). Plain characters, copied as a block, 72 bytes. The away side is
// "team", as in the live modes.
struct WatchGame {
  char event_id[16]{};
  uint8_t league{0};
  bool fav{false};    // the panel's team or a favorite plays
  uint8_t fails{0};   // its own reads failed in a row (watch_failed)
  uint32_t group{0};  // the source it came from (WatchSource)
  uint32_t away_id{0}, home_id{0};
  uint32_t read_cycle{0};   // the cycle of its last good read
  uint32_t tried_cycle{0};  // the cycle that last tried to read it, good or not
  GameState state{GameState::NOT_FOUND};  // NOT_FOUND: not read yet
  bool completed{false}, team_home{false};
  bool down{false};       // football: a down to play
  bool try_words{false};  // football: the last play says touchdown or conversion (alerts.h, safety)
  uint8_t period{0}, regulation{4}, possession{0}, bases{0}, goals{0}, team_reds{0}, opp_reds{0};
  int8_t outs{-1};
  Half half{Half::NONE};
  int16_t team_score{0}, opp_score{0};
  char clock[8]{};
  uint32_t play{0};  // ESPN's id for the last play, hashed (watch_hash); 0 none
};
static_assert(std::is_trivially_copyable<WatchGame>::value, "watched games copy as a block");
static_assert(sizeof(WatchGame) == 72, "eight watched games keep under 600 bytes of internal RAM");

inline bool watch_same(const WatchGame &g, uint8_t league, const std::string &event_id) {
  return g.league == league && event_id == g.event_id;
}

// ESPN's play ids only need comparing: 32 bits of FNV-1a, 0 for none.
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

// A read of the game into its WatchGame.
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

// The read before as detect_big_play reads it. The words of the last play
// aren't kept: a turnover needs ESPN's play ids to count as new, and a safety
// needs only whether the play before said touchdown or conversion.
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

// A home run told by the score and the bases, for a read whose last play is
// a later one (a day scoreboard's often is): the side batting scored the
// runners on base plus one, the bases are empty, and it is the same half
// inning with the same outs. The runs, or 0.
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

// The big play between a watched game's last read and this one, cur, of the
// types in mask. Every side counts, as in the live modes. Nothing on the
// first read of a game, and never basketball's FINAL: the game is over, so
// there is nothing left to switch to.
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

// The alert types the watcher can switch for in a sport: none for golf and
// racing, and never basketball's FINAL.
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

// Who the watched games are for. In Live games every live game, or with
// Favorite teams only on those with the panel's team or a favorite; in My
// team and Favorite teams, the favorites' games. Only sports with a big play
// switched on, and never the mode's own game (board) or the one a big play
// has the board on (visit).
struct WatchCtx {
  bool live{false};
  bool fav_only{false};
  std::pair<uint8_t, uint32_t> team{0, 0};         // the panel's team, id 0 for none
  std::vector<std::pair<uint8_t, uint32_t>> favs;  // league and ESPN id
  uint8_t board_league{0}, visit_league{0};
  std::string board_event, visit_event;
  uint32_t mask{0};  // the alert types switched on (alerts.h)
  bool wide{false};  // splash words for two panels
};

// 0: not watched. 1: watched. 2: watched, and the panel's team or a favorite plays.
inline int watch_wants(const WatchCtx &c, uint8_t league, const std::string &event_id, uint32_t away, uint32_t home) {
  if (event_id.empty() || (league == c.board_league && event_id == c.board_event) ||
      (league == c.visit_league && event_id == c.visit_event) ||
      (c.mask & watch_sport_mask(league_sport((League) league))) == 0)
    return 0;
  const bool fav = game_has_team(league, away, home, c.favs.data(), c.favs.size()) ||
                   (c.live && game_has_team(league, away, home, &c.team, 1));
  if (fav)
    return 2;
  return c.live && !c.fav_only ? 1 : 0;
}

// Favorites first, then the closest games (in scores: 8 points in football,
// 3 in basketball, 1 elsewhere), late ones ahead of early ones: a score in
// the last period beats a tie in the first, which beats two scores late. A
// game not read yet sits in the middle. Lower is better.
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

// Which watched games one read speaks for: none (the board's scan only adds
// games), every game of a source (league and group: its day scoreboard), or
// one game (league and event_id).
struct WatchScope {
  enum Kind : uint8_t { NONE, SOURCE, GAME } kind{NONE};
  uint8_t league{0};
  uint32_t group{0};
  const char *event_id{""};
  bool covers(const WatchGame &g) const {
    return kind == SOURCE ? g.league == league && g.group == group : kind == GAME && watch_same(g, league, event_id);
  }
};

// Adds g to the best kWatchMax of games (watch_rank): a full list drops its
// worst game for g when g ranks better, so no list of games, and no vector's
// memory (internal RAM on the panel), ever grows past kWatchMax.
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

// Folds one read's games (fresh, each with its new read) into the watched
// ones. A game the read covers and no longer lists is over or no longer
// wanted, and a game seen to end drops out after that read; then the best
// kWatchMax stay, in watch_rank order.
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

// A game's own read failed: a second failure in a row drops it (ESPN moved
// or removed it, say); its source finds it again if it is still on.
inline void watch_failed(std::vector<WatchGame> &games, uint8_t league, const std::string &event_id) {
  for (size_t i = 0; i < games.size(); i++)
    if (watch_same(games[i], league, event_id) && ++games[i].fails >= 2) {
      games.erase(games.begin() + i);
      return;
    }
}

// Where watched games are found: a league's day scoreboard (group 0), or a
// college conference's (team_day_url). In Live games a pro league's day
// scoreboard is read every cycle, since it gives every game's score at once.
// Its games are read one by one (by_event) for a college league, whose day
// scoreboard is large, and for the favorites in My team and Favorite teams,
// a game on its own being a twentieth of a league's day or less; then the
// day scoreboard only finds them, at the next start, and a college league's
// games in Live games come from the board's own scan instead (fed).
struct WatchSource {
  uint8_t league{0};
  uint32_t group{0};
  bool by_event{false};
  bool fed{false};
  uint32_t read_cycle{0};  // the cycle that last read its scoreboard
  uint32_t wait_until{0};  // millis(): no scoreboard read before this (watch_wait_until); 0 none
};

// Live games: each league it follows. My team and Favorite teams: each
// favorite's league, a college favorite's by its conference (Team::group).
// Only leagues of a sport with a big play in mask (watch_sport_mask), so never
// golf or racing.
inline std::vector<WatchSource> watch_sources(bool live, const std::vector<League> &leagues,
                                              const std::vector<const Team *> &favs, uint32_t mask) {
  std::vector<WatchSource> out;
  auto add = [&](League l, uint32_t group) {
    if ((mask & watch_sport_mask(league_sport(l))) == 0)
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

// The next read this cycle that fits now (fits(source, event) says whether
// that kind of read does: watch_read_fits): one game of a by_event source
// (game, an index into games), or a source's day scoreboard (game -1) once
// its wait is over. Source -1 when none fits now (waiting: some are still
// due) or the cycle's reads are done. Each read is tried once a cycle, and
// one that doesn't fit never holds up the reads after it.
struct WatchRead {
  int source{-1};
  int game{-1};
  bool waiting{false};
};

template<typename Fits>
inline WatchRead watch_next(const std::vector<WatchSource> &src, const std::vector<WatchGame> &games, uint32_t cycle,
                            uint32_t now, Fits fits) {
  WatchRead none;
  for (size_t s = 0; s < src.size(); s++) {
    const WatchSource &w = src[s];
    for (size_t i = 0; w.by_event && i < games.size(); i++)
      if (games[i].league == w.league && games[i].group == w.group && games[i].tried_cycle != cycle) {
        if (fits(w, true))
          return WatchRead{(int) s, (int) i, false};
        none.waiting = true;
        break;  // its other games take as long
      }
    if (!w.fed && w.read_cycle != cycle && (w.wait_until == 0 || (int32_t) (now - w.wait_until) >= 0)) {
      if (fits(w, false))
        return WatchRead{(int) s, -1, false};
      none.waiting = true;
    }
  }
  return none;
}

inline WatchRead watch_next(const std::vector<WatchSource> &src, const std::vector<WatchGame> &games, uint32_t cycle,
                            uint32_t now) {
  return watch_next(src, games, cycle, now, [](const WatchSource &, bool) { return true; });
}

// When a source's day scoreboard is read next, after a read at now (millis;
// now_epoch the clock) that found a wanted game on or not (on) and the
// earliest wanted start still to come (next_start, 0 none): every cycle
// while a game is on and the scoreboard is how its games are read, else at
// that start, at least 2 minutes and at most 3 hours on.
inline uint32_t watch_wait_until(const WatchSource &s, bool on, uint32_t now, int64_t now_epoch, int64_t next_start) {
  if (on && !s.by_event)
    return 0;
  const uint32_t at = now + no_live_rescan_ms(now_epoch, next_start, kWatchWaitMinMs, kWatchWaitMaxMs);
  return at != 0 ? at : 1;
}

// How long a read is expected to take: the last one's time (ms, 0 untimed)
// and a margin, or the default.
inline uint32_t watch_expect_ms(uint32_t last_ms, bool event) {
  return last_ms != 0 ? last_ms + kWatchMarginMs : event ? kWatchEventMs : kWatchBoardMs;
}

// A read may start now only when the board's next poll is at least its
// expected time away, so the board isn't kept waiting for it.
inline bool watch_read_fits(uint32_t now, uint32_t next_board_ms, uint32_t expect_ms) {
  return (int32_t) (next_board_ms - now) >= (int32_t) expect_ms;
}

// The watcher's read times by kind: each league's day scoreboard, and the
// last one a game on its own (0 untimed), with a bit for each kind no gap
// could take this cycle.
struct WatchTimes {
  static_assert(kLeagueCount < 32, "a bit per kind of read");
  uint16_t ms[kLeagueCount + 1]{};
  uint32_t skipped{0};
  static size_t kind(uint8_t league, bool event) { return event ? kLeagueCount : league < kLeagueCount ? league : 0; }
  // Whether that read fits before due, the board's next read (watch_next's
  // fits); one that doesn't is marked until a read of its kind starts.
  bool fits(uint32_t now, uint32_t due, uint8_t league, bool event) {
    const size_t k = kind(league, event);
    if (watch_read_fits(now, due, watch_expect_ms(this->ms[k], event)))
      return true;
    this->skipped |= 1u << k;
    return false;
  }
  void started(uint8_t league, bool event) { this->skipped &= ~(1u << kind(league, event)); }
  // A read took took_ms: a good one's own time is kept, a failed one's only
  // when longer, so a read that hung until the HTTP timeout isn't expected
  // to be quick the next time.
  void took(uint8_t league, bool event, uint32_t took_ms, bool ok) {
    uint16_t &m = this->ms[kind(league, event)];
    const uint16_t t = (uint16_t) std::min(took_ms, kWatchTimedMaxMs);
    m = ok || t > m ? t : m;
  }
  // A new cycle: a kind no gap could take all the last one is expected an
  // eighth quicker, so a slow read gets its turn, and a new time, before
  // long instead of never.
  void cycle() {
    for (size_t k = 0; k <= kLeagueCount; k++)
      if ((this->skipped >> k) & 1)
        this->ms[k] -= this->ms[k] / 8;
    this->skipped = 0;
  }
};

// Favorite teams ticks every second (FAV_TICK) for its playlist and reads
// ESPN on few of those ticks, so a watcher read there fits against its next
// read instead (millis()): the locked game's next poll (polled at poll_ms,
// every poll_every; not yet: now), or refresh_at, the next favorite's team
// read, whichever comes first.
inline uint32_t watch_fav_due(uint32_t now, bool locked, uint32_t poll_ms, uint32_t poll_every, uint32_t refresh_at) {
  if (!locked)
    return refresh_at;
  const uint32_t poll = poll_ms == 0 ? now : poll_ms + poll_every;
  return (int32_t) (poll - refresh_at) < 0 ? poll : refresh_at;
}

// At a new cycle, with key what the watcher kept off at the last (WatchCtx's
// board and visit games): when that changed, every day scoreboard is read
// again this cycle, waits or not, as a game that just left the board can be
// one to watch now.
inline void watch_board_moved(std::vector<WatchSource> &src, uint32_t &key, const WatchCtx &c) {
  const uint32_t k =
      watch_hash(std::to_string(c.board_league) + c.board_event + "/" + std::to_string(c.visit_league) + c.visit_event);
  if (k == key)
    return;
  key = k;
  for (auto &s : src)
    s.wait_until = 0;
}

// A big play found in a watched game, and what the board needs to go there.
struct WatchAlert {
  bool valid{false};
  bool fav{false};
  std::string event_id;
  uint32_t away_id{0}, home_id{0};
  std::string away_abbr, home_abbr;
  Splash splash;
};

// A game as the bets read it (bets.h, either side as "team"), from league;
// false when it can't be a bet's game: no game, no event id a bet keeps, a
// leaderboard, or a game of another sport than the league's.
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

// One read for the watcher, filled on the main loop and read on the fetch
// task: what to read, the watched games' last reads (a copy), and what it
// found. bet_events are the games the bets are tied to already (bets.h):
// the read hands back each one it takes, as bets, so those bets keep its
// details and take its result, though it never ties a bet to a game.
struct WatchJob {
  uint8_t league{0};
  uint32_t group{0};
  std::string event_id;  // one game (its away side away_id); empty: the source's day scoreboard
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

// Should the read take this game of the day's scoreboard in full: a wanted
// game that is on, or one watched as on that has ended (its last play can
// be a walk-off or a buzzer beater). A wanted game still to start only
// moves next_start.
inline bool watch_want_day(WatchJob &job, const DayGame &d) {
  if (watch_wants(job.ctx, job.league, d.event_id, d.away_id, d.home_id) == 0)
    return false;
  if (d.state == GameState::PRE) {
    if (!d.time_tbd && d.kickoff_epoch > 0 && (job.next_start == 0 || d.kickoff_epoch < job.next_start))
      job.next_start = d.kickoff_epoch;
    return false;
  }
  if (d.state == GameState::IN)
    return true;
  for (const auto &g : job.games)
    if (watch_same(g, job.league, d.event_id))
      return d.state == GameState::POST && g.state == GameState::IN;
  return false;
}

// One game the read took, cur (its away side as "team"): its new read into
// fresh, and its big play against the last read into alert. A favorite's big
// play wins over another game's in the same read.
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
  for (const auto &prev : job.games) {
    if (!watch_same(prev, job.league, cur.event_id) || job.cycle - prev.read_cycle > kWatchStaleCycles ||
        (job.alert.valid && (job.alert.fav || !g.fav)))
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

// A day scoreboard (scan_url, team_day_url) for one read: each game
// watch_want_day picks, read as parse_event reads a game on its own (its
// away side as "team"), into watch_take. False on a JSON error.
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

// The panel's own team's game counts as live from its start time on, even
// while ESPN still says it is to come, or while the board's read of it is
// from before another game's big play took the board.
inline bool watch_live(const GameSnapshot &g, int64_t now_epoch) {
  return g.valid && (g.state == GameState::IN || (g.state == GameState::PRE && !g.time_tbd && g.kickoff_epoch > 0 &&
                                                  now_epoch >= g.kickoff_epoch));
}

// What decides a switch to a watched game's big play.
struct WatchSwitch {
  bool allowed{true};     // nothing holds the board on its game (Follow this game, follow.h)
  bool splash{false};     // the board's own splash is up
  bool own_live{false};   // the mode's game is the panel's team, live (watch_live)
  bool alert_fav{false};  // the big play's game has the panel's team or a favorite
  bool from_mine{false};  // "Switch from my team"
  bool away_long{false};  // the board has been away from the mode's game a stay already
};

// The panel's own team's live game is never left for another game's big
// play, unless a favorite plays in that one and "Switch from my team" is on.
// A big play elsewhere while the board is away can move it on to that game
// only during the first stay, so the mode's game is back within two.
inline bool watch_may_switch(const WatchSwitch &s) {
  if (!s.allowed || s.splash || s.away_long)
    return false;
  return !s.own_live || (s.alert_fav && s.from_mine);
}

// "Stay on a big play": kept in tens of seconds, 0 for the default minute.
inline uint32_t watch_stay_ms(uint8_t tens) { return (tens != 0 ? tens : 6) * 10000u; }

// Back to what the mode was showing: the board has been on the big play's
// game for the stay, the panel's team's game started meanwhile and that big
// play may not hold the board from it, or Follow this game now holds the
// mode's game (s, as for watch_may_switch), which then goes straight back up.
inline bool watch_visit_over(uint32_t now, uint32_t since_ms, uint32_t stay_ms, const WatchSwitch &s) {
  return !s.allowed || now - since_ms >= stay_ms || (s.own_live && !(s.alert_fav && s.from_mine));
}

// The game Follow this game pins (follow.h): the mode's own, which a visit
// saved for the way back (saved, nullptr with no visit), never the big
// play's game the board is on for a moment.
inline const GameSnapshot &watch_mode_game(const GameSnapshot &board, const GameSnapshot *saved) {
  return saved != nullptr ? *saved : board;
}

}  // namespace espn
