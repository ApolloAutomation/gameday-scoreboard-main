// prop= and propdel= keys are tested in test_set.cpp.
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "players.h"
#include "props.h"
#include "set_request.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define RCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

#define RCHECK_EQ(a, b)                                                                                   \
  do {                                                                                                    \
    checks++;                                                                                             \
    const std::string va = (a), vb = (b);                                                                 \
    if (va != vb) {                                                                                       \
      failures++;                                                                                         \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                                     \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

constexpr uint8_t NFL = (uint8_t) League::NFL, NBA = (uint8_t) League::NBA, MLB = (uint8_t) League::MLB,
                  NHL = (uint8_t) League::NHL, EPL = (uint8_t) League::EPL;
constexpr int64_t T0 = 1793000000;
constexpr int64_t KICK = T0 + 3 * 3600;
constexpr int64_t MIDNIGHT = T0 + 12 * 3600;

PlayerWatch watch_of(std::initializer_list<uint32_t> ids) {
  PlayerWatch w;
  for (uint32_t id : ids)
    w.ids[w.n++] = id;
  return w;
}

// The box read with these players watched.
std::vector<BoxLine> box(const char *file, Sport sport, std::initializer_list<uint32_t> ids, size_t *used = nullptr) {
  const std::string json = slurp(file);
  BoxCutter cut;
  StringReader in(json, &cut);
  std::vector<BoxLine> out;
  const PlayerWatch w = watch_of(ids);
  RCHECK(parse_box(in, sport, w, out), "%s parses", file);
  if (used != nullptr)
    *used = cut.used();
  return out;
}

// parse_box over a string, with the cut.
bool box_str(const std::string &json, std::initializer_list<uint32_t> ids, std::vector<BoxLine> &out,
             size_t *used = nullptr) {
  BoxCutter cut;
  StringReader in(json, &cut);
  const bool ok = parse_box(in, Sport::BASKETBALL, watch_of(ids), out);
  if (used != nullptr)
    *used = cut.used();
  return ok;
}

Prop prop(const char *wire) {
  SetOp op;
  RCHECK(parse_set("prop", wire, op, 4), "prop=%s parses", wire);
  op.prop.placed = (uint32_t) T0;
  return op.prop;
}

int value_of(const Prop &p, const std::vector<BoxLine> &lines) {
  const BoxLine *l = box_line(lines, p.athlete);
  return l != nullptr ? prop_value(p, *l) : -1;
}

BetGame game(BetPhase phase, uint32_t event = 401873051, uint32_t team = 2, uint32_t opp = 16, uint8_t league = NFL) {
  BetGame g;
  g.league = league;
  g.event = event;
  g.team_id = team;
  g.opp_id = opp;
  g.kickoff = KICK;
  g.phase = phase;
  return g;
}

void test_cut() {
  const std::string json = slurp("fixtures/summary_nba_live.json");
  const size_t end = json.find(",\"format\"");
  RCHECK(end != std::string::npos, "the fixture goes on past its box score");
  size_t used = 0;
  box("fixtures/summary_nba_live.json", Sport::BASKETBALL, {4431680}, &used);
  RCHECK(used == end, "the read stops at the box score's end: %zu, not %zu", used, end);

  const std::string mlb = slurp("fixtures/summary_mlb_final.json");
  box("fixtures/summary_mlb_final.json", Sport::BASEBALL, {5149101}, &used);
  RCHECK(used == mlb.find(",\"format\""), "a summary that opens with notes stops there too");

  // Keys named alike deeper down don't count.
  const std::string odd = R"({"x":{"boxscore":1},"note":"boxscore","b\"x":"}","boxscore":{"players":[]},"plays":[1,2]})";
  std::vector<BoxLine> out;
  RCHECK(box_str(odd, {1}, out, &used) && used == odd.find(",\"plays\""),
         "only the root's boxscore member ends the read");
  RCHECK(!box_str(R"({"header":{"id":"1"}})", {1}, out), "a document with no box score is refused");
  RCHECK(!box_str(R"({"boxscore":{"players":[{"statistics":[)", {1}, out), "a box score cut short is refused");
  RCHECK(box_str(R"({"boxscore":{"players":[]}})", {1}, out), "a box score at the very end reads too");
}

void test_football_box() {
  // BUF @ MIN at 8:44 of the 2nd.
  auto live = box("fixtures/summary_nfl_live.json", Sport::FOOTBALL, {3918298, 4379399, 4565190, 1});
  const BoxLine *allen = box_line(live, 3918298);
  RCHECK(allen != nullptr && allen->played, "Allen's line");
  if (allen != nullptr) {
    RCHECK(allen->v[atom::CMP] == 10 && allen->v[atom::PASS_YDS] == 86 && allen->v[atom::INT] == 2 &&
               allen->v[atom::PASS_TD] == 0,
           "passing: 5 completions, 43 yards, 1 interception");
    RCHECK(allen->v[atom::RUSH_ATT] == 2 && allen->v[atom::RUSH_YDS] == 14, "and his run");
  }
  const BoxLine *turner = box_line(live, 4565190);
  RCHECK(turner != nullptr && turner->v[atom::TACKLES] == 8 && turner->v[atom::SACKS] == 2, "a sack and 4 tackles");
  RCHECK(box_line(live, 1) == nullptr, "a player not in the box has no line");
  RCHECK(live.size() == 3, "only watched players are kept");

  // JAX @ BAL final: touchdowns and a kicker's points.
  auto fin = box("fixtures/summary_nfl_final.json", Sport::FOOTBALL, {4882093, 4432620, 4686361, 3043078, 3916433});
  Prop td = prop("0,nfl,4882093,30,TUTEN,td,yes,");
  RCHECK(value_of(td, fin) == 2, "a rushing touchdown counts as any TD");
  td.athlete = 4432620;
  RCHECK(value_of(td, fin) == 2, "a receiving touchdown too");
  td.athlete = 3916433;
  RCHECK(value_of(td, fin) == 0, "no touchdown");
  const BoxLine *henry = box_line(fin, 3043078);
  RCHECK(henry != nullptr && henry->v[atom::PASS_YDS] == -6 && henry->v[atom::RUSH_YDS] == 114,
         "a running back's -3 passing yards and 57 rushing");
  Prop rr = prop("0,nfl,3043078,33,HENRY,rryds,over,60.5");
  RCHECK(value_of(rr, fin) == 114, "rush and receiving yards add up");
  const BoxLine *little = box_line(fin, 4686361);
  RCHECK(little != nullptr && little->v[atom::KICK_PTS] == 30, "15 kicking points");

  // College has the same box.
  auto ncaa = box("fixtures/summary_ncaa_final.json", Sport::FOOTBALL, {5079369, 4804878});
  const BoxLine *carr = box_line(ncaa, 5079369);
  RCHECK(carr != nullptr && carr->v[atom::PASS_YDS] == 638 && carr->v[atom::PASS_TD] == 6, "CJ Carr 319 and 3");
  const BoxLine *barkate = box_line(ncaa, 4804878);
  RCHECK(barkate != nullptr && barkate->v[atom::REC] == 20 && barkate->v[atom::REC_TD] == 4, "10 catches, 2 scores");
}

void test_other_boxes() {
  auto nba = box("fixtures/summary_nba_live.json", Sport::BASKETBALL, {4431680});
  Prop pra = prop("0,nba,4431680,1,OKONGWU,pra,over,30.5");
  RCHECK(value_of(pra, nba) == 56, "16 points, 9 rebounds, 3 assists");
  Prop threes = prop("0,nba,4431680,1,OKONGWU,threes,over,1.5");
  RCHECK(value_of(threes, nba) == 4, "2 of 5 threes is 2");

  auto fin = box("fixtures/summary_nba_final.json", Sport::BASKETBALL, {5176517, 4396971});
  const BoxLine *dnp = box_line(fin, 5176517);
  RCHECK(dnp != nullptr && !dnp->played, "a player who didn't play has no stats");
  const BoxLine *reid = box_line(fin, 4396971);
  RCHECK(reid != nullptr && reid->played && reid->v[atom::PTS] == 50 && reid->v[atom::REB] == 26, "25 and 13");

  auto mcbb = box("fixtures/summary_mcbb_final.json", Sport::BASKETBALL, {5144139});
  const BoxLine *jemison = box_line(mcbb, 5144139);
  RCHECK(jemison != nullptr && jemison->v[atom::THREES] == 12 && jemison->v[atom::PTS] == 40, "college: 6 threes, 20");

  auto mlb = box("fixtures/summary_mlb_final.json", Sport::BASEBALL, {5149101, 5080761});
  Prop hrr = prop("0,mlb,5149101,8,PRATT,hrr,over,2.5");
  RCHECK(value_of(hrr, mlb) == 10, "2 hits, a run, 2 RBI");
  const BoxLine *miz = box_line(mlb, 5080761);
  RCHECK(miz != nullptr && miz->v[atom::OUTS] == 34 && miz->v[atom::K] == 4 && miz->v[atom::HA] == 4 &&
             miz->v[atom::ER] == 0 && miz->v[atom::H] == 0,
         "5.2 innings is 17 outs; a pitcher's hits allowed aren't his hits");

  auto nhl = box("fixtures/summary_nhl_live.json", Sport::HOCKEY, {3114727, 4894487});
  const BoxLine *leon = box_line(nhl, 3114727);
  RCHECK(leon != nullptr && leon->v[atom::G] == 2 && leon->v[atom::SOG] == 4 && leon->v[atom::A] == 0,
         "a goal on 2 shots");
  const BoxLine *levi = box_line(nhl, 4894487);
  RCHECK(levi != nullptr && levi->v[atom::SV] == 26 && levi->v[atom::G] == 0, "a goalie's 13 saves, no goals");
  auto final_nhl = box("fixtures/summary_nhl_final.json", Sport::HOCKEY, {4319858});
  Prop sog = prop("0,nhl,4319858,26,TKACHUK,sog,over,4.5");
  RCHECK(value_of(sog, final_nhl) == 14, "7 shots on goal, never the shootout column");
}

void test_parse_prop() {
  SetOp op;
  RCHECK(parse_set("prop", "0,nfl,3918298,2,Allen,passyds,over,250.5", op, 4) && op.kind == SetKind::PROP &&
             op.slot == 0 && op.prop.athlete == 3918298 && op.prop.team_id == 2 && op.prop.line == 501 &&
             op.prop.side == (uint8_t) PropSide::OVER && std::string(op.prop.name) == "ALLEN" && prop_valid(op.prop),
         "an over");
  RCHECK(parse_set("prop", "3,nba,4431680,1,OKONGWU,reb,under,9", op, 4) && op.slot == 3 && op.prop.line == 18 &&
             op.prop.side == (uint8_t) PropSide::UNDER,
         "a whole-number under can push");
  RCHECK(parse_set("prop", "0,mlb,1,15,JUDGE,hr,yes,", op, 4) && op.prop.side == (uint8_t) PropSide::YES &&
             op.prop.line == 0 && prop_valid(op.prop),
         "a yes prop has no line");
  RCHECK(parse_set("prop", "0,epl,280555,359,SAKA,goal,yes,", op, 4) && prop_valid(op.prop), "soccer's anytime goal");
  const char *refused[] = {
      "0,nfl,3918298,2,ALLEN,passyds,over,",          // no line given
      "0,nfl,3918298,2,ALLEN,passyds,over,250.25",    // quarter points are refused
      "0,nfl,3918298,2,ALLEN,passyds,over,0",         // a line of 0
      "0,nfl,3918298,2,ALLEN,passyds,over,-10.5",     // a sign on the line
      "0,nfl,3918298,2,ALLEN,passyds,over,1000",      // a line past 999.5
      "0,nfl,3918298,2,ALLEN,passyds,yes,",           // yes on an over/under stat
      "0,nfl,3918298,2,ALLEN,td,over,0.5",            // over on a yes stat
      "0,nfl,3918298,2,ALLEN,td,yes,1",               // a yes with a line
      "0,nfl,3918298,2,ALLEN,pts,over,20.5",          // basketball's stat in football
      "0,nfl,3918298,2,ALLEN,PASSYDS,over,20.5",      // keys are lower case
      "0,nfl,3918298,999,ALLEN,passyds,over,250.5",   // not an NFL team
      "0,nfl,0,2,ALLEN,passyds,over,250.5",           // no athlete id
      "0,nfl,3918298,2,,passyds,over,250.5",          // no name given
      "0,nfl,3918298,2,AL<b>,passyds,over,250.5",     // a name the panel can't show
      "17,nfl,3918298,2,ALLEN,passyds,over,250.5",    // past the last slot
      "99,nfl,3918298,2,ALLEN,passyds,over,250.5",    // far past it
      "100,nfl,3918298,2,ALLEN,passyds,over,250.5",   // a three-digit slot
      "01,nfl,3918298,2,ALLEN,passyds,over,250.5",    // a leading zero
      "00,nfl,3918298,2,ALLEN,passyds,over,250.5",    // zero, written twice
      "+9,nfl,3918298,2,ALLEN,passyds,over,250.5",    // a plus sign first
      " 9,nfl,3918298,2,ALLEN,passyds,over,250.5",    // a leading space
      ",nfl,3918298,2,ALLEN,passyds,over,250.5",      // no slot at all
      "0,milb,1,234,SMITH,hits,over,0.5",             // a minor league team
      "0,ahl,1,324,SMITH,pts,over,0.5",               // a HockeyTech league team
      "0,pga,1,1,SMITH,pts,over,0.5",                 // a golf tour
      "0,nfl,3918298,2,ALLEN,passyds,over,250.5,x",   // a ninth field
      "0,nfl,3918298,2,ALLEN,passyds,over",           // only seven fields
  };
  for (const char *v : refused)
    RCHECK(!parse_set("prop", v, op, 4), "prop=%s is refused", v);
  for (int slot = 1; slot <= kPropSlots; slot++) {
    const std::string v = std::to_string(slot);
    RCHECK(parse_set("propdel", v, op, 4) && op.kind == SetKind::PROP_DEL && op.slot == slot, "propdel %d", slot);
    RCHECK(parse_set("prop", v + ",nfl,3918298,2,ALLEN,td,yes,", op, 4) && op.slot == slot, "prop into slot %d", slot);
  }
  for (const char *v : {"0", "17", "99", "100", "", "01", "00", "09", "+1", "-1", " 1", "1 ", "1a", "x"})
    RCHECK(!parse_set("propdel", v, op, 4), "propdel=%s is refused", v);
  int del_at = -1, prop_at = -1, at = 0;
  for (const char *key : kSetKeys) {
    if (std::string(key) == "propdel")
      del_at = at;
    if (std::string(key) == "prop")
      prop_at = at;
    at++;
  }
  RCHECK(del_at >= 0 && del_at < prop_at, "propdel empties a slot before prop fills it");
  for (size_t i = 0; i < kPropStatCount; i++)
    for (size_t k = i + 1; k < kPropStatCount; k++)
      RCHECK(kPropStats[i].sport != kPropStats[k].sport || std::string(kPropStats[i].key) != kPropStats[k].key,
             "stat keys %s are unique within a sport", kPropStats[i].key);
}

void test_text() {
  Prop p = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  RCHECK_EQ(prop_label(p), "ALLEN O 250.5 PASS YDS");
  RCHECK_EQ(prop_ticker(p, PropStatus::PRE, -1), "PROP ALLEN O 250.5 PASS YDS");
  RCHECK_EQ(prop_ticker(p, PropStatus::ON_TRACK, 374), "ALLEN O 250.5 PASS YDS 187 ON TRACK");
  RCHECK_EQ(prop_ticker(p, PropStatus::LIVE, kPropNoValue), "ALLEN O 250.5 PASS YDS");
  RCHECK_EQ(prop_ticker(p, PropStatus::LIVE, 0), "ALLEN O 250.5 PASS YDS 0");
  p.final_half = 520;
  RCHECK_EQ(prop_ticker(p, PropStatus::WON, 300), "ALLEN O 250.5 PASS YDS 260 WON");
  RCHECK_EQ(prop_ticker(p, PropStatus::VOID, kPropNoValue), "ALLEN O 250.5 PASS YDS VOID");
  RCHECK_EQ(prop_ticker(p, PropStatus::VOID, 0), "ALLEN O 250.5 PASS YDS VOID");
  // Kneel-downs leave a quarterback below zero.
  Prop rush = prop("0,nfl,3139477,12,MAHOMES,rushyds,under,20.5");
  RCHECK_EQ(prop_ticker(rush, PropStatus::ON_TRACK, -6), "MAHOMES U 20.5 RUSH YDS -3 ON TRACK");
  rush.final_half = -6;
  RCHECK_EQ(prop_ticker(rush, PropStatus::WON, kPropNoValue), "MAHOMES U 20.5 RUSH YDS -3 WON");
  Prop u = prop("0,nfl,4565190,16,TURNER,sacks,under,1");
  RCHECK_EQ(prop_ticker(u, PropStatus::OFF_TRACK, 3), "TURNER U 1 SACKS 1.5 OFF TRACK");
  Prop y = prop("0,nfl,4882093,30,TUTEN,td,yes,");
  RCHECK_EQ(prop_label(y), "TUTEN ANY TD");
  RCHECK_EQ(prop_ticker(y, PropStatus::HIT, 2), "TUTEN ANY TD HIT");
  RCHECK_EQ(prop_ticker(y, PropStatus::NOT_YET, 0), "TUTEN ANY TD NOT YET");
  RCHECK_EQ(prop_side_key(y), "yes");
  RCHECK_EQ(prop_status_key(PropStatus::OFF_TRACK), "off_track");
  RCHECK_EQ(prop_status_key(PropStatus::VOID), "void");
}

void test_status() {
  Prop p = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  RCHECK(prop_status(p, nullptr, -1, -1, T0) == PropStatus::WAITING, "no game yet");
  BetGame live = game(BetPhase::LIVE);
  RCHECK(prop_observe(p, live, KICK + 60, MIDNIGHT) && p.event == 401873051 && p.opp_id == 16, "tied to its game");
  RCHECK(prop_status(p, &live, kPropNoValue, 300, KICK + 60) == PropStatus::LIVE, "no number yet");
  RCHECK(prop_status(p, &live, 86, 30, KICK + 60) == PropStatus::LIVE, "too early to call");
  RCHECK(prop_status(p, &live, 200, 354, KICK + 60) == PropStatus::ON_TRACK, "100 yards at 35%% is on pace for 282");
  RCHECK(prop_status(p, &live, 86, 354, KICK + 60) == PropStatus::OFF_TRACK, "43 yards at 35%% is not");
  RCHECK(prop_status(p, &live, 502, 500, KICK + 60) == PropStatus::HIT, "over the line already");
  Prop u = p;
  u.side = (uint8_t) PropSide::UNDER;
  RCHECK(prop_status(u, &live, 86, 354, KICK + 60) == PropStatus::ON_TRACK, "an under on a slow pace");
  RCHECK(prop_status(u, &live, 502, 500, KICK + 60) == PropStatus::OFF_TRACK, "an under past the line");
  BetGame pre = game(BetPhase::PRE);
  RCHECK(prop_status(p, &pre, 0, -1, KICK - 60) == PropStatus::PRE, "before the start");
  BetGame other = game(BetPhase::LIVE, 401873099);
  RCHECK(prop_status(p, &other, 0, -1, KICK + 60) == PropStatus::UNREAD, "the panel shows another game");
  RCHECK(prop_status(p, nullptr, 0, -1, KICK - 60) == PropStatus::PRE, "no game shown before the start");
  BetGame fin = game(BetPhase::FINAL);
  RCHECK(prop_status(p, &fin, 300, -1, KICK + 4 * 3600) == PropStatus::OFF_TRACK, "at the final, before grading");
  RCHECK(prop_status(u, &fin, 300, -1, KICK + 4 * 3600) == PropStatus::ON_TRACK, "an under below it");
  Prop rec = prop("0,nfl,4379399,2,COOK,rec,over,5");
  rec.event = p.event;
  RCHECK(prop_status(rec, &fin, 10, -1, KICK + 4 * 3600) == PropStatus::LIVE, "on a whole line: a push, not on track");
  rec.side = (uint8_t) PropSide::UNDER;
  RCHECK(prop_status(rec, &fin, 10, -1, KICK + 4 * 3600) == PropStatus::LIVE, "an under on it too");
  RCHECK(prop_status(rec, &live, 10, 1000, KICK + 60) == PropStatus::ON_TRACK, "live, on the line keeps its pace");
  // A negative number is a number.
  Prop rush = prop("0,nfl,3139477,12,MAHOMES,rushyds,under,20.5");
  rush.event = p.event;
  RCHECK(prop_status(rush, &live, -6, 500, KICK + 60) == PropStatus::ON_TRACK, "-3 rushing yards at the half");
  rush.side = (uint8_t) PropSide::OVER;
  RCHECK(prop_status(rush, &live, -6, 500, KICK + 60) == PropStatus::OFF_TRACK, "an over on it is off track");
  Prop y = prop("0,nfl,3918298,2,ALLEN,td,yes,");
  y.event = p.event;
  RCHECK(prop_status(y, &live, 0, 900, KICK) == PropStatus::NOT_YET, "no touchdown yet");
  RCHECK(prop_status(y, &live, 2, 100, KICK) == PropStatus::HIT, "a touchdown");
}

void test_grade() {
  Prop p = prop("0,nba,4431680,1,OKONGWU,reb,under,9");
  BetGame g = game(BetPhase::LIVE, 401909952, 1, 13, NBA);
  prop_observe(p, g, KICK, MIDNIGHT);
  RCHECK(prop_grade(p, true, 18, KICK + 9000, MIDNIGHT) && p.result == (uint8_t) PropResult::PUSH &&
             p.final_half == 18 && p.expires == (uint32_t) MIDNIGHT,
         "9 rebounds on a line of 9 pushes");
  RCHECK(!prop_grade(p, true, 18, KICK + 9100, MIDNIGHT), "the same read again changes nothing");
  RCHECK(prop_grade(p, true, 16, KICK + 9100, MIDNIGHT) && p.result == (uint8_t) PropResult::WON &&
             p.expires == (uint32_t) MIDNIGHT,
         "a corrected box grades again, the midnight kept");
  Prop o = prop("0,nba,4431680,1,OKONGWU,pts,over,20.5");
  prop_grade(o, true, 32, KICK, MIDNIGHT);
  RCHECK(o.result == (uint8_t) PropResult::LOST, "16 points under 20.5");
  prop_grade(o, false, 0, KICK, MIDNIGHT);
  RCHECK(o.result == (uint8_t) PropResult::VOID, "a player who didn't play voids it");
  Prop y = prop("0,mlb,1,15,JUDGE,hr,yes,");
  prop_grade(y, true, 2, KICK, MIDNIGHT);
  RCHECK(y.result == (uint8_t) PropResult::WON, "a home run");
  prop_grade(y, true, 0, KICK, MIDNIGHT);
  RCHECK(y.result == (uint8_t) PropResult::LOST, "none");
  RCHECK(!prop_expired(o, MIDNIGHT - 1) && prop_expired(o, MIDNIGHT), "graded props go at midnight");
  Prop neg = prop("0,nfl,3139477,12,MAHOMES,rushyds,under,20.5");
  RCHECK(prop_grade(neg, true, -6, KICK, MIDNIGHT) && neg.result == (uint8_t) PropResult::WON && neg.final_half == -6,
         "-3 rushing yards keeps its number");

  Prop off = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  prop_observe(off, game(BetPhase::LIVE), KICK, MIDNIGHT);
  RCHECK(prop_observe(off, game(BetPhase::OFF), KICK + 600, MIDNIGHT) && off.result == (uint8_t) PropResult::VOID,
         "a postponed game voids it");
  Prop old = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  old.placed = (uint32_t) (KICK + 3600);
  RCHECK(!prop_observe(old, game(BetPhase::FINAL), KICK + 4 * 3600, MIDNIGHT) && old.event == 0,
         "a game over before the prop was entered isn't its game");
  Prop other = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  RCHECK(!prop_observe(other, game(BetPhase::LIVE, 401873060, 22, 16), KICK, MIDNIGHT), "another team's game");
  RCHECK(!prop_update(other, game(BetPhase::LIVE), KICK, MIDNIGHT) && other.event == 0,
         "an unfollowed read never ties a prop");
}

// The final's box score: who played.
void test_grade_box() {
  auto nfl = box("fixtures/summary_nfl_final.json", Sport::FOOTBALL, {4882093, 3916433, 7});
  Prop td = prop("0,nfl,4882093,30,TUTEN,td,yes,");
  RCHECK(prop_grade_box(td, nfl, KICK, MIDNIGHT) && td.result == (uint8_t) PropResult::WON, "Tuten's touchdown");
  Prop none = prop("0,nfl,3916433,33,FLOWERS,td,yes,");
  prop_grade_box(none, nfl, KICK, MIDNIGHT);
  RCHECK(none.result == (uint8_t) PropResult::LOST, "listed without one");
  // Football lists only players with a stat.
  Prop gone = prop("0,nfl,7,33,KOLAR,td,yes,");
  prop_grade_box(gone, nfl, KICK, MIDNIGHT);
  RCHECK(gone.result == (uint8_t) PropResult::LOST && gone.final_half == 0, "no stat at all: no touchdown");
  Prop over = prop("0,nfl,7,33,KOLAR,rec,over,0.5");
  prop_grade_box(over, nfl, KICK, MIDNIGHT);
  RCHECK(over.result == (uint8_t) PropResult::LOST, "an over on no catches loses");
  Prop under = prop("0,nfl,7,33,KOLAR,rec,under,1.5");
  prop_grade_box(under, nfl, KICK, MIDNIGHT);
  RCHECK(under.result == (uint8_t) PropResult::WON, "an under wins");
  auto ncaa = box("fixtures/summary_ncaa_final.json", Sport::FOOTBALL, {7});
  Prop college = prop("0,ncaa,7,87,SMITH,rec,over,2.5");
  RCHECK(college.athlete == 7, "a college prop");
  prop_grade_box(college, ncaa, KICK, MIDNIGHT);
  RCHECK(college.result == (uint8_t) PropResult::LOST, "a college receiver with no catch isn't listed");

  // Other sports list who played.
  auto nba = box("fixtures/summary_nba_final.json", Sport::BASKETBALL, {5176517, 4396971});
  Prop dnp = prop("0,nba,5176517,5,DNP,pts,under,10.5");
  prop_grade_box(dnp, nba, KICK, MIDNIGHT);
  RCHECK(dnp.result == (uint8_t) PropResult::VOID, "didNotPlay voids it");
  Prop absent = prop("0,nba,9,5,ABSENT,pts,under,10.5");
  prop_grade_box(absent, nba, KICK, MIDNIGHT);
  RCHECK(absent.result == (uint8_t) PropResult::VOID, "a basketball player not listed");
  auto nhl = box("fixtures/summary_nhl_final.json", Sport::HOCKEY, {9});
  Prop scratch = prop("0,nhl,9,26,SCRATCH,sog,under,2.5");
  prop_grade_box(scratch, nhl, KICK, MIDNIGHT);
  RCHECK(scratch.result == (uint8_t) PropResult::VOID, "a scratched skater");
  auto mlb = box("fixtures/summary_mlb_final.json", Sport::BASEBALL, {9});
  Prop bench = prop("0,mlb,9,15,BENCH,hits,under,0.5");
  prop_grade_box(bench, mlb, KICK, MIDNIGHT);
  RCHECK(bench.result == (uint8_t) PropResult::VOID, "a batter who never batted");
}

// Failed reads wait longer; times kept by sport.
void test_box_timing() {
  RCHECK(box_wait_ms(30000, 0) == 30000 && box_wait_ms(30000, 1) == 60000 && box_wait_ms(30000, 4) == 480000,
         "the wait doubles");
  RCHECK(box_wait_ms(30000, 5) == kBoxWaitMaxMs && box_wait_ms(60000, 255) == kBoxWaitMaxMs, "up to 15 minutes");
  BoxTimes t;
  RCHECK(t.expect(Sport::BASEBALL) == 6000, "a first read's guess");
  t.took(Sport::BASKETBALL, 900, true);
  t.took(Sport::BASEBALL, 4200, true);
  RCHECK(t.expect(Sport::BASKETBALL) == 1900 && t.expect(Sport::BASEBALL) == 5200, "each sport its own time");
  t.took(Sport::BASKETBALL, 300, false);
  RCHECK(t.expect(Sport::BASKETBALL) == 1900, "a quick refusal changes nothing");
  t.took(Sport::BASKETBALL, 20000, false);
  RCHECK(t.expect(Sport::BASKETBALL) == 21000, "a timeout is counted");
  t.took(Sport::HOCKEY, 120000, false);
  RCHECK(t.expect(Sport::HOCKEY) == 61000, "a worker given up on, capped at a minute");
  t.skip(Sport::BASKETBALL);
  t.cycle();
  RCHECK(t.expect(Sport::BASKETBALL) == 18500 && t.expect(Sport::HOCKEY) == 61000, "only a skipped kind shrinks");
  t.started(Sport::BASKETBALL);
  t.cycle();
  RCHECK(t.expect(Sport::BASKETBALL) == 18500, "a started read stops shrinking");
  t.took(Sport::BASKETBALL, 1100, true);
  RCHECK(t.expect(Sport::BASKETBALL) == 2100, "a good read sets it again");
}

void test_store() {
  PropBlob b{};
  Prop p = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  for (int i = 0; i < kPropSlots; i++)
    RCHECK(prop_put(b.props, 0, p) == i + 1, "slot %d", i + 1);
  RCHECK(prop_put(b.props, 0, p) == 0, "every slot in use");
  RCHECK(prop_put(b.props, 3, prop("0,nba,4431680,1,OKONGWU,reb,under,9")) == 3 && b.props[2].league == NBA,
         "a slot is replaced");
  b.props[4].stat = 13;  // basketball's points on an NFL player
  b.props[5].side = (uint8_t) PropSide::YES;
  b.props[6].name[0] = 'a';
  b.props[7].league = (uint8_t) League::MILB;
  props_loaded(b);
  RCHECK(!prop_empty(b.props[0]) && prop_empty(b.props[4]) && prop_empty(b.props[5]) && prop_empty(b.props[6]) &&
             prop_empty(b.props[7]),
         "bad slots read back are emptied");
}

// Two saved halves: slots 1-8 under v2026.11.3's key.
void test_halves() {
  PropBlob b{};
  Prop p = prop("0,nba,4431680,1,OKONGWU,reb,under,9");
  for (int i = 0; i < kPropSlots; i++) {
    p.athlete = 1001 + i;
    RCHECK(prop_put(b.props, 0, p) == i + 1, "slot %d", i + 1);
  }
  RCHECK(prop_put(b.props, 0, p) == 0 && prop_put(b.props, kPropSlots + 1, p) == 0, "a seventeenth is refused");
  const PropHalf lo = prop_half(b, 0), hi = prop_half(b, 1);
  RCHECK(lo.props[0].athlete == 1001 && lo.props[7].athlete == 1008, "the first half is slots 1-8");
  RCHECK(hi.props[0].athlete == 1009 && hi.props[7].athlete == 1016, "the second half is slots 9-16");
  PropBlob joined{};
  prop_half_put(joined, 1, hi);
  prop_half_put(joined, 0, lo);
  RCHECK(memcmp(&joined, &b, sizeof(b)) == 0, "the halves join back to every slot");
  PropBlob first{};
  prop_half_put(first, 0, lo);
  bool empty = true;
  for (int i = kPropHalfSlots; i < kPropSlots; i++)
    empty = empty && prop_empty(first.props[i]);
  RCHECK(memcmp(first.props, b.props, sizeof(lo)) == 0 && empty, "no second half saved: slots 9-16 stay empty");
  // v2026.11.3's eight-slot blob is the first half, byte for byte.
  Prop old[8]{};
  old[5] = prop("0,nfl,3918298,2,ALLEN,passyds,over,250.5");
  static_assert(sizeof(old) == sizeof(PropHalf), "the old blob's size");
  PropHalf saved{};
  memcpy(&saved, old, sizeof(old));
  PropBlob upgraded{};
  prop_half_put(upgraded, 0, saved);
  props_loaded(upgraded);
  RCHECK(upgraded.props[5].athlete == 3918298 && upgraded.props[5].line == 501, "an update keeps slots 1-8");
  RCHECK(prop_half_bit(0) == 1 && prop_half_bit(7) == 1 && prop_half_bit(8) == 2 && prop_half_bit(15) == 2,
         "each slot's half");
  // The first free slot, across the halves.
  b.props[11] = Prop{};
  RCHECK(prop_put(b.props, 0, p) == 12, "a free slot in the second half");
  b.props[2] = Prop{};
  b.props[13] = Prop{};
  RCHECK(prop_put(b.props, 0, p) == 3 && prop_put(b.props, 0, p) == 14, "the first half's free slot first");
  RCHECK(prop_put(b.props, 16, prop("0,nfl,3918298,2,ALLEN,td,yes,")) == 16 && b.props[15].league == NFL,
         "slot 16 is replaced");
  b.props[12].stat = 0;  // football's passing yards on an NBA player
  b.props[15].name[0] = 'a';
  props_loaded(b);
  RCHECK(prop_empty(b.props[12]) && prop_empty(b.props[15]) && !prop_empty(b.props[8]) && !prop_empty(b.props[14]),
         "bad slots in the second half are emptied too");
}

// Sixteen props on sixteen players of one game.
void test_sixteen_box() {
  static const uint32_t kIds[] = {4360310, 2576336, 4882093, 4362619, 3916433, 4432620, 4686248, 4430539,
                                  4917427, 4879194, 4911851, 4685415, 4576129, 3050073, 4240610, 4572704};
  static_assert(sizeof(kIds) / sizeof(kIds[0]) == kPropSlots, "a player per slot");
  Prop props[kPropSlots]{};
  for (int i = 0; i < kPropSlots; i++) {
    props[i] = prop("0,nfl,3918298,2,ALLEN,td,yes,");
    props[i].athlete = kIds[i];
  }
  const PlayerWatch w = prop_box_watch(props, NFL);
  RCHECK(w.n == kPropSlots, "the box read watches all sixteen");
  const std::string json = slurp("fixtures/summary_nfl_final.json");
  BoxCutter cut;
  StringReader in(json, &cut);
  std::vector<BoxLine> lines;
  RCHECK(parse_box(in, Sport::FOOTBALL, w, lines), "the final parses");
  RCHECK(lines.size() == (size_t) kPropSlots, "a line for each: %zu", lines.size());
  for (int i = 0; i < kPropSlots; i++)
    RCHECK(box_line(lines, kIds[i]) != nullptr, "slot %d's player", i + 1);
  // Two props on one player: one line.
  props[15].athlete = kIds[0];
  RCHECK(prop_box_watch(props, NFL).n == kPropSlots - 1, "one player watched once");
}

void test_progress() {
  auto read = [](const char *file, Sport sport, uint8_t league) {
    const std::string json = slurp(file);
    JsonDocument doc;
    deserializeJson(doc, json);
    std::string id = doc["id"] | "";
    uint32_t team = (uint32_t) atol(doc["competitions"][0]["competitors"][0]["id"] | "0");
    GameSnapshot g;
    RCHECK(parse_event(json, sport, team, g, id), "%s parses", file);
    return game_progress(g, league);
  };
  RCHECK(read("fixtures/event_nfl_live.json", Sport::FOOTBALL, NFL) == 354, "8:44 in the 2nd");
  RCHECK(read("fixtures/event_nba_live.json", Sport::BASKETBALL, NBA) == 573, "8:27 in the 3rd");
  RCHECK(read("fixtures/event_wnba_live.json", Sport::BASKETBALL, (uint8_t) League::WNBA) == 498,
         "4.5 left in the WNBA's 2nd");
  RCHECK(read("fixtures/event_nhl_live.json", Sport::HOCKEY, NHL) == 277, "3:21 left in the 1st");
  RCHECK(read("fixtures/event_mlb_top.json", Sport::BASEBALL, MLB) == 703, "top 7th, two out");
  RCHECK(read("fixtures/event_mlb_end.json", Sport::BASEBALL, MLB) == 555, "end of the 5th");
  RCHECK(read("fixtures/event_nba_final.json", Sport::BASKETBALL, NBA) == 1000, "a final");
  RCHECK(read("fixtures/event_nfl_pre.json", Sport::FOOTBALL, NFL) == -1, "before the start");
  RCHECK(read("fixtures/event_wnba_ot.json", Sport::BASKETBALL, (uint8_t) League::WNBA) == 1000, "overtime");
}

// A game read's watch: props only, no favorites.
PlayerWatch read_watch(const Prop *props, uint8_t league) {
  static const FavPlayer none[kPlayerSlots]{};
  return roster_watch(watch_roster(none, props), league);
}

void test_soccer() {
  const std::string json = slurp("fixtures/event_epl_final.json");
  Prop props[kPropSlots]{};
  props[0] = prop("0,epl,280555,359,SAKA,goal,yes,");
  props[1] = prop("0,nfl,3918298,2,ALLEN,td,yes,");
  const PlayerWatch w = read_watch(props, EPL);
  RCHECK(w.n == 1 && w.ids[0] == 280555, "soccer prop players join the game read");
  GameSnapshot g;
  RCHECK(parse_event(json, Sport::SOCCER, 359, g, "", &w), "the final parses");
  RCHECK(soccer_goals(g, 280555) == 2, "Saka's penalty counts");
  RCHECK(soccer_goals(g, 218522) == 2, "with a watch, every scorer's id comes along");
  g.soc.goals[0].own_goal = true;
  RCHECK(soccer_goals(g, 218522) == 0, "an own goal isn't the player's");

  // Extra time isn't a sportsbook's anytime goal.
  RCHECK(soccer_regulation("90'+5'") && soccer_regulation("45'+3'") && soccer_regulation("52'"), "regulation");
  RCHECK(!soccer_regulation("92'") && !soccer_regulation("105'+1'") && !soccer_regulation("120'"), "extra time");
  const std::string aet = slurp("fixtures/event_soccer_aet.json");
  Prop et[kPropSlots]{};
  et[0] = prop("0,epl,45843,359,EARLY,goal,yes,");
  et[1] = prop("0,epl,233937,359,LATE,goal,yes,");
  et[2] = prop("0,epl,332394,359,LATER,goal,yes,");
  const PlayerWatch ew = read_watch(et, EPL);
  GameSnapshot a;
  RCHECK(parse_event(aet, Sport::SOCCER, 202, a, "", &ew), "the extra-time final parses");
  RCHECK(soccer_goals(a, 45843) == 2, "29' counts");
  RCHECK(soccer_goals(a, 233937) == 0 && soccer_goals(a, 332394) == 0, "92' and 103' in extra time don't");
  RCHECK(read_watch(props, NFL).n == 0, "other sports read their box score instead");
  const PlayerWatch bw = prop_box_watch(props, NFL);
  RCHECK(bw.n == 1 && bw.ids[0] == 3918298, "the box read keeps the league's prop players");
}

void test_color() {
  // As the device page's chips (app.js propTone)
  RCHECK(prop_color(PropStatus::ON_TRACK) == kBetGood && prop_color(PropStatus::HIT) == kBetGood &&
             prop_color(PropStatus::WON) == kBetGood,
         "good");
  RCHECK(prop_color(PropStatus::OFF_TRACK) == kBetBad && prop_color(PropStatus::LOST) == kBetBad, "bad");
  RCHECK(prop_color(PropStatus::NOT_YET) == kBetEven && prop_color(PropStatus::PUSH) == kBetEven &&
             prop_color(PropStatus::VOID) == kBetEven,
         "even");
  RCHECK(prop_color(PropStatus::WAITING) == 0 && prop_color(PropStatus::PRE) == 0 &&
             prop_color(PropStatus::UNREAD) == 0 && prop_color(PropStatus::LIVE) == 0,
         "no color without a status word");
}

}  // namespace

int run_prop_tests(int &total_checks) {
  test_cut();
  test_football_box();
  test_other_boxes();
  test_parse_prop();
  test_text();
  test_status();
  test_grade();
  test_grade_box();
  test_box_timing();
  test_store();
  test_halves();
  test_sixteen_box();
  test_progress();
  test_soccer();
  test_color();
  total_checks += checks;
  return failures;
}
