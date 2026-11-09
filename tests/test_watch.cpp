// The big-play watcher (components/gameday/watch.h): which games it watches,
// what a read of one says against the last (real day scoreboards from
// fixtures/ with the score or the play moved on), when a read may run, and
// when the board switches and goes back.
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
  // A sport with none of its big plays switched on isn't watched: basketball
  // with only the final, which never switches the board.
  c.mask = alert_bit(Alert::FINAL) | alert_bit(Alert::HOCKEY_GOAL);
  WCHECK(watch_wants(c, (uint8_t) League::NBA, "1", 11, 5) == 0);
  WCHECK(watch_wants(c, (uint8_t) League::NHL, "1", 4, 5) == 0);  // not a favorite, Favorite teams only on
  c.fav_only = false;
  WCHECK(watch_wants(c, (uint8_t) League::NHL, "1", 4, 5) == 1);
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
  // BC @ CIN, ESPN's last play an interception, id 401856777515. Cincinnati
  // (2132) is a favorite, so its game is among the 8 a read keeps of the 18 on.
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
  // The same play read twice is not new.
  WatchJob again = read_day(sb, League::NCAA, cin, next.fresh);
  WCHECK(!again.alert.valid);
  // Without ids a turnover can't be told new: none.
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
  std::vector<WatchSource> live =
      watch_sources(true, {League::NFL, League::NCAA, League::NHL, League::PGA}, {}, kAlertsAll);
  WCHECK(live.size() == 3);
  // None for a sport with no big play switched on: hockey's goal off.
  WCHECK(watch_sources(true, {League::NFL, League::NCAA, League::NHL}, {}, kAlertsAll & ~alert_bit(Alert::HOCKEY_GOAL))
             .size() == 2);
  WCHECK(watch_sources(true, {League::NBA}, {}, alert_bit(Alert::FINAL)).empty());
  WCHECK(watch_sources(true, {League::NBA}, {}, 0).empty());
  WCHECK(live[0].league == (uint8_t) League::NFL && !live[0].by_event && !live[0].fed);
  WCHECK(live[1].league == (uint8_t) League::NCAA && live[1].by_event && live[1].fed);
  // Favorites: by event, a college one by its conference, each once.
  const Team *dal = team(League::NFL, 6), *phi = team(League::NFL, 21), *tex = team(League::NCAA, 251),
             *tamu = team(League::NCAA, 245);
  WCHECK(dal && phi && tex && tamu);
  std::vector<WatchSource> fav = watch_sources(false, {}, {dal, phi, tex, tamu}, kAlertsAll);
  WCHECK(fav.size() == 2);
  WCHECK(fav[0].league == (uint8_t) League::NFL && fav[0].group == 0 && fav[0].by_event && !fav[0].fed);
  WCHECK(fav[1].league == (uint8_t) League::NCAA && fav[1].group == tex->group && fav[1].group != 0);
  // Minor league baseball comes from MLB, not ESPN: never a source, in Live
  // games or as a favorite, and none of its games is wanted, even the
  // panel's team's.
  const Team *dur = team(League::MILB, 234);
  WCHECK(dur != nullptr);
  const std::vector<WatchSource> with_milb = watch_sources(true, {League::MLB, League::MILB}, {}, kAlertsAll);
  WCHECK(with_milb.size() == 1 && with_milb[0].league == (uint8_t) League::MLB);
  WCHECK(watch_sources(true, {League::MILB}, {}, kAlertsAll).empty());
  WCHECK(watch_sources(false, {}, {dur}, kAlertsAll).empty());
  WCHECK(watch_sources(false, {}, {dur, dal}, kAlertsAll).size() == 1);
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
  }

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
  g.read_cycle = g.tried_cycle = 1;
  games.push_back(g);
  r = watch_next(fav, games, 2, now + 45000);
  WCHECK(r.source == 0 && r.game == 0);
  games[0].tried_cycle = 2;
  WCHECK(watch_next(fav, games, 2, now + 45000).source == -1);  // both scoreboards wait
  WCHECK(!watch_next(fav, games, 2, now + 45000).waiting);
  // The conference's next start is here: its scoreboard, last read a cycle
  // before the game, goes first, then the game again.
  r = watch_next(fav, games, 3, now + 600000);
  WCHECK(r.source == 1 && r.game == -1);
  fav[1].read_cycle = 3;
  r = watch_next(fav, games, 3, now + 600000);
  WCHECK(r.source == 0 && r.game == 0);

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
  const WatchSwitch away;
  WCHECK(!watch_visit_over(1059999, 1000000, 60000, away));
  WCHECK(watch_visit_over(1060000, 1000000, 60000, away));
  WCHECK(watch_visit_over(30000, 0xFFFFFFF0u, 30000, away));  // across millis() wrapping
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

// Follow this game (follow.h) holds the mode's own game: the watcher never
// switches while it does, a visit up when it starts ends at once, so the
// board goes straight back, and what it follows is the mode's game, which
// the visit saved, never the big play's game on the board for a moment.
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
  // It holds against the mode's game, and would let go at once if it were
  // held up against the big play's game on the board.
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

  // The device page offers the switch during a visit only while the mode's
  // game can still be followed, and asking leaves the pin as it was.
  FollowPin none;
  WCHECK(follow_can_start(none, true, true, watch_mode_game(visit_game, &mode_game)) && !none.on);
  GameSnapshot ended = mode_game;
  ended.state = GameState::POST;
  WCHECK(!follow_can_start(none, true, true, watch_mode_game(visit_game, &ended)));
  WCHECK(!follow_can_start(none, true, false, mode_game));  // Favorite teams with no card up
  WCHECK(!follow_can_start(none, false, true, mode_game));  // My team
  FollowPin held;
  WCHECK(follow_start(held, true, true, mode_game, now));
  WCHECK(follow_can_start(held, true, true, ended) && held.on);  // a final it holds already
}

// Bets (bets.h): a watcher's read hands back only the games the bets are
// tied to already, those bets keep its details and take its result at the
// final, and a bet with no game yet or on another game stays as it was.
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
  Bet tied{};  // a moneyline on the away side, tied to this game already
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

  // A game the read doesn't take (the board's own, here) hands back nothing.
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

  // A leaderboard or a game with no event id a bet keeps is no bet's game.
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

// The panel's one fetch task, modeled in 50 ms steps for the scheduling
// tests: the board's polls, and between them the watcher's reads as
// start_watch_job_ starts them (watch_next with WatchTimes::fits, a new cycle
// every kWatchCycleMs, a look 250 ms after one that found nothing fitting).
// In My team and Live games the board polls every poll_every after its last
// poll; in Favorite teams (fav) a 1 s tick (FAV_TICK) polls the locked game
// once that is due and otherwise gives the watcher the tick, fitting against
// watch_fav_due. With side_every set, the idle display's scores read too, as
// start_side_job_ does from loop(): looked at once a second in the gaps
// between the board's reads, ahead of the watcher, a read every side_every
// that takes side_took and isn't fitted to the board's next read.
struct FetchModel {
  std::vector<WatchSource> src;
  std::vector<WatchGame> games;
  WatchTimes times;
  bool fav = false;
  uint32_t poll_every = 10000, poll_took = 300;
  uint32_t side_every = 0, side_took = 4000;
  int side_reads = 0;
  // What one watcher read takes and whether it answers: a source's day
  // scoreboard (event id "") or one game.
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

// Favorite teams ticks every second, so its board is always due within one:
// the watcher fits against the locked game's next poll instead, and reads
// between polls without making one late.
void test_favorites_tick() {
  WCHECK(!watch_read_fits(1000, 1000 + 1000, watch_expect_ms(0, true)));  // the tick itself never leaves room
  WCHECK(watch_fav_due(1000, true, 0, 10000, 999999) == 1000);                // never polled: due now
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999) == 11000);
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 8000) == 8000);  // a team read comes first
  WCHECK(watch_fav_due(5000, false, 1000, 10000, 8000) == 8000);  // the playlist: only team reads
  // A minor league game's poll waits out MLB's backoff: the watcher has the
  // gap until then, and an overdue team read of a waiting host is due when
  // the wait ends, not now.
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999, 60000) == 65000);
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 30000, 60000) == 30000);  // a team read before the wait ends
  WCHECK(watch_fav_due(5000, true, 1000, 10000, 999999, 1000) == 11000);  // a wait over before the poll is due
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
  // About one read of each game a cycle, 40 cycles, and the locked game's
  // poll no later than a tick would make it anyway.
  WCHECK(m.game_reads["1"] >= 35 && m.game_reads["2"] >= 35);
  WCHECK(m.polls >= 160);
  WCHECK(m.worst_late <= 1000);
}

// A day scoreboard too slow for the live game's 10 s gap no longer stops the
// reads behind it, and gets its turn again every other cycle once its time is
// expected an eighth quicker, without making a poll late.
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

  // WatchTimes on its own.
  WatchTimes t;
  WCHECK(t.fits(0, 5000, (uint8_t) League::NBA, false));  // untimed: 5 s for a day
  WCHECK(!t.fits(0, 4999, (uint8_t) League::NBA, false) && t.skipped != 0);
  t.started((uint8_t) League::NBA, false);
  WCHECK(t.skipped == 0);
  t.took((uint8_t) League::NBA, false, 1800, true);
  WCHECK(t.ms[(size_t) League::NBA] == 1800 && t.fits(0, 2800, (uint8_t) League::NBA, false));
  t.took((uint8_t) League::NBA, false, 900, false);  // a quick failure says nothing of a read's time
  WCHECK(t.ms[(size_t) League::NBA] == 1800 && t.fits(0, 2800, (uint8_t) League::NBA, false));
  // A hang to the timeout says nothing of it either: the time stands, and
  // that kind waits for the next cycle, unmarked, while the others go on.
  t.took((uint8_t) League::NBA, false, 20000, false);
  WCHECK(t.ms[(size_t) League::NBA] == 1800);
  WCHECK(!t.fits(0, 60000, (uint8_t) League::NBA, false) && t.skipped == 0);
  WCHECK(t.fits(0, 60000, (uint8_t) League::NFL, false) && t.fits(0, 60000, (uint8_t) League::NBA, true));
  t.cycle();
  WCHECK(t.fits(0, 2800, (uint8_t) League::NBA, false));
  // Hanging again right after: two cycles more, then six more each time.
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

// A read that fails, quick or hung until the HTTP timeout, never holds up
// the others: they go on that cycle, and a hung one waits for the next
// cycle, then longer each time it hangs again (WatchTimes::took).
void test_failing_reads() {
  FetchModel m;
  m.src = {day_source(League::NFL), day_source(League::NBA)};
  m.took = [](const WatchSource &s, FetchModel::Read) { return s.league == (uint8_t) League::NFL ? 20000u : 2000u; };
  m.answers = [](const WatchSource &s, FetchModel::Read) { return s.league != (uint8_t) League::NFL; };
  m.run(30 * 60 * 1000);
  WCHECK(m.source_reads[1] >= 38);  // the NBA, every cycle
  WCHECK(m.source_reads[0] >= 5 && m.source_reads[0] <= 9);  // cycles 1, 2, 5, then every 7th
  WCHECK(m.late_polls <= m.source_reads[0]);  // only a retry of the hung read can make one late
  WCHECK(m.polls >= 160);

  // My team, a game whose own read fails at once every time: the other is
  // read that same cycle, and the failing one drops out after two tries.
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

  // watch_failed on its own.
  std::vector<WatchGame> games = {game("A", 1, false, 0, 0, 1), game("B", 1, false, 0, 0, 1)};
  watch_failed(games, 1, "A");
  WCHECK(games.size() == 2 && games[0].fails == 1);
  watch_failed(games, 2, "B");  // another league's id
  WCHECK(games.size() == 2 && games[1].fails == 0);
  watch_failed(games, 1, "A");
  WCHECK(games.size() == 1 && find(games, "B") != nullptr);
}

// A failed read leaves the game's last good read where it was: a good read
// three cycles later is too far from it to tell a big play from.
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

// However many games a scan or a scoreboard lists, the lists hold eight.
void test_bounded() {
  std::vector<WatchGame> fresh;
  for (int i = 0; i < 60; i++) {
    char id[8];
    snprintf(id, sizeof(id), "%d", i);
    WatchGame g = game(id, (uint8_t) League::MCBB, i == 41, 0, 0, 0, GameState::NOT_FOUND);
    watch_keep(fresh, g);
  }
  WCHECK(fresh.size() == kWatchMax && fresh.capacity() <= kWatchMax);
  WCHECK(find(fresh, "41") != nullptr);  // the favorite's
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

// The visit: the mode's game and the visit's are both kept off, a big play
// elsewhere moves the board on only during the first stay, and the panel's
// team's game ends it from its start time on.
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
  s.alert_fav = s.from_mine = true;  // unless a favorite's big play may take it from that game
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

// A game on that leaves the board can be one to watch now: its league's day
// scoreboards are read again at the next cycle, waits or not. A card that
// isn't on coming and going (Favorite teams' playlist turns one every 10
// seconds) reads nothing again, and a game still to start sets the wait to
// its start even while it is the board's card.
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
  // A visit: nothing leaves the board when it starts, its game does when it ends.
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

  // Favorite teams' playlist, four favorites' cards in turn, none on: every
  // league's wait stands, cycle after cycle.
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

  // The card on the board is a favorite's game still to come (VAN at 23:00):
  // its start sets the wait, as if it weren't on the board.
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

// More reads due than the board's gaps can take (six leagues' day
// scoreboards of 6 s each, about four a cycle between 10 s polls): the ones
// a cycle left out go first the next, so every league is read at least every
// other cycle, close enough to tell a big play from (kWatchStaleCycles), not
// the same last ones never. The first in order keep the cycles to spare. The
// same for one source's games read one by one, in watch_rank's order.
void test_fair_reads() {
  FetchModel m;
  m.src = {day_source(League::NFL), day_source(League::MLB),  day_source(League::MLS),
           day_source(League::NBA), day_source(League::WNBA), day_source(League::NHL)};
  m.took = [](const WatchSource &, FetchModel::Read) { return 6000u; };
  m.run(30 * 60 * 1000);
  int total = 0;
  for (int n : m.source_reads) {
    WCHECK(n >= 19);  // 40 cycles
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

// Favorite teams on a live game polled every 10 s, two other favorites'
// games read one by one: one read that hangs until the HTTP timeout costs
// those games that cycle's reads, not minutes of them.
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

// A golf tour or a racing series followed like a team: its leaderboard is
// the team's game, though it names no team, and is live while a round is
// played or a session is on track. Then "Switch from my team" decides.
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

// The first read of the board's game after a visit: what happened while the
// board was away isn't splashed late, but a final reached meanwhile still
// gets its victory splash.
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
  // A leaderboard splashes only its winner: the board from before stays.
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

// My team with the scores as the idle display: the team's next game is a
// card polled every 5 minutes while two favorites' games are on. The
// scores' reads and the watcher's share the one fetch task without keeping
// each other from their reads, and the board's poll waits at most for one
// scores read, as it did before the watcher, never for a watcher read.
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
  // Scores reading often (every minute, far more than they do) still leave
  // the watcher every cycle, and the watcher leaves them every look.
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
