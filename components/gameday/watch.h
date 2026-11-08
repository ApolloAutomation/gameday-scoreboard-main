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
// scoreboard, or one game on its own. A timed one gets the margin on top.
static const uint32_t kWatchBoardMs = 5000, kWatchEventMs = 2500, kWatchMarginMs = 1000;
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
// it). Plain characters, copied as a block. The away side is "team", as in
// the live modes.
struct WatchGame {
  char event_id[16]{};
  uint8_t league{0};
  uint32_t group{0};  // the source it came from (WatchSource)
  uint32_t away_id{0}, home_id{0};
  bool fav{false};          // the panel's team or a favorite plays
  uint32_t read_cycle{0};   // the cycle that last read it
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

// Who the watched games are for. In Live games every live game, or with
// Favorite teams only on those with the panel's team or a favorite; in My
// team and Favorite teams, the favorites' games. Never the board's own game.
struct WatchCtx {
  bool live{false};
  bool fav_only{false};
  std::pair<uint8_t, uint32_t> team{0, 0};         // the panel's team, id 0 for none
  std::vector<std::pair<uint8_t, uint32_t>> favs;  // league and ESPN id
  uint8_t board_league{0};
  std::string board_event;
  uint32_t mask{0};  // the alert types switched on (alerts.h)
  bool wide{false};  // splash words for two panels
};

// 0: not watched. 1: watched. 2: watched, and the panel's team or a favorite plays.
inline int watch_wants(const WatchCtx &c, uint8_t league, const std::string &event_id, uint32_t away, uint32_t home) {
  if (event_id.empty() || (league == c.board_league && event_id == c.board_event))
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

// Folds one read's games (fresh, each with its new read) into the watched
// ones. A game the read covers and no longer lists is over or no longer
// wanted, and a game seen to end drops out after that read; then the best
// kWatchMax stay, in watch_rank order.
inline void watch_merge(std::vector<WatchGame> &games, const std::vector<WatchGame> &fresh, const WatchScope &scope) {
  for (const auto &f : fresh) {
    size_t i = 0;
    while (i < games.size() && !watch_same(games[i], f.league, f.event_id))
      i++;
    if (i < games.size())
      games[i] = f;
    else
      games.push_back(f);
  }
  size_t keep = 0;
  for (size_t i = 0; i < games.size(); i++) {
    bool listed = false;
    for (const auto &f : fresh)
      listed = listed || watch_same(f, games[i].league, games[i].event_id);
    if (games[i].state != GameState::POST && (listed || !scope.covers(games[i])))
      games[keep++] = games[i];
  }
  games.erase(games.begin() + keep, games.end());
  for (size_t i = 1; i < games.size(); i++)  // stable, and never more than a few dozen
    for (size_t k = i; k > 0 && watch_rank(games[k - 1]) > watch_rank(games[k]); k--)
      std::swap(games[k - 1], games[k]);
  if (games.size() > kWatchMax)
    games.erase(games.begin() + kWatchMax, games.end());
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
inline std::vector<WatchSource> watch_sources(bool live, const std::vector<League> &leagues,
                                              const std::vector<const Team *> &favs) {
  std::vector<WatchSource> out;
  auto add = [&](League l, uint32_t group) {
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
      if (!is_leaderboard(l))
        add(l, 0);
  } else {
    for (const Team *t : favs)
      if (t != nullptr && !is_leaderboard(t->league))
        add(t->league, watch_by_event(t->league) ? t->group : 0);
  }
  return out;
}

// The next read this cycle: one game of a by_event source (game, an index
// into games), or a source's day scoreboard (game -1) once its wait is over;
// source -1 when the cycle's reads are done.
struct WatchRead {
  int source{-1};
  int game{-1};
};

inline WatchRead watch_next(const std::vector<WatchSource> &src, const std::vector<WatchGame> &games, uint32_t cycle,
                            uint32_t now) {
  for (size_t s = 0; s < src.size(); s++) {
    const WatchSource &w = src[s];
    for (size_t i = 0; w.by_event && i < games.size(); i++)
      if (games[i].league == w.league && games[i].group == w.group && games[i].read_cycle != cycle)
        return WatchRead{(int) s, (int) i};
    if (!w.fed && w.read_cycle != cycle && (w.wait_until == 0 || (int32_t) (now - w.wait_until) >= 0))
      return WatchRead{(int) s, -1};
  }
  return WatchRead{};
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
// expected time away, so the board is never kept waiting for it.
inline bool watch_read_fits(uint32_t now, uint32_t next_board_ms, uint32_t expect_ms) {
  return (int32_t) (next_board_ms - now) >= (int32_t) expect_ms;
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

// One read for the watcher, filled on the main loop and read on the fetch
// task: what to read, the watched games' last reads (a copy), and what it
// found.
struct WatchJob {
  uint8_t league{0};
  uint32_t group{0};
  std::string event_id;  // one game (its away side away_id); empty: the source's day scoreboard
  uint32_t away_id{0};
  uint32_t cycle{0};
  WatchCtx ctx;
  std::vector<WatchGame> games;
  bool ok{false};
  uint32_t took_ms{0};
  std::vector<WatchGame> fresh;
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
  g.read_cycle = job.cycle;
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
  job.fresh.push_back(g);
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

// What decides a switch to a watched game's big play.
struct WatchSwitch {
  bool allowed{true};     // nothing holds the board on its game (a pin to one game)
  bool splash{false};     // the board's own splash is up
  bool own_live{false};   // the board's game is the panel's team, live
  bool alert_fav{false};  // the big play's game has the panel's team or a favorite
  bool from_mine{false};  // "Switch from my team"
};

// The panel's own team's live game is never left for another game's big
// play, unless a favorite plays in that one and "Switch from my team" is on.
inline bool watch_may_switch(const WatchSwitch &s) {
  if (!s.allowed || s.splash)
    return false;
  return !s.own_live || (s.alert_fav && s.from_mine);
}

// "Stay on a big play": kept in tens of seconds, 0 for the default minute.
inline uint32_t watch_stay_ms(uint8_t tens) { return (tens != 0 ? tens : 6) * 10000u; }

// The board has been on the big play's game for the stay: back to what the
// mode was showing.
inline bool watch_visit_over(uint32_t now, uint32_t since_ms, uint32_t stay_ms) { return now - since_ms >= stay_ms; }

}  // namespace espn
