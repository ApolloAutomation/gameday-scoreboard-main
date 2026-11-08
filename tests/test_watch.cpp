// The big-play watcher (components/gameday/watch.h): which games it watches,
// what a read of one says against the last (real day scoreboards from
// fixtures/ with the score or the play moved on), when a read may run, and
// when the board switches and goes back.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "watch.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define WCHECK(cond)                                           \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                          \
  } while (0)

#define WCHECK_EQ(a, b)                                                                       \
  do {                                                                                        \
    checks++;                                                                                 \
    const std::string va = (a), vb = (b);                                                     \
    if (va != vb) {                                                                           \
      failures++;                                                                             \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                         \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// A day scoreboard with one event changed by edit.
std::string edited(const std::string &json, const char *event_id, const std::function<void(JsonObject)> &edit) {
  JsonDocument doc;
  deserializeJson(doc, json, DeserializationOption::NestingLimit(40));
  for (JsonObject ev : doc["events"].as<JsonArray>())
    if (std::string(ev["id"] | "") == event_id)
      edit(ev);
  std::string out;
  serializeJson(doc, out);
  return out;
}

void set_score(JsonObject ev, const char *home_away, int score) {
  for (JsonObject c : ev["competitions"][0]["competitors"].as<JsonArray>())
    if (std::string(c["homeAway"] | "") == home_away)
      c["score"] = std::to_string(score);
}

// Live games, every game watched.
WatchCtx live_ctx() {
  WatchCtx c;
  c.live = true;
  c.mask = kAlertsAll;
  return c;
}

// One read of a day scoreboard the way the fetch task does it, against the
// watched games' last reads (games). The job keeps what it found.
WatchJob read_day(const std::string &json, League league, const WatchCtx &ctx,
                  const std::vector<WatchGame> &games = {}, uint32_t group = 0, uint32_t cycle = 1) {
  WatchJob job;
  job.league = (uint8_t) league;
  job.group = group;
  job.cycle = cycle;
  job.ctx = ctx;
  job.games = games;
  std::string input = json;
  job.ok = watch_read_day(input, job);
  return job;
}

const WatchGame *find(const std::vector<WatchGame> &games, const char *event_id) {
  for (const auto &g : games)
    if (std::string(g.event_id) == event_id)
      return &g;
  return nullptr;
}

void test_day_reads() {
  // A college conference's Saturday: three games on, five to come.
  const std::string ncaa = slurp("fixtures/day_ncaa_20261017.json");
  WatchJob job = read_day(ncaa, League::NCAA, live_ctx());
  WCHECK(job.ok);
  WCHECK(job.fresh.size() == 3);
  WCHECK(!job.alert.valid);  // never on the first read of a game
  WCHECK(job.next_start > 0);
  const WatchGame *cit = find(job.fresh, "401856725");  // CIT @ TA&M, 14:29 - 2nd, 0-14
  WCHECK(cit != nullptr);
  if (cit != nullptr) {
    WCHECK(cit->away_id == 2643 && cit->home_id == 245);
    WCHECK(cit->state == GameState::IN && cit->period == 2 && cit->team_score == 0 && cit->opp_score == 14);
    WCHECK(!cit->fav && cit->group == 0 && cit->read_cycle == 1);
    WCHECK(cit->play != 0);  // ESPN's play id, hashed
  }
  // The game on the board is never watched.
  WatchCtx board = live_ctx();
  board.board_league = (uint8_t) League::NCAA;
  board.board_event = "401856725";
  WCHECK(read_day(ncaa, League::NCAA, board).fresh.size() == 2);
  // The same event id in another league is another game.
  board.board_league = (uint8_t) League::NFL;
  WCHECK(read_day(ncaa, League::NCAA, board).fresh.size() == 3);

  // The visitors score a touchdown: CIT's, either side counting.
  const std::string td = edited(ncaa, "401856725", [](JsonObject ev) { set_score(ev, "away", 7); });
  WatchJob next = read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 2);
  WCHECK(next.alert.valid);
  WCHECK_EQ(next.alert.event_id, "401856725");
  WCHECK(next.alert.away_id == 2643 && next.alert.home_id == 245);
  WCHECK_EQ(next.alert.away_abbr, "CIT");
  WCHECK_EQ(next.alert.splash.text, "CIT TOUCHDOWN");
  WCHECK(next.fresh.size() == 3);
  // A last read from three cycles back is too old to compare: the read only
  // starts over.
  WatchJob late = read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 4);
  WCHECK(!late.alert.valid && late.fresh.size() == 3 && find(late.fresh, "401856725")->read_cycle == 4);
  WCHECK(read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 3).alert.valid);
  // The home side's field goal.
  const std::string fg = edited(ncaa, "401856725", [](JsonObject ev) { set_score(ev, "home", 17); });
  WCHECK_EQ(read_day(fg, League::NCAA, live_ctx(), job.fresh).alert.splash.text, "TA&M FIELD GOAL");
  // With its switch off, nothing.
  WatchCtx no_fg = live_ctx();
  no_fg.mask &= ~alert_bit(Alert::FIELD_GOAL);
  WCHECK(!read_day(fg, League::NCAA, no_fg, job.fresh).alert.valid);
  // Nothing changed: nothing.
  WCHECK(!read_day(ncaa, League::NCAA, live_ctx(), job.fresh).alert.valid);
}

void test_favorites() {
  const std::string ncaa = slurp("fixtures/day_ncaa_20261017.json");
  // My team and Favorite teams: only the favorites' games. Texas A&M (245) is one.
  WatchCtx fav;
  fav.mask = kAlertsAll;
  fav.favs = {{(uint8_t) League::NCAA, 245}};
  WatchJob job = read_day(ncaa, League::NCAA, fav);
  WCHECK(job.fresh.size() == 1 && job.fresh[0].fav);
  // The panel's own team counts only in Live games.
  WatchCtx mine;
  mine.mask = kAlertsAll;
  mine.team = {(uint8_t) League::NCAA, 251};  // Texas
  WCHECK(read_day(ncaa, League::NCAA, mine).fresh.empty());
  mine.live = true;
  mine.fav_only = true;
  WatchJob only = read_day(ncaa, League::NCAA, mine);
  WCHECK(only.fresh.size() == 1 && only.fresh[0].fav);
  WCHECK_EQ(only.fresh[0].event_id, "401856724");  // FLA @ TEX
  // Favorite teams only off: every game, Texas's first.
  mine.fav_only = false;
  WCHECK(read_day(ncaa, League::NCAA, mine).fresh.size() == 3);

  // watch_wants on its own.
  WatchCtx c;
  c.favs = {{(uint8_t) League::NBA, 11}};
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 11, 5) == 2);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 5, 11) == 2);
  WCHECK(watch_wants(c, (uint8_t) League::NHL, "1", 11, 5) == 0);  // id 11 in another league
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 4, 5) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "", 11, 5) == 0);
  c.live = true;
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 4, 5) == 1);
  c.fav_only = true;
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 4, 5) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 11, 5) == 2);
}

void test_baseball() {
  // LAD @ MIL, Bot 3rd, MIL 3-2, runners on first and second, two out. The
  // day scoreboard's last play is already the next batter's.
  const std::string mlb = slurp("fixtures/day_mlb_20261012.json");
  WatchJob first = read_day(mlb, League::MLB, live_ctx());
  WCHECK(first.fresh.size() == 1);
  if (first.fresh.size() != 1)
    return;
  const WatchGame &g = first.fresh[0];
  WCHECK(g.half == Half::BOTTOM && g.outs == 2 && g.bases == (Baseball::FIRST | Baseball::SECOND));
  // Three runs and the bases empty with the same outs: a home run.
  const std::string homer = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 6);
    ev["competitions"][0]["situation"]["onFirst"] = false;
    ev["competitions"][0]["situation"]["onSecond"] = false;
  });
  WCHECK_EQ(read_day(homer, League::MLB, live_ctx(), first.fresh).alert.splash.text, "MIL HOME RUN");
  // With the bases loaded before, four runs are a grand slam.
  const std::string loaded = edited(mlb, "401908031", [](JsonObject ev) {
    ev["competitions"][0]["situation"]["onThird"] = true;
  });
  WatchJob before = read_day(loaded, League::MLB, live_ctx());
  const std::string slam = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 7);
    ev["competitions"][0]["situation"]["onFirst"] = false;
    ev["competitions"][0]["situation"]["onSecond"] = false;
  });
  WatchCtx wide = live_ctx();
  wide.wide = true;
  WCHECK_EQ(read_day(slam, League::MLB, wide, before.fresh).alert.splash.text, "MIL GRAND SLAM");
  // Two runs with a runner still on: runs.
  const std::string two = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 5);
    ev["competitions"][0]["situation"]["onFirst"] = false;
  });
  WCHECK_EQ(read_day(two, League::MLB, live_ctx(), first.fresh).alert.splash.text, "MIL 2 RUNS");
  // A third out between the reads: the bases empty, but no home run told.
  const std::string third_out = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 6);
    ev["competitions"][0]["situation"]["onFirst"] = false;
    ev["competitions"][0]["situation"]["onSecond"] = false;
    ev["competitions"][0]["situation"]["outs"] = 0;
  });
  WCHECK_EQ(read_day(third_out, League::MLB, live_ctx(), first.fresh).alert.splash.text, "MIL 3 RUNS");

  // watch_homer by hand: a solo shot, and the side batting in the top half.
  WatchGame prev;
  prev.state = GameState::IN;
  prev.period = 5;
  prev.half = Half::TOP;
  prev.outs = 1;
  prev.bases = 0;
  prev.team_score = 1;
  GameSnapshot cur;
  cur.state = GameState::IN;
  cur.period = 5;
  cur.mlb.half = Half::TOP;
  cur.mlb.outs = 1;
  cur.team_score = 2;
  WCHECK(watch_homer(prev, cur) == 1);
  cur.team_home = true;  // the home side doesn't bat in the top half
  WCHECK(watch_homer(prev, cur) == 0);
  cur.team_home = false;
  cur.mlb.bases = Baseball::FIRST;
  WCHECK(watch_homer(prev, cur) == 0);
  cur.mlb.bases = 0;
  prev.outs = -1;  // no count read before
  WCHECK(watch_homer(prev, cur) == 0);
}

void test_soccer() {
  // CRU @ BRA, 25', BRA 2-0 (a goal and an own goal).
  const std::string bra = slurp("fixtures/day_bra_20261012.json");
  WatchJob first = read_day(bra, League::MLS, live_ctx());
  WCHECK(first.fresh.size() == 1);
  if (first.fresh.size() != 1)
    return;
  WCHECK(first.fresh[0].goals == 2);
  auto add_detail = [](JsonObject ev, const char *team, bool goal, bool pen, bool red) {
    JsonObject d = ev["competitions"][0]["details"].as<JsonArray>().add<JsonObject>();
    d["type"]["text"] = goal ? "Penalty - Scored" : "Red Card";
    d["clock"]["displayValue"] = "30'";
    d["team"]["id"] = team;
    d["scoringPlay"] = goal;
    d["penaltyKick"] = pen;
    d["redCard"] = red;
    d["ownGoal"] = false;
    d["shootout"] = false;
  };
  const std::string pen = edited(bra, "401841260", [&](JsonObject ev) {
    set_score(ev, "home", 3);
    add_detail(ev, "6079", true, true, false);
  });
  WatchCtx wide = live_ctx();
  wide.wide = true;
  WCHECK_EQ(read_day(pen, League::MLS, wide, first.fresh).alert.splash.text, "BRA PENALTY GOAL");
  const std::string goal = edited(bra, "401841260", [&](JsonObject ev) {
    set_score(ev, "away", 1);
    add_detail(ev, "2022", true, false, false);
  });
  WCHECK_EQ(read_day(goal, League::MLS, live_ctx(), first.fresh).alert.splash.text, "CRU GOAL");
  const std::string red = edited(bra, "401841260", [&](JsonObject ev) { add_detail(ev, "2022", false, false, true); });
  WatchJob r = read_day(red, League::MLS, live_ctx(), first.fresh);
  WCHECK(r.alert.valid);
  WCHECK(r.alert.splash.text.find("RED CARD") != std::string::npos);
  WCHECK(r.alert.splash.color == 0xCC0000);
}

void test_turnover_needs_play_ids() {
  // BC @ CIN, ESPN's last play an interception, id 401856777515.
  const std::string sb = slurp("fixtures/scoreboard_ncaa_live.json");
  const std::string before = edited(sb, "401856777", [](JsonObject ev) {
    JsonObject lp = ev["competitions"][0]["situation"]["lastPlay"];
    lp["id"] = "401856777514";
    lp["type"]["text"] = "Pass Reception";
    lp["text"] = "Pass complete.";
  });
  WatchJob first = read_day(before, League::NCAA, live_ctx());
  WCHECK(first.fresh.size() == 18);
  WatchJob next = read_day(sb, League::NCAA, live_ctx(), first.fresh);
  WCHECK_EQ(next.alert.splash.text, "CIN\nINTER-CEPTION");  // the home side picked it off
  // The same play read twice is not new.
  WatchJob again = read_day(sb, League::NCAA, live_ctx(), next.fresh);
  WCHECK(!again.alert.valid);
  // Without ids a turnover can't be told new: none.
  auto no_id = [](JsonObject ev) { ev["competitions"][0]["situation"]["lastPlay"].as<JsonObject>().remove("id"); };
  WatchJob plain = read_day(edited(before, "401856777", no_id), League::NCAA, live_ctx());
  WCHECK(!read_day(edited(sb, "401856777", no_id), League::NCAA, live_ctx(), plain.fresh).alert.valid);
}

GameSnapshot basketball(int team, int opp, int period, const char *clock, GameState state = GameState::IN) {
  GameSnapshot g;
  g.valid = true;
  g.event_id = "9";
  g.sport = Sport::BASKETBALL;
  g.state = state;
  g.completed = state == GameState::POST;
  g.team_score = team;
  g.opp_score = opp;
  g.period = period;
  g.display_clock = clock;
  g.team_abbr = "NY";
  g.opp_abbr = "ATL";
  return g;
}

void test_detect() {
  WatchGame prev;
  snprintf(prev.event_id, sizeof(prev.event_id), "9");
  prev.league = (uint8_t) League::WNBA;
  watch_store(prev, basketball(98, 100, 4, "1:30"));
  WCHECK(watch_detect(prev, basketball(102, 100, 4, "1:10"), kAlertsAll).type == Alert::LEAD_CHANGE);
  WCHECK(watch_detect(prev, basketball(102, 100, 5, "5:00"), kAlertsAll).type == Alert::OVERTIME);
  // A final doesn't switch: the game is over.
  watch_store(prev, basketball(98, 100, 4, "0:40"));
  WCHECK(watch_detect(prev, basketball(98, 101, 4, "0.0", GameState::POST), kAlertsAll).type == Alert::NONE);
  // Not read yet: nothing.
  WatchGame unread;
  WCHECK(watch_detect(unread, basketball(102, 100, 4, "1:10"), kAlertsAll).type == Alert::NONE);

  // The read before rebuilt: what detect_big_play needs from it.
  GameSnapshot fb;
  fb.valid = true;
  fb.event_id = "5";
  fb.state = GameState::IN;
  fb.period = 3;
  fb.possession = 2;
  fb.down_distance = "2nd & 7 at DAL 30";
  fb.play_id = "123";
  fb.last_play = "J. Doe pass short right for a TOUCHDOWN";
  fb.display_clock = "4:21";
  fb.team_score = 21;
  fb.opp_score = 17;
  WatchGame w;
  snprintf(w.event_id, sizeof(w.event_id), "5");
  watch_store(w, fb);
  const GameSnapshot p = watch_prev(w, Sport::FOOTBALL);
  WCHECK(p.valid && p.event_id == "5" && p.state == GameState::IN && p.period == 3 && p.possession == 2);
  WCHECK(!p.down_distance.empty() && p.display_clock == "4:21" && p.team_score == 21 && p.opp_score == 17);
  WCHECK(p.play_id == std::to_string(watch_hash("123")) && p.last_play.empty());
  WCHECK(p.play_type == "touchdown");  // the try after it can't be a safety
  WCHECK(watch_hash("") == 0 && watch_hash("123") != 0 && watch_hash("123") != watch_hash("124"));
  // Two points right after a touchdown are a conversion, not a safety.
  GameSnapshot two = fb;
  two.state = GameState::IN;
  two.team_score = 23;
  two.last_play = "";
  two.play_id = "";
  WCHECK(watch_detect(w, two, kAlertsAll).type == Alert::TWO_POINT);
}

WatchGame game(const char *id, uint8_t league, bool fav, int team, int opp, int period,
               GameState state = GameState::IN) {
  WatchGame g;
  snprintf(g.event_id, sizeof(g.event_id), "%s", id);
  g.league = league;
  g.fav = fav;
  g.state = state;
  g.team_score = (int16_t) team;
  g.opp_score = (int16_t) opp;
  g.period = (uint8_t) period;
  return g;
}

void test_merge() {
  const uint8_t nfl = (uint8_t) League::NFL, nhl = (uint8_t) League::NHL;
  std::vector<WatchGame> games;
  std::vector<WatchGame> fresh = {game("1", nfl, false, 35, 3, 4),  game("2", nfl, false, 21, 20, 4),
                                  game("3", nfl, true, 0, 28, 2),   game("4", nfl, false, 14, 14, 1),
                                  game("5", nfl, false, 10, 3, 4),  game("6", nfl, false, 7, 0, 2),
                                  game("7", nfl, false, 3, 0, 3),   game("8", nfl, false, 24, 0, 4),
                                  game("9", nfl, false, 17, 13, 4), game("10", nfl, false, 0, 0, 1, GameState::POST)};
  WatchScope source;
  source.kind = WatchScope::SOURCE;
  source.league = nfl;
  watch_merge(games, fresh, source);
  WCHECK(games.size() == kWatchMax);
  // The favorite first, however far behind, then a score or less in the 4th,
  // then a tie in the 1st, then a score early, then the blowouts.
  const char *order[] = {"3", "2", "5", "9", "4", "6", "7", "8"};
  for (size_t i = 0; i < kWatchMax && i < games.size(); i++)
    WCHECK_EQ(games[i].event_id, order[i]);
  WCHECK(find(games, "10") == nullptr);  // over
  WCHECK(find(games, "1") == nullptr);   // the biggest blowout goes first
  // A game the next scoreboard read no longer lists drops out, other leagues' stay.
  games.push_back(game("50", nhl, false, 1, 1, 3));
  watch_merge(games, std::vector<WatchGame>{game("2", nfl, false, 24, 20, 4)}, source);
  WCHECK(games.size() == 2);
  WCHECK(find(games, "50") != nullptr && find(games, "2") != nullptr && find(games, "2")->team_score == 24);
  // A game's own read speaks for that game only: a read that no longer
  // takes it (now on the board) drops it, the other stays.
  WatchScope one;
  one.kind = WatchScope::GAME;
  one.league = nhl;
  one.event_id = "50";
  std::vector<WatchGame> both = games;
  watch_merge(both, {}, one);
  WCHECK(both.size() == 1 && find(both, "2") != nullptr);
  // It ends and drops out.
  watch_merge(games, std::vector<WatchGame>{game("50", nhl, false, 2, 1, 3, GameState::POST)}, one);
  WCHECK(games.size() == 1 && find(games, "50") == nullptr);
  // The board's scan only adds: none of the old ones drop out.
  watch_merge(games, std::vector<WatchGame>{game("60", (uint8_t) League::NCAA, false, 0, 0, 0, GameState::NOT_FOUND)},
              WatchScope{});
  WCHECK(games.size() == 2 && find(games, "2") != nullptr && find(games, "60") != nullptr);
  games.pop_back();
  // A game not read yet sits between the close games and the blowouts.
  WCHECK(watch_rank(game("u", nfl, false, 0, 0, 0, GameState::NOT_FOUND)) > watch_rank(game("c", nfl, false, 20, 17, 2)));
  WCHECK(watch_rank(game("u", nfl, false, 0, 0, 0, GameState::NOT_FOUND)) < watch_rank(game("b", nfl, false, 42, 0, 4)));
  // Basketball's scores are threes, baseball's runs.
  WCHECK(watch_rank(game("b", (uint8_t) League::NBA, false, 100, 97, 4)) ==
         watch_rank(game("m", (uint8_t) League::MLB, false, 3, 2, 8)));
}

const Team *team(League league, uint32_t id) {
  for (size_t i = 0; i < kTeamCount; i++)
    if (kTeams[i].league == league && kTeams[i].espn_id == id)
      return &kTeams[i];
  return nullptr;
}

void test_sources_and_reads() {
  // Live games: a scoreboard per pro league, college fed by the board's scan.
  std::vector<WatchSource> live = watch_sources(true, {League::NFL, League::NCAA, League::NHL, League::PGA}, {});
  WCHECK(live.size() == 3);
  WCHECK(live[0].league == (uint8_t) League::NFL && !live[0].by_event && !live[0].fed);
  WCHECK(live[1].league == (uint8_t) League::NCAA && live[1].by_event && live[1].fed);
  // Favorites: by event, a college one by its conference, each once.
  const Team *dal = team(League::NFL, 6), *phi = team(League::NFL, 21), *tex = team(League::NCAA, 251),
             *tamu = team(League::NCAA, 245);
  WCHECK(dal && phi && tex && tamu);
  std::vector<WatchSource> fav = watch_sources(false, {}, {dal, phi, tex, tamu});
  WCHECK(fav.size() == 2);
  WCHECK(fav[0].league == (uint8_t) League::NFL && fav[0].group == 0 && fav[0].by_event && !fav[0].fed);
  WCHECK(fav[1].league == (uint8_t) League::NCAA && fav[1].group == tex->group && fav[1].group != 0);

  // A cycle: the first source's scoreboard, then its games one by one.
  const uint32_t now = 100000;
  std::vector<WatchGame> games;
  WatchRead r = watch_next(fav, games, 1, now);
  WCHECK(r.source == 0 && r.game == -1);
  fav[0].read_cycle = 1;
  fav[0].wait_until = watch_wait_until(fav[0], true, now, 1800000000, 0);
  WCHECK(fav[0].wait_until == now + kWatchWaitMaxMs);  // by event: the scoreboard only finds games
  r = watch_next(fav, games, 1, now);
  WCHECK(r.source == 1 && r.game == -1);
  fav[1].read_cycle = 1;
  fav[1].wait_until = watch_wait_until(fav[1], false, now, 1800000000, 1800000000 + 600);
  WCHECK(fav[1].wait_until == now + 600000);  // the next start, 10 minutes on
  WCHECK(watch_next(fav, games, 1, now).source == -1);
  // Next cycle: a game found on the first source is read on its own.
  WatchGame g = game("401", (uint8_t) League::NFL, true, 7, 3, 2);
  g.read_cycle = 1;
  games.push_back(g);
  r = watch_next(fav, games, 2, now + 45000);
  WCHECK(r.source == 0 && r.game == 0);
  games[0].read_cycle = 2;
  WCHECK(watch_next(fav, games, 2, now + 45000).source == -1);  // both scoreboards wait
  WCHECK(watch_next(fav, games, 3, now + 600000).source == 0);  // the game, again
  games[0].read_cycle = 3;
  r = watch_next(fav, games, 3, now + 600000);
  WCHECK(r.source == 1 && r.game == -1);  // the conference's next start is here

  // Live games: the scoreboard every cycle while a game is on, a fed source never.
  WCHECK(watch_wait_until(live[0], true, now, 1800000000, 0) == 0);
  WCHECK(watch_wait_until(live[0], false, now, 1800000000, 1800000000 + 30) == now + kWatchWaitMinMs);
  live[0].read_cycle = 1;
  live[2].read_cycle = 1;
  WCHECK(watch_next(live, {}, 1, now).source == -1);
  std::vector<WatchGame> fed = {game("77", (uint8_t) League::NCAA, false, 0, 0, 0, GameState::NOT_FOUND)};
  r = watch_next(live, fed, 1, now);
  WCHECK(r.source == 1 && r.game == 0);

  // A read fits only before the board's next poll.
  WCHECK(watch_expect_ms(0, false) == kWatchBoardMs && watch_expect_ms(0, true) == kWatchEventMs);
  WCHECK(watch_expect_ms(1800, false) == 1800 + kWatchMarginMs);
  WCHECK(watch_read_fits(1000, 1000 + 5000, 5000));
  WCHECK(!watch_read_fits(1000, 1000 + 4999, 5000));
  WCHECK(!watch_read_fits(1000, 900, 2500));                    // the board's poll is due
  WCHECK(watch_read_fits(0xFFFFF000u, 0xFFFFF000u + 9000, 3000));  // across millis() wrapping
}

void test_switch() {
  WatchSwitch s;
  WCHECK(watch_may_switch(s));
  s.splash = true;  // the board's own splash is up
  WCHECK(!watch_may_switch(s));
  s.splash = false;
  s.allowed = false;  // pinned to one game
  WCHECK(!watch_may_switch(s));
  s.allowed = true;
  s.own_live = true;  // the panel's team is playing on the board
  WCHECK(!watch_may_switch(s));
  s.alert_fav = true;
  WCHECK(!watch_may_switch(s));
  s.from_mine = true;  // Switch from my team, for a favorite's big play only
  WCHECK(watch_may_switch(s));
  s.alert_fav = false;
  WCHECK(!watch_may_switch(s));

  WCHECK(watch_stay_ms(0) == 60000 && watch_stay_ms(3) == 30000 && watch_stay_ms(30) == 300000);
  WCHECK(!watch_visit_over(1059999, 1000000, 60000));
  WCHECK(watch_visit_over(1060000, 1000000, 60000));
  WCHECK(watch_visit_over(30000, 0xFFFFFFF0u, 30000));  // across millis() wrapping
  WCHECK(clamp_watch_stay_seconds(0) == 30 && clamp_watch_stay_seconds(95) == 90 && clamp_watch_stay_seconds(301) == 300);
}

// A game that just ended is still read once, for a walk-off or a buzzer beater.
void test_final_read() {
  const std::string mlb = slurp("fixtures/day_mlb_20261012.json");
  WatchJob first = read_day(mlb, League::MLB, live_ctx());
  const std::string over = edited(mlb, "401908031", [](JsonObject ev) {
    ev["status"]["type"]["state"] = "post";
    ev["status"]["type"]["completed"] = true;
  });
  WatchJob last = read_day(over, League::MLB, live_ctx(), first.fresh);
  WCHECK(last.fresh.size() == 1 && last.fresh[0].state == GameState::POST);
  // Not watched before: a final isn't taken.
  WCHECK(read_day(over, League::MLB, live_ctx()).fresh.empty());
  std::vector<WatchGame> games = first.fresh;
  WatchScope all;
  all.kind = WatchScope::SOURCE;
  all.league = (uint8_t) League::MLB;
  watch_merge(games, last.fresh, all);
  WCHECK(games.empty());
}

}  // namespace

int run_watch_tests(int &total_checks) {
  test_day_reads();
  test_favorites();
  test_baseball();
  test_soccer();
  test_turnover_needs_play_ids();
  test_detect();
  test_merge();
  test_sources_and_reads();
  test_switch();
  test_final_read();
  total_checks += checks;
  return failures;
}
