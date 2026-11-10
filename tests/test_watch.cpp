// Big-play watcher (watch.h) against real ESPN day scoreboards.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "follow.h"
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

WatchCtx live_ctx() {
  WatchCtx c;
  c.live = true;
  c.mask = kAlertsAll;
  return c;
}

// Mirrors the fetch task's read of a day scoreboard.
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
  WatchCtx board = live_ctx();
  board.board_league = (uint8_t) League::NCAA;
  board.board_event = "401856725";
  WCHECK(read_day(ncaa, League::NCAA, board).fresh.size() == 2);
  // The same event id in another league is another game.
  board.board_league = (uint8_t) League::NFL;
  WCHECK(read_day(ncaa, League::NCAA, board).fresh.size() == 3);

  // Either side's touchdown counts, here the visitors'.
  const std::string td = edited(ncaa, "401856725", [](JsonObject ev) { set_score(ev, "away", 7); });
  WatchJob next = read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 2);
  WCHECK(next.alert.valid);
  WCHECK_EQ(next.alert.event_id, "401856725");
  WCHECK(next.alert.away_id == 2643 && next.alert.home_id == 245);
  WCHECK_EQ(next.alert.away_abbr, "CIT");
  WCHECK_EQ(next.alert.splash.text, "CIT TOUCHDOWN");
  WCHECK(next.fresh.size() == 3);
  // Three cycles back is too stale to compare.
  WatchJob late = read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 4);
  WCHECK(!late.alert.valid && late.fresh.size() == 3 && find(late.fresh, "401856725")->read_cycle == 4);
  WCHECK(read_day(td, League::NCAA, live_ctx(), job.fresh, 0, 3).alert.valid);
  const std::string fg = edited(ncaa, "401856725", [](JsonObject ev) { set_score(ev, "home", 17); });
  WCHECK_EQ(read_day(fg, League::NCAA, live_ctx(), job.fresh).alert.splash.text, "TA&M FIELD GOAL");
  WatchCtx no_fg = live_ctx();
  no_fg.mask &= ~alert_bit(Alert::FIELD_GOAL);
  WCHECK(!read_day(fg, League::NCAA, no_fg, job.fresh).alert.valid);
  WCHECK(!read_day(ncaa, League::NCAA, live_ctx(), job.fresh).alert.valid);
}

void test_favorites() {
  const std::string ncaa = slurp("fixtures/day_ncaa_20261017.json");
  // Only favorites' games; Texas A&M (245) is one.
  WatchCtx fav;
  fav.mask = kAlertsAll;
  fav.favs = {{(uint8_t) League::NCAA, 245}};
  WatchJob job = read_day(ncaa, League::NCAA, fav);
  WCHECK(job.fresh.size() == 1 && job.fresh[0].fav);
  // The panel's own team counts only in Live games.
  WatchCtx mine;
  mine.mask = kAlertsAll;
  mine.team = {(uint8_t) League::NCAA, 251};  // team 251 is Texas
  WCHECK(read_day(ncaa, League::NCAA, mine).fresh.empty());
  mine.live = true;
  mine.fav_only = true;
  WatchJob only = read_day(ncaa, League::NCAA, mine);
  WCHECK(only.fresh.size() == 1 && only.fresh[0].fav);
  WCHECK_EQ(only.fresh[0].event_id, "401856724");  // the FLA @ TEX game
  // Favorite teams only off: every game, Texas's first.
  mine.fav_only = false;
  WCHECK(read_day(ncaa, League::NCAA, mine).fresh.size() == 3);

  WatchCtx c;
  c.mask = kAlertsAll;
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
  // A final alone never switches, so basketball isn't watched.
  c.mask = alert_bit(Alert::FINAL) | alert_bit(Alert::HOCKEY_GOAL);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 11, 5) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NHL, "1", 4, 5) == 0);  // not a favorite, Favorite teams only on
  c.fav_only = false;
  WCHECK(watch_wants(c, (uint8_t) League::NHL, "1", 4, 5) == 1);
}

void test_baseball() {
  // LAD @ MIL, Bot 3rd, 3-2, two on, two out.
  const std::string mlb = slurp("fixtures/day_mlb_20261012.json");
  WatchJob first = read_day(mlb, League::MLB, live_ctx());
  WCHECK(first.fresh.size() == 1);
  if (first.fresh.size() != 1)
    return;
  const WatchGame &g = first.fresh[0];
  WCHECK(g.half == Half::BOTTOM && g.outs == 2 && g.bases == (Baseball::FIRST | Baseball::SECOND));
  // Three runs, bases empty, same outs: a homer.
  const std::string homer = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 6);
    ev["competitions"][0]["situation"]["onFirst"] = false;
    ev["competitions"][0]["situation"]["onSecond"] = false;
  });
  WCHECK_EQ(read_day(homer, League::MLB, live_ctx(), first.fresh).alert.splash.text, "MIL HOME RUN");
  // Four runs with the bases loaded: a grand slam.
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
  // A third out empties bases too: no homer.
  const std::string third_out = edited(mlb, "401908031", [](JsonObject ev) {
    set_score(ev, "home", 6);
    ev["competitions"][0]["situation"]["onFirst"] = false;
    ev["competitions"][0]["situation"]["onSecond"] = false;
    ev["competitions"][0]["situation"]["outs"] = 0;
  });
  WCHECK_EQ(read_day(third_out, League::MLB, live_ctx(), first.fresh).alert.splash.text, "MIL 3 RUNS");

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
  // CRU @ BRA, 25', 2-0 including an own goal.
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
  // Favorite CIN (2132) stays among 8 kept of 18.
  const std::string sb = slurp("fixtures/scoreboard_ncaa_live.json");
  const std::string before = edited(sb, "401856777", [](JsonObject ev) {
    JsonObject lp = ev["competitions"][0]["situation"]["lastPlay"];
    lp["id"] = "401856777514";
    lp["type"]["text"] = "Pass Reception";
    lp["text"] = "Pass complete.";
  });
  WatchCtx cin = live_ctx();
  cin.favs = {{(uint8_t) League::NCAA, 2132}};
  WatchJob first = read_day(before, League::NCAA, cin);
  WCHECK(first.fresh.size() == kWatchMax && first.fresh.capacity() <= kWatchMax);
  WCHECK(find(first.fresh, "401856777") != nullptr && find(first.fresh, "401856777")->fav);
  WCHECK(find(first.fresh, "401858433") != nullptr);  // 24-24 in the 4th
  WCHECK(find(first.fresh, "401866627") == nullptr);  // 51-3 in the 4th
  WatchJob next = read_day(sb, League::NCAA, cin, first.fresh);
  WCHECK_EQ(next.alert.splash.text, "CIN\nINTER-CEPTION");  // the home side picked it off
  WCHECK(next.alert.fav);
  WatchJob again = read_day(sb, League::NCAA, cin, next.fresh);
  WCHECK(!again.alert.valid);
  // Without ids a turnover can't be told new.
  auto no_id = [](JsonObject ev) { ev["competitions"][0]["situation"]["lastPlay"].as<JsonObject>().remove("id"); };
  WatchJob plain = read_day(edited(before, "401856777", no_id), League::NCAA, cin);
  WCHECK(!read_day(edited(sb, "401856777", no_id), League::NCAA, cin, plain.fresh).alert.valid);
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
  watch_store(prev, basketball(98, 100, 4, "0:40"));
  WCHECK(watch_detect(prev, basketball(98, 101, 4, "0.0", GameState::POST), kAlertsAll).type == Alert::NONE);
  WatchGame unread;
  WCHECK(watch_detect(unread, basketball(102, 100, 4, "1:10"), kAlertsAll).type == Alert::NONE);

  // watch_prev keeps only what detect_big_play needs.
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
  // Two points after a touchdown: conversion, not safety.
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
  // Favorite, close late, tied early, early score, then blowouts.
  const char *order[] = {"3", "2", "5", "9", "4", "6", "7", "8"};
  for (size_t i = 0; i < kWatchMax && i < games.size(); i++)
    WCHECK_EQ(games[i].event_id, order[i]);
  WCHECK(find(games, "10") == nullptr);  // game 10 already ended
  WCHECK(find(games, "1") == nullptr);   // the biggest blowout goes first
  // Unlisted games drop out; other leagues' games stay.
  games.push_back(game("50", nhl, false, 1, 1, 3));
  watch_merge(games, std::vector<WatchGame>{game("2", nfl, false, 24, 20, 4)}, source);
  WCHECK(games.size() == 2);
  WCHECK(find(games, "50") != nullptr && find(games, "2") != nullptr && find(games, "2")->team_score == 24);
  // A single-game read drops only its own game.
  WatchScope one;
  one.kind = WatchScope::GAME;
  one.league = nhl;
  one.event_id = "50";
  std::vector<WatchGame> both = games;
  watch_merge(both, {}, one);
  WCHECK(both.size() == 1 && find(both, "2") != nullptr);
  watch_merge(games, std::vector<WatchGame>{game("50", nhl, false, 2, 1, 3, GameState::POST)}, one);
  WCHECK(games.size() == 1 && find(games, "50") == nullptr);
  // The board's scan only adds, never drops.
  watch_merge(games, std::vector<WatchGame>{game("60", (uint8_t) League::NCAA, false, 0, 0, 0, GameState::NOT_FOUND)},
              WatchScope{});
  WCHECK(games.size() == 2 && find(games, "2") != nullptr && find(games, "60") != nullptr);
  games.pop_back();
  // Unread games rank between close games and blowouts.
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
  // Live: one scoreboard per pro league; college fed by scan.
  std::vector<WatchSource> live =
      watch_sources(true, {League::NFL, League::NCAA, League::NHL, League::PGA}, {}, kAlertsAll);
  WCHECK(live.size() == 3);
  WCHECK(watch_sources(true, {League::NFL, League::NCAA, League::NHL}, {}, kAlertsAll & ~alert_bit(Alert::HOCKEY_GOAL))
             .size() == 2);
  WCHECK(watch_sources(true, {League::NBA}, {}, alert_bit(Alert::FINAL)).empty());
  WCHECK(watch_sources(true, {League::NBA}, {}, 0).empty());
  WCHECK(live[0].league == (uint8_t) League::NFL && !live[0].by_event && !live[0].fed);
  WCHECK(live[1].league == (uint8_t) League::NCAA && live[1].by_event && live[1].fed);
  // Favorites by event; college by conference, each once.
  const Team *dal = team(League::NFL, 6), *phi = team(League::NFL, 21), *tex = team(League::NCAA, 251),
             *tamu = team(League::NCAA, 245);
  WCHECK(dal && phi && tex && tamu);
  std::vector<WatchSource> fav = watch_sources(false, {}, {dal, phi, tex, tamu}, kAlertsAll);
  WCHECK(fav.size() == 2);
  WCHECK(fav[0].league == (uint8_t) League::NFL && fav[0].group == 0 && fav[0].by_event && !fav[0].fed);
  WCHECK(fav[1].league == (uint8_t) League::NCAA && fav[1].group == tex->group && fav[1].group != 0);
  // Minor league comes from MLB, never an ESPN source.
  const Team *dur = team(League::MILB, 234);
  WCHECK(dur != nullptr);
  const std::vector<WatchSource> with_milb = watch_sources(true, {League::MLB, League::MILB}, {}, kAlertsAll);
  WCHECK(with_milb.size() == 1 && with_milb[0].league == (uint8_t) League::MLB);
  WCHECK(watch_sources(true, {League::MILB}, {}, kAlertsAll).empty());
  WCHECK(watch_sources(false, {}, {dur}, kAlertsAll).empty());
  WCHECK(watch_sources(false, {}, {dur, dal}, kAlertsAll).size() == 1);
  // AHL and ECHL come from HockeyTech: same.
  const Team *syr = team(League::AHL, 324), *adk = team(League::ECHL, 74);
  WCHECK(syr != nullptr && adk != nullptr);
  const std::vector<WatchSource> with_ht =
      watch_sources(true, {League::NHL, League::AHL, League::ECHL}, {}, kAlertsAll);
  WCHECK(with_ht.size() == 1 && with_ht[0].league == (uint8_t) League::NHL);
  WCHECK(watch_sources(false, {}, {syr, adk}, kAlertsAll).empty());
  {
    WatchCtx c;
    c.live = true;
    c.mask = kAlertsAll;
    c.team = {(uint8_t) League::MILB, 234};
    c.favs.emplace_back((uint8_t) League::MILB, 235);
    WCHECK(watch_wants_teams(c, (uint8_t) League::MILB, 234, 235) == 0);
    WCHECK(watch_wants_teams(c, (uint8_t) League::MILB, 102, 2310) == 0);
    WCHECK(watch_wants(c, (uint8_t) League::MILB, "776655", 102, 2310) == 0);
    WCHECK(watch_wants_teams(c, (uint8_t) League::MLB, 10, 12) == 1);
    c.favs.emplace_back((uint8_t) League::AHL, 324);
    WCHECK(watch_wants_teams(c, (uint8_t) League::AHL, 324, 440) == 0);
    WCHECK(watch_wants_teams(c, (uint8_t) League::ECHL, 74, 66) == 0);
  }

  // A cycle: scoreboard first, then its games singly.
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
  // Next cycle reads the found game on its own.
  WatchGame g = game("401", (uint8_t) League::NFL, true, 7, 3, 2);
  g.read_cycle = g.tried_cycle = 1;
  games.push_back(g);
  r = watch_next(fav, games, 2, now + 45000);
  WCHECK(r.source == 0 && r.game == 0);
  games[0].tried_cycle = 2;
  WCHECK(watch_next(fav, games, 2, now + 45000).source == -1);  // both scoreboards wait
  WCHECK(!watch_next(fav, games, 2, now + 45000).waiting);
  // Conference start due: its scoreboard first, then the game.
  r = watch_next(fav, games, 3, now + 600000);
  WCHECK(r.source == 1 && r.game == -1);
  fav[1].read_cycle = 3;
  r = watch_next(fav, games, 3, now + 600000);
  WCHECK(r.source == 0 && r.game == 0);

  // Live: scoreboard every cycle while on; fed sources never.
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
  const WatchSwitch away;
  WCHECK(!watch_visit_over(1059999, 1000000, 60000, away));
  WCHECK(watch_visit_over(1060000, 1000000, 60000, away));
  WCHECK(watch_visit_over(30000, 0xFFFFFFF0u, 30000, away));  // across millis() wrapping
  WCHECK(clamp_watch_stay_seconds(0) == 30 && clamp_watch_stay_seconds(95) == 90 && clamp_watch_stay_seconds(301) == 300);
}

// Ended games are read once more for walk-offs.
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

// Follow pins the mode's game, never the visit's.
void test_follow() {
  GameSnapshot mode_game, visit_game;
  for (GameSnapshot *g : {&mode_game, &visit_game}) {
    g->valid = true;
    g->sport = Sport::FOOTBALL;
    g->state = GameState::IN;
    g->kickoff_epoch = 1800000000;
  }
  mode_game.event_id = "401";
  visit_game.event_id = "402";
  const int64_t now = 1800003600;
  WCHECK(&watch_mode_game(mode_game, nullptr) == &mode_game);  // no visit: the board's game
  WCHECK(&watch_mode_game(visit_game, &mode_game) == &mode_game);
  FollowPin pin;
  WCHECK(follow_start(pin, true, true, watch_mode_game(visit_game, &mode_game), now));
  WCHECK(pin.on && pin.event_id == "401");
  // Checking against the visit's game would release the pin.
  FollowPin against_visit = pin;
  WCHECK(follow_holds(pin, watch_mode_game(visit_game, &mode_game), now + 60, 0, 0));
  WCHECK(!follow_holds(against_visit, visit_game, now + 60, 0, 0) && !against_visit.on);

  WatchSwitch s;
  s.allowed = !pin.on;
  WCHECK(!watch_may_switch(s));
  WCHECK(watch_visit_over(1000, 990, 60000, s));  // well before the stay is over
  s.alert_fav = s.from_mine = true;              // not even a favorite's with Switch from my team
  WCHECK(!watch_may_switch(s));
  WCHECK(watch_visit_over(1000, 990, 60000, s));
  pin = FollowPin{};  // let go: big plays elsewhere switch again
  s.allowed = !pin.on;
  WCHECK(watch_may_switch(s));
  WCHECK(!watch_visit_over(1000, 990, 60000, s));

  // During a visit, offer Follow only for followable games.
  FollowPin none;
  WCHECK(follow_can_start(none, true, true, watch_mode_game(visit_game, &mode_game)) && !none.on);
  GameSnapshot ended = mode_game;
  ended.state = GameState::POST;
  WCHECK(!follow_can_start(none, true, true, watch_mode_game(visit_game, &ended)));
  WCHECK(!follow_can_start(none, true, false, mode_game));  // Favorite teams with no card up
  WCHECK(!follow_can_start(none, false, true, mode_game));  // in My team mode
  FollowPin held;
  WCHECK(follow_start(held, true, true, mode_game, now));
  WCHECK(follow_can_start(held, true, true, ended) && held.on);  // a final it holds already
}

// Watcher reads update only games bets are tied to.
void test_bets() {
  const std::string mlb = slurp("fixtures/day_mlb_20261012.json");
  WatchJob plain = read_day(mlb, League::MLB, live_ctx());
  WCHECK(plain.fresh.size() == 1 && plain.bets.empty());
  if (plain.fresh.size() != 1)
    return;
  const WatchGame &w = plain.fresh[0];
  const uint32_t event = bet_event_id(w.event_id);
  WCHECK(event != 0);
  auto read = [&](const std::string &json) {
    WatchJob job;
    job.league = (uint8_t) League::MLB;
    job.cycle = 1;
    job.ctx = live_ctx();
    job.bet_events = {event + 1, event};
    std::string input = json;
    job.ok = watch_read_day(input, job);
    return job;
  };
  WatchJob on = read(mlb);
  WCHECK(on.bets.size() == 1);
  if (on.bets.size() != 1)
    return;
  const BetGame live = on.bets[0];
  WCHECK(live.event == event && live.league == (uint8_t) League::MLB && live.phase == BetPhase::LIVE);
  WCHECK(live.team_id == w.away_id && live.opp_id == w.home_id && live.kickoff > 0);
  WCHECK(live.team_score == w.team_score && live.opp_score == w.opp_score);

  const int64_t now = live.kickoff + 3600, midnight = now + 6 * 3600;
  Bet tied{};  // away moneyline already tied to this game
  tied.team_id = w.away_id;
  tied.league = (uint8_t) League::MLB;
  tied.type = (uint8_t) BetType::MONEYLINE;
  tied.placed = (uint32_t) (live.kickoff - 600);
  tied.event = event;
  tied.opp_id = w.home_id;
  tied.kickoff = (uint32_t) live.kickoff;
  Bet waiting = tied;  // the same bet with no game yet
  waiting.event = waiting.opp_id = waiting.kickoff = 0;
  Bet later = tied;  // tied to the team's later game
  later.event = event + 1;
  later.kickoff = (uint32_t) (live.kickoff + 86400);
  Bet b = tied;
  WCHECK(!bet_update(b, live, now, midnight));  // nothing new while it's on
  b = waiting;
  WCHECK(!bet_update(b, live, now, midnight) && b.event == 0);  // never ties a bet
  b = later;
  WCHECK(!bet_update(b, live, now, midnight) && b.event == event + 1);  // nor moves one
  b = waiting;
  WCHECK(bet_observe(b, live, now, midnight) && b.event == event);  // as the board's own read would

  const std::string over = edited(mlb, w.event_id, [](JsonObject ev) {
    ev["status"]["type"]["state"] = "post";
    ev["status"]["type"]["completed"] = true;
  });
  std::vector<WatchGame> games = plain.fresh;
  WatchJob last;
  last.league = (uint8_t) League::MLB;
  last.cycle = 2;
  last.ctx = live_ctx();
  last.games = games;
  last.bet_events = {event};
  std::string input = over;
  WCHECK(watch_read_day(input, last));
  WCHECK(last.bets.size() == 1 && last.bets[0].phase == BetPhase::FINAL);
  if (last.bets.size() != 1)
    return;
  b = tied;
  WCHECK(bet_update(b, last.bets[0], now, midnight));
  WCHECK(b.result != (uint8_t) BetResult::NONE && b.expires == (uint32_t) midnight);
  b = waiting;
  WCHECK(!bet_update(b, last.bets[0], now, midnight) && b.result == (uint8_t) BetResult::NONE);
  ManualBets src;
  src.blob.bets[0] = tied;
  src.blob.bets[1] = waiting;
  src.blob.bets[2] = later;
  bets_update(src, last.bets[0], now, midnight);
  WCHECK(src.dirty && src.blob.bets[0].result != (uint8_t) BetResult::NONE);
  WCHECK(src.blob.bets[1].event == 0 && src.blob.bets[2].event == event + 1);
  WCHECK(src.blob.bets[1].result == (uint8_t) BetResult::NONE && src.blob.bets[2].result == (uint8_t) BetResult::NONE);

  // The board's own game isn't taken, so no bets.
  WatchCtx board = live_ctx();
  board.board_league = (uint8_t) League::MLB;
  board.board_event = w.event_id;
  WatchJob off;
  off.league = (uint8_t) League::MLB;
  off.cycle = 1;
  off.ctx = board;
  off.bet_events = {event};
  input = mlb;
  WCHECK(watch_read_day(input, off) && off.bets.empty());

  // Leaderboards and id-less games are never bet games.
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::POST;
  g.sport = Sport::GOLF;
  g.event_id = "401";
  BetGame out;
  WCHECK(!watch_bet_game(g, (uint8_t) League::PGA, out));
  g.sport = Sport::BASEBALL;
  g.event_id = "abc";
  WCHECK(!watch_bet_game(g, (uint8_t) League::MLB, out));
  g.event_id = "401";
  WCHECK(!watch_bet_game(g, (uint8_t) League::NFL, out));  // another sport's league
  WCHECK(watch_bet_game(g, (uint8_t) League::MLB, out) && out.phase == BetPhase::OFF);  // ended, not completed
}

// Models the one fetch task in 50 ms steps.
struct FetchModel {
  std::vector<WatchSource> src;
  std::vector<WatchGame> games;
  WatchTimes times;
  bool fav = false;
  uint32_t poll_every = 10000, poll_took = 300;
  uint32_t side_every = 0, side_took = 4000;
  int side_reads = 0;
  // Read cost and answer; empty event id is a scoreboard.
  using Read = const std::string &;
  std::function<uint32_t(const WatchSource &, Read)> took = [](const WatchSource &, Read) { return 1500u; };
  std::function<bool(const WatchSource &, Read)> answers = [](const WatchSource &, Read) { return true; };
  int polls = 0, late_polls = 0;
  uint32_t worst_late = 0;
  std::vector<int> source_reads;
  std::map<std::string, int> game_reads;  // by event id

  void run(uint32_t ms) {
    source_reads.assign(src.size(), 0);
    game_reads.clear();
    uint32_t now = 100000, end = now + ms;
    uint32_t cycle = 0, cycle_ms = now - kWatchCycleMs, hold = 0, busy_until = 0, next_tick = now;
    uint32_t polled = 0, next_poll = now;  // Favorite teams' last poll, My team's next
    uint32_t side_check = now - 1000, side_last = 0;
    side_reads = 0;
    bool idle = false, busy = false, job_poll = false, job_ok = false, job_event = false, job_side = false;
    uint8_t job_league = 0;
    std::string job_event_id;
    for (; (int32_t) (now - end) < 0; now += 50) {
      if (busy) {
        if ((int32_t) (now - busy_until) < 0)
          continue;
        busy = false;
        if (job_side) {
          job_side = false;
        } else if (job_poll) {
          polled = now;
          next_poll = now + poll_every;
        } else {
          times.took(job_league, job_event, took_ms_, job_ok);
          if (!job_ok && job_event)
            watch_failed(games, job_league, job_event_id);
        }
        continue;  // the next loop looks again
      }
      const uint32_t due = fav ? watch_fav_due(now, true, polled, poll_every, now + 6 * 3600 * 1000u) : next_poll;
      const bool gap = (int32_t) (now - (fav ? next_tick : due)) < 0;
      if (side_every != 0 && gap && now - side_check >= 1000) {
        side_check = now;
        if (side_reads == 0 || now - side_last >= side_every) {
          side_last = now;
          side_reads++;
          busy = job_side = true;
          busy_until = now + side_took;
          continue;
        }
      }
      if (fav && (int32_t) (now - next_tick) < 0)
        continue;
      if ((int32_t) (now - due) >= 0) {
        const uint32_t late = now - due;
        worst_late = std::max(worst_late, late);
        late_polls += late > 1000;
        polls++;
        busy = true;
        job_poll = true;
        busy_until = now + poll_took;
        continue;
      }
      if (fav)
        next_tick = now + 1000;
      if (hold != 0 && (int32_t) (now - hold) < 0)
        continue;
      hold = 0;
      if (now - cycle_ms >= kWatchCycleMs) {
        cycle += (now - cycle_ms) / kWatchCycleMs;
        cycle_ms = now;
        idle = false;
        times.cycle();
      }
      if (idle)
        continue;
      const WatchRead r = watch_next(src, games, cycle, now, [&](const WatchSource &s, bool event) {
        return times.fits(now, due, s.league, event);
      });
      if (r.source < 0) {
        if (r.waiting)
          hold = now + 250;
        else
          idle = true;
        continue;
      }
      WatchSource &s = src[r.source];
      job_event = r.game >= 0;
      times.started(s.league, job_event);
      job_league = s.league;
      job_poll = false;
      const std::string id = r.game >= 0 ? games[r.game].event_id : "";
      job_ok = answers(s, id);
      took_ms_ = took(s, id);
      if (job_event) {
        games[r.game].tried_cycle = cycle;
        job_event_id = games[r.game].event_id;
        game_reads[games[r.game].event_id]++;
        if (job_ok)
          games[r.game].read_cycle = cycle;
      } else {
        s.read_cycle = cycle;
        source_reads[r.source]++;
      }
      busy = true;
      busy_until = now + took_ms_;
    }
  }
  uint32_t took_ms_ = 0;
};

WatchSource day_source(League l) {
  WatchSource s;
  s.league = (uint8_t) l;
  return s;
}

// Favorites fit reads against the locked game's next poll.
void test_favorites_tick() {
  WCHECK(!watch_read_fits(1000, 1000 + 1000, watch_expect_ms(0, true)));  // the tick itself never leaves room
  WCHECK(watch_fav_due(1000, true, 0, 10000, 999999) == 1000);                // never polled: due now
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999) == 11000);
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 8000) == 8000);  // a team read comes first
  WCHECK(watch_fav_due(5000, false, 1000, 10000, 8000) == 8000);  // the playlist: only team reads
  // Minor league polls wait out MLB's backoff.
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999, 60000) == 65000);
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 30000, 60000) == 30000);  // a team read before the wait ends
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999, 1000) == 11000);  // a wait over before the poll is due
  // An ESPN refusal holds only ESPN-bound board reads.
  WCHECK(watch_refusal_holds_board(false, false, (uint8_t) League::NFL));    // My team, from ESPN
  WCHECK(!watch_refusal_holds_board(false, false, (uint8_t) League::MILB));  // My team, from MLB
  WCHECK(!watch_refusal_holds_board(false, false, (uint8_t) League::AHL));   // and from HockeyTech
  WCHECK(!watch_refusal_holds_board(false, false, (uint8_t) League::ECHL));
  WCHECK(watch_refusal_holds_board(false, true, (uint8_t) League::MILB));    // Live games: its scans read ESPN
  WCHECK(!watch_refusal_holds_board(true, false, (uint8_t) League::NFL));    // Favorite teams waits its own way
  WCHECK(watch_after_wait(1000, 5000, 0) == 1000);    // no wait: overdue stays overdue
  WCHECK(watch_after_wait(1000, 5000, 3000) == 8000);
  WCHECK(watch_after_wait(9000, 5000, 3000) == 9000);
  WCHECK(watch_after_wait(0xFFFFF000u, 0xFFFFF800u, 0x1000) == 0x800);  // across millis() wrapping
  WCHECK(!watch_read_fits(5000, watch_after_wait(1000, 5000, 0), watch_expect_ms(0, true)));
  WCHECK(watch_read_fits(5000, watch_after_wait(1000, 5000, 60000), watch_expect_ms(0, true)));
  FetchModel m;
  m.fav = true;
  WatchSource s = day_source(League::NFL);
  s.by_event = true;
  s.wait_until = 100000 + kWatchWaitMaxMs;  // its scoreboard already found the games
  m.src = {s};
  m.games = {game("1", (uint8_t) League::NFL, true, 7, 3, 2), game("2", (uint8_t) League::NFL, true, 0, 0, 1)};
  m.run(30 * 60 * 1000);
  // About one read per game per cycle, 40 cycles.
  WCHECK(m.game_reads["1"] >= 35 && m.game_reads["2"] >= 35);
  WCHECK(m.polls >= 160);
  WCHECK(m.worst_late <= 1000);
}

void test_slow_scoreboard() {
  FetchModel m;
  m.src = {day_source(League::NFL), day_source(League::NBA)};
  m.times.ms[(size_t) League::NFL] = 9200;  // an NFL Sunday's 250 KB on a slow network
  m.took = [](const WatchSource &s, FetchModel::Read) { return s.league == (uint8_t) League::NFL ? 9200u : 2000u; };
  m.run(30 * 60 * 1000);
  WCHECK(m.source_reads[1] >= 38);  // the NBA, every cycle
  WCHECK(m.source_reads[0] >= 15);  // the NFL, every other cycle
  WCHECK(m.worst_late == 0);
  WCHECK(m.polls >= 170);

  WatchTimes t;
  WCHECK(t.fits(0, 5000, (uint8_t) League::NBA, false));  // untimed: 5 s for a day
  WCHECK(!t.fits(0, 4999, (uint8_t) League::NBA, false) && t.skipped != 0);
  t.started((uint8_t) League::NBA, false);
  WCHECK(t.skipped == 0);
  t.took((uint8_t) League::NBA, false, 1800, true);
  WCHECK(t.ms[(size_t) League::NBA] == 1800 && t.fits(0, 2800, (uint8_t) League::NBA, false));
  t.took((uint8_t) League::NBA, false, 900, false);  // a quick failure says nothing of a read's time
  WCHECK(t.ms[(size_t) League::NBA] == 1800 && t.fits(0, 2800, (uint8_t) League::NBA, false));
  // A timeout hang skips one cycle, time unchanged.
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(t.ms[(size_t) League::NBA] == 1800);
  WCHECK(!t.fits(0, 60000, (uint8_t) League::NBA, false) && t.skipped == 0);
  WCHECK(t.fits(0, 60000, (uint8_t) League::NFL, false) && t.fits(0, 60000, (uint8_t) League::NBA, true));
  t.cycle();
  WCHECK(t.fits(0, 2800, (uint8_t) League::NBA, false));
  // Repeat hangs: two more cycles, then six more each.
  auto waits = [&t]() {
    int n = 0;
    for (; n < 20 && !t.fits(0, 60000, (uint8_t) League::NBA, false); n++)
      t.cycle();
    return n;
  };
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(waits() == 3);
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(waits() == 7);
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(waits() == 7);
  t.took((uint8_t) League::NBA, false, 1200, true);  // a good read's own time, and the count starts over
  WCHECK(t.ms[(size_t) League::NBA] == 1200 && t.slow_fails[(size_t) League::NBA] == 0);
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(waits() == 1);
  t.took(0, true, 120000, true);  // a game on its own, kept to the minute
  WCHECK(t.ms[kLeagueCount] == 60000);
  WCHECK(!t.fits(0, 10000, 0, true));
  t.cycle();
  WCHECK(t.ms[kLeagueCount] == 52500 && t.skipped == 0);
  t.cycle();  // not skipped again: kept
  WCHECK(t.ms[kLeagueCount] == 52500);
}

void test_failing_reads() {
  FetchModel m;
  m.src = {day_source(League::NFL), day_source(League::NBA)};
  m.took = [](const WatchSource &s, FetchModel::Read) { return s.league == (uint8_t) League::NFL ? 20000u : 2000u; };
  m.answers = [](const WatchSource &s, FetchModel::Read) { return s.league != (uint8_t) League::NFL; };
  m.run(30 * 60 * 1000);
  WCHECK(m.source_reads[1] >= 38);  // the NBA, every cycle
  WCHECK(m.source_reads[0] >= 5 && m.source_reads[0] <= 9);  // cycles 1, 2, 5, then every 7th
  WCHECK(m.late_polls <= m.source_reads[0]);  // only the hung read's retry can delay a poll
  WCHECK(m.polls >= 160);

  // An always-failing game drops out after two tries.
  FetchModel t;
  t.poll_every = 15 * 60 * 1000;  // the panel's team's game is still to come
  WatchSource fav = day_source(League::NBA);
  fav.by_event = true;
  fav.wait_until = 100000 + kWatchWaitMaxMs;
  t.src = {fav};
  t.games = {game("A", (uint8_t) League::NBA, true, 50, 50, 2), game("B", (uint8_t) League::NBA, true, 40, 44, 2)};
  t.took = [](const WatchSource &, FetchModel::Read) { return 300u; };
  t.answers = [](const WatchSource &, FetchModel::Read id) { return id != "A"; };
  t.run(30 * 60 * 1000);
  WCHECK(t.game_reads["A"] == 2);
  WCHECK(t.games.size() == 1 && find(t.games, "B") != nullptr);
  WCHECK(t.game_reads["B"] >= 39);

  std::vector<WatchGame> games = {game("A", 1, false, 0, 0, 1), game("B", 1, false, 0, 0, 1)};
  watch_failed(games, 1, "A");
  WCHECK(games.size() == 2 && games[0].fails == 1);
  watch_failed(games, 2, "B");  // another league's id
  WCHECK(games.size() == 2 && games[1].fails == 0);
  watch_failed(games, 1, "A");
  WCHECK(games.size() == 1 && find(games, "B") != nullptr);
}

void test_failed_read_is_no_read() {
  WatchGame prev;
  snprintf(prev.event_id, sizeof(prev.event_id), "9");
  prev.league = (uint8_t) League::WNBA;
  watch_store(prev, basketball(98, 100, 4, "1:30"));
  prev.read_cycle = prev.tried_cycle = 1;
  prev.tried_cycle = 3;  // tried at cycles 2 and 3, both failed
  prev.fails = 1;
  WatchJob job;
  job.league = (uint8_t) League::WNBA;
  job.ctx = live_ctx();
  job.games = {prev};
  job.cycle = 4;
  GameSnapshot cur = basketball(102, 100, 4, "1:10");
  cur.team_id = 1;
  cur.opp_id = 2;
  watch_take(job, cur);
  WCHECK(!job.alert.valid && job.fresh.size() == 1);
  WCHECK(job.fresh[0].read_cycle == 4 && job.fresh[0].tried_cycle == 4 && job.fresh[0].fails == 0);
  // Two cycles on is still close enough.
  job.cycle = 3;
  job.fresh.clear();
  watch_take(job, cur);
  WCHECK(job.alert.valid && job.alert.splash.text.find("LEAD") != std::string::npos);
}

// Watch lists hold at most eight games.
void test_bounded() {
  std::vector<WatchGame> fresh;
  for (int i = 0; i < 60; i++) {
    char id[8];
    snprintf(id, sizeof(id), "%d", i);
    WatchGame g = game(id, (uint8_t) League::MCBB, i == 41, 0, 0, 0, GameState::NOT_FOUND);
    watch_keep(fresh, g);
  }
  WCHECK(fresh.size() == kWatchMax && fresh.capacity() <= kWatchMax);
  WCHECK(find(fresh, "41") != nullptr);  // the favorite's game
  std::vector<WatchGame> games = {game("x", (uint8_t) League::NBA, false, 100, 99, 4)};
  watch_merge(games, fresh, WatchScope{});
  WCHECK(games.size() == kWatchMax && games.capacity() <= kWatchMax);
  WCHECK(std::string(games[0].event_id) == "41");
  WCHECK(find(games, "x") != nullptr);  // a 1-point game late beats one not read yet
  // A scoreboard read of 16 games keeps the best eight.
  std::vector<WatchGame> day;
  for (int i = 0; i < 16; i++) {
    char id[8];
    snprintf(id, sizeof(id), "n%d", i);
    watch_keep(day, game(id, (uint8_t) League::NFL, false, 3 * i, 0, 4));
  }
  WCHECK(day.size() == kWatchMax && find(day, "n0") != nullptr && find(day, "n15") == nullptr);
}

// Only the first stay may move to another big play.
void test_visit() {
  WatchCtx c = live_ctx();
  c.board_league = c.visit_league = (uint8_t) League::NFL;
  c.board_event = "A";  // the mode's game, as the visit saved it
  c.visit_event = "V";
  WCHECK(watch_wants(c, (uint8_t) League::NFL, "A", 1, 2) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NFL, "V", 1, 2) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NFL, "B", 1, 2) == 1);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "A", 1, 2) == 1);
  const std::string ncaa = slurp("fixtures/day_ncaa_20261017.json");
  WatchCtx n = live_ctx();
  n.board_league = n.visit_league = (uint8_t) League::NCAA;
  n.board_event = "401856725";
  n.visit_event = "401856724";
  WCHECK(read_day(ncaa, League::NCAA, n).fresh.size() == 1);

  WatchSwitch s;
  s.away_long = true;  // a stay already away
  WCHECK(!watch_may_switch(s));
  s.away_long = false;
  WCHECK(watch_may_switch(s));
  // The panel's team kicked off while the board was away.
  s.own_live = true;
  WCHECK(watch_visit_over(1000, 900, 60000, s));
  s.alert_fav = s.from_mine = true;  // unless a favorite's big play may take over
  WCHECK(!watch_visit_over(1000, 900, 60000, s));

  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.kickoff_epoch = 1800000000;
  WCHECK(!watch_live(g, 1799999999));
  WCHECK(watch_live(g, 1800000000));  // its start time: live, whatever ESPN says yet
  g.time_tbd = true;
  WCHECK(!watch_live(g, 1800009999));
  g.time_tbd = false;
  g.state = GameState::IN;
  WCHECK(watch_live(g, 0));
  g.state = GameState::POST;
  WCHECK(!watch_live(g, 1800009999));
  g.state = GameState::IN;
  g.valid = false;
  WCHECK(!watch_live(g, 1800009999));
}

// A game leaving the board re-reads its league next cycle.
void test_board_moves() {
  const uint32_t now = 100000;
  std::vector<WatchSource> src = {day_source(League::NFL), day_source(League::NBA)};
  WatchKept kept;
  WatchCtx c = live_ctx();
  c.board_league = (uint8_t) League::NFL;
  c.board_event = "401";
  c.board_live = true;
  watch_board_moved(src, kept, c);
  WCHECK(kept.board != 0 && kept.board_league == (uint8_t) League::NFL);
  for (auto &s : src) {
    s.read_cycle = 1;
    s.wait_until = watch_wait_until(s, false, now, 1800000000, 0);
  }
  WCHECK(src[0].wait_until == now + kWatchWaitMaxMs);
  WCHECK(watch_next(src, {}, 2, now + kWatchCycleMs).source == -1);
  watch_board_moved(src, kept, c);  // the same board: the waits stand
  WCHECK(src[0].wait_until == now + kWatchWaitMaxMs && src[1].wait_until == now + kWatchWaitMaxMs);
  c.board_league = (uint8_t) League::NBA;  // Live games moved on to a basketball game
  c.board_event = "88";
  watch_board_moved(src, kept, c);
  WCHECK(src[0].wait_until == 0);                      // the football game that left can be watched now
  WCHECK(src[1].wait_until == now + kWatchWaitMaxMs);  // no basketball game left the board
  WCHECK(watch_next(src, {}, 2, now + kWatchCycleMs).source == 0);
  // A visit's game leaves the board only when it ends.
  src[0].wait_until = 5;
  c.visit_league = (uint8_t) League::NFL;
  c.visit_event = "402";
  watch_board_moved(src, kept, c);
  WCHECK(src[0].wait_until == 5);
  c.visit_league = 0;
  c.visit_event = "";
  watch_board_moved(src, kept, c);
  WCHECK(src[0].wait_until == 0);
  // The board's game ending frees its league once.
  src[1].wait_until = 5;
  c.board_live = false;
  watch_board_moved(src, kept, c);
  WCHECK(src[1].wait_until == 0);
  src[1].wait_until = 5;
  watch_board_moved(src, kept, c);
  WCHECK(src[1].wait_until == 5);

  // Playlist cards not on never reset the waits.
  std::vector<WatchSource> fav;
  for (League l : {League::NFL, League::MLB, League::NBA, League::NHL}) {
    WatchSource s = day_source(l);
    s.by_event = true;
    s.read_cycle = 1;
    s.wait_until = now + kWatchWaitMaxMs;
    fav.push_back(s);
  }
  WatchKept fk;
  const char *cards[] = {"11", "22", "33", "44"};
  for (int i = 0; i < 40; i++) {
    WatchCtx f;
    f.mask = kAlertsAll;
    f.board_league = fav[(i * 3) % 4].league;
    f.board_event = cards[(i * 3) % 4];
    watch_board_moved(fav, fk, f);
  }
  for (const auto &s : fav)
    WCHECK(s.wait_until == now + kWatchWaitMaxMs);
  WCHECK(watch_next(fav, {}, 41, now + 40 * kWatchCycleMs).source == -1);

  // Board card VAN at 23:00: its start sets the wait.
  const std::string ncaa = slurp("fixtures/day_ncaa_20261017.json");
  WatchCtx vf;
  vf.mask = kAlertsAll;
  vf.favs = {{(uint8_t) League::NCAA, 238}};
  vf.board_league = (uint8_t) League::NCAA;
  vf.board_event = "401856726";
  WatchJob day = read_day(ncaa, League::NCAA, vf);
  WCHECK(day.ok && day.fresh.empty() && day.next_start == 1792278000);
  vf.favs.push_back({(uint8_t) League::NCAA, 201});  // OU at 23:30 too: the earlier start
  WCHECK(read_day(ncaa, League::NCAA, vf).next_start == 1792278000);
  vf.favs = {{(uint8_t) League::NCAA, 201}};  // the card isn't a favorite's: only OU's start
  WCHECK(read_day(ncaa, League::NCAA, vf).next_start == 1792279800);
  vf.mask = kAlertsAll & ~watch_sport_mask(Sport::FOOTBALL);  // no football big play switched on
  WCHECK(read_day(ncaa, League::NCAA, vf).next_start == 0);
}

// Reads left out one cycle go first the next.
void test_fair_reads() {
  FetchModel m;
  m.src = {day_source(League::NFL), day_source(League::MLB),  day_source(League::MLS),
           day_source(League::NBA), day_source(League::WNBA), day_source(League::NHL)};
  m.took = [](const WatchSource &, FetchModel::Read) { return 6000u; };
  m.run(30 * 60 * 1000);
  int total = 0;
  for (int n : m.source_reads) {
    WCHECK(n >= 19);  // over 40 cycles
    total += n;
  }
  WCHECK(total >= 170);  // the gaps were full
  WCHECK(m.worst_late == 0);

  FetchModel g;
  WatchSource s = day_source(League::NFL);
  s.by_event = true;
  s.wait_until = 100000 + kWatchWaitMaxMs;
  g.src = {s};
  for (const char *id : {"1", "2", "3", "4", "5", "6"})
    g.games.push_back(game(id, (uint8_t) League::NFL, true, 0, 0, 1));
  g.took = [](const WatchSource &, FetchModel::Read) { return 6000u; };
  g.run(30 * 60 * 1000);
  for (const char *id : {"1", "2", "3", "4", "5", "6"})
    WCHECK(g.game_reads[id] >= 19);
  WCHECK(g.worst_late == 0);
}

// One hang costs a cycle's reads, not minutes.
void test_one_hang() {
  FetchModel m;
  m.fav = true;
  WatchSource s = day_source(League::NBA);
  s.by_event = true;
  s.wait_until = 100000 + kWatchWaitMaxMs;
  m.src = {s};
  m.games = {game("B", (uint8_t) League::NBA, true, 50, 50, 2), game("C", (uint8_t) League::NBA, true, 40, 44, 2)};
  int b_reads = 0;
  m.took = [&b_reads](const WatchSource &, FetchModel::Read id) {
    return id == "B" && b_reads++ == 3 ? 20000u : 1500u;
  };
  int b_answers = 0;
  m.answers = [&b_answers](const WatchSource &, FetchModel::Read id) { return !(id == "B" && b_answers++ == 3); };
  m.run(30 * 60 * 1000);
  WCHECK(m.game_reads["B"] >= 37 && m.game_reads["C"] >= 37);  // 40 cycles, one lost
  WCHECK(m.late_polls <= 2);
}

// A followed tour's leaderboard counts as the team's game.
void test_leaderboard_team() {
  GameSnapshot golf;
  golf.valid = true;
  golf.sport = Sport::GOLF;
  golf.state = GameState::IN;
  golf.event_id = "401703500";
  WCHECK(watch_own_game(golf, (uint8_t) League::PGA, 1106));
  WCHECK(!watch_own_game(golf, (uint8_t) League::PGA, 0));
  WCHECK(!watch_own_game(golf, (uint8_t) League::F1, 2030));  // another sport's leaderboard
  golf.golf.phase = GolfPhase::LIVE;
  WCHECK(watch_live(golf, 1800000000));
  golf.golf.phase = GolfPhase::BREAK;  // overnight between rounds
  WCHECK(!watch_live(golf, 1800000000));
  GameSnapshot race;
  race.valid = true;
  race.sport = Sport::RACING;
  race.state = GameState::IN;
  race.event_id = "600057";
  race.race.phase = RacePhase::LIVE;
  race.race.session_start = 1800000000;
  WCHECK(watch_own_game(race, (uint8_t) League::F1, 2030));
  WCHECK(watch_live(race, 1800000600));
  race.race.phase = RacePhase::BREAK;
  WCHECK(!watch_live(race, 1800000600));
  // A team's game still goes by its ids.
  GameSnapshot nfl;
  nfl.valid = true;
  nfl.sport = Sport::FOOTBALL;
  nfl.team_id = 6;
  nfl.opp_id = 21;
  WCHECK(watch_own_game(nfl, (uint8_t) League::NFL, 21) && !watch_own_game(nfl, (uint8_t) League::NFL, 7));

  golf.golf.phase = GolfPhase::LIVE;
  WatchSwitch sw;
  sw.own_live = watch_own_game(golf, (uint8_t) League::PGA, 1106) && watch_live(golf, 1800000000);
  sw.alert_fav = true;  // My team: a favorite's big play
  WCHECK(!watch_may_switch(sw));
  WCHECK(watch_visit_over(1000, 990, 60000, sw));
  sw.from_mine = true;
  WCHECK(watch_may_switch(sw));
  WCHECK(!watch_visit_over(1000, 990, 60000, sw));
}

// After a visit, splash only a final reached meanwhile.
void test_back_prev() {
  const GameSnapshot before = basketball(98, 97, 4, "0:40");
  const GameSnapshot won = basketball(104, 99, 4, "0.0", GameState::POST);
  WCHECK_EQ(decide_splash(watch_back_prev(before, won), won, true, false, kAlertsAll, false).text, "NY WINS!");
  WCHECK(decide_splash(GameSnapshot{}, won, true, false, kAlertsAll, false).text.empty());  // as against nothing
  WCHECK_EQ(decide_splash(watch_back_prev(before, won), won, true, true, kAlertsAll, false).text, "NY WINS!");
  const GameSnapshot lost = basketball(99, 104, 4, "0.0", GameState::POST);
  WCHECK_EQ(decide_splash(watch_back_prev(before, lost), lost, true, false, kAlertsAll, false).text, "FINAL");
  WCHECK(decide_splash(watch_back_prev(before, lost), lost, true, false, 0, false).text.empty());
  // A lead change while the board was away: none.
  const GameSnapshot lead = basketball(98, 101, 4, "0:20");
  WCHECK(!decide_splash(before, lead, true, false, kAlertsAll, false).text.empty());
  WCHECK(decide_splash(watch_back_prev(before, lead), lead, true, false, kAlertsAll, false).text.empty());
  WCHECK(!watch_back_prev(before, lead).valid);
  // Another game on the board by then: none.
  GameSnapshot other = won;
  other.event_id = "8";
  WCHECK(!watch_back_prev(before, other).valid);
  // A final that isn't completed (called off): no winner.
  GameSnapshot off = won;
  off.completed = false;
  WCHECK(decide_splash(watch_back_prev(before, off), off, true, false, kAlertsAll, false).text.empty());
  // A leaderboard splashes only its winner.
  GameSnapshot g0;
  g0.valid = true;
  g0.sport = Sport::GOLF;
  g0.event_id = "401703500";
  g0.golf.phase = GolfPhase::LIVE;
  GameSnapshot g1 = g0;
  g1.golf.phase = GolfPhase::FINAL;
  const GameSnapshot gp = watch_back_prev(g0, g1);
  WCHECK(gp.valid && gp.event_id == "401703500" && gp.golf.phase == GolfPhase::LIVE);
}

// Idle scores and the watcher share the fetch task.
void test_idle_scores() {
  auto model = [](uint32_t side_every) {
    FetchModel m;
    m.poll_every = 5 * 60 * 1000;
    m.side_every = side_every;
    m.side_took = 4000;
    WatchSource s = day_source(League::NFL);
    s.by_event = true;
    s.wait_until = 100000 + kWatchWaitMaxMs;  // its scoreboard already found the games
    m.src = {s};
    m.games = {game("1", (uint8_t) League::NFL, true, 7, 3, 2), game("2", (uint8_t) League::NFL, true, 0, 0, 1)};
    m.run(2 * 3600 * 1000);
    return m;
  };
  FetchModel alone = model(0);
  WCHECK(alone.worst_late == 0 && alone.polls >= 24);
  WCHECK(alone.game_reads["1"] >= 155 && alone.game_reads["2"] >= 155);  // 160 cycles in 2 hours
  FetchModel shared = model(30 * 60 * 1000);  // the scores' scan every half hour, as with games on
  WCHECK(shared.side_reads >= 4);
  WCHECK(shared.game_reads["1"] >= 155 && shared.game_reads["2"] >= 155);
  WCHECK(shared.polls >= 24 && shared.worst_late <= shared.side_took);
  // Even minute-by-minute scores leave the watcher every cycle.
  FetchModel busy = model(60 * 1000);
  WCHECK(busy.side_reads >= 115);
  WCHECK(busy.game_reads["1"] >= 155 && busy.game_reads["2"] >= 155);
  WCHECK(busy.worst_late <= busy.side_took);
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
  test_follow();
  test_bets();
  test_final_read();
  test_favorites_tick();
  test_slow_scoreboard();
  test_failing_reads();
  test_failed_read_is_no_read();
  test_bounded();
  test_visit();
  test_board_moves();
  test_fair_reads();
  test_one_hang();
  test_leaderboard_team();
  test_back_prev();
  test_idle_scores();
  total_checks += checks;
  return failures;
}
