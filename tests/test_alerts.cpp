// Big-play alerts (components/gameday/alerts.h): every alert type from two
// reads of a game, mostly real ESPN documents from fixtures/ with the score or
// the play moved on, and the splash words checked against the panel's lines.
// The play wording in the NFL cases is ESPN's, from its play-by-play of the
// 2026-11-01 games.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "alerts.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define ACHECK_EQ(a, b)                                                                       \
  do {                                                                                        \
    checks++;                                                                                 \
    const std::string va = (a), vb = (b);                                                     \
    if (va != vb) {                                                                           \
      failures++;                                                                             \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                         \
  } while (0)

#define ACHECK(cond)                                           \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                          \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

GameSnapshot event(const char *path, Sport sport, uint32_t team) {
  GameSnapshot s;
  ACHECK(parse_event_str(slurp(path), sport, team, s));
  return s;
}

// One game out of a day's scoreboard, read the way the panel reads a game on
// its own (parse_event, which keeps football's play type).
GameSnapshot from_scoreboard(const char *path, const char *id, uint32_t team) {
  JsonDocument doc;
  ACHECK(!deserializeJson(doc, slurp(path), DeserializationOption::NestingLimit(40)));
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (std::string(ev["id"] | "") != id)
      continue;
    std::string one;
    serializeJson(ev, one);
    GameSnapshot s;
    ACHECK(parse_event_str(one, Sport::FOOTBALL, team, s, id));
    return s;
  }
  ACHECK(false);
  return GameSnapshot{};
}

// What decide_splash puts on the panel, on one panel or two.
std::string splash(const GameSnapshot &prev, const GameSnapshot &cur, bool theirs = true, bool neutral = false,
                   uint32_t mask = kAlertsAll, bool wide = false) {
  return decide_splash(prev, cur, theirs, neutral, mask, wide).text;
}

uint32_t all_but(Alert a) { return kAlertsAll & ~alert_bit(a); }

// ---- football ---------------------------------------------------------------

const uint32_t kCincinnati = 2132, kBostonCollege = 103;

void test_interception() {
  // BC @ CIN, end of the 3rd: "Pass Interception Return", CIN's ball after it.
  const char *sb = "fixtures/scoreboard_ncaa_live.json";
  GameSnapshot cur = from_scoreboard(sb, "401856777", kCincinnati);
  ACHECK_EQ(cur.play_type, "Pass Interception Return");
  ACHECK_EQ(cur.team_abbr, "CIN");
  ACHECK(cur.possession == 1);
  cur.state = GameState::IN;  // ESPN's end of the quarter is still "in"
  GameSnapshot prev = cur;
  prev.last_play = "Shotgun #10 M.McKenzie pass complete short left to #4 R.Cronin for 7 yards";
  prev.play_type = "Pass Reception";
  prev.possession = 2;
  // One panel breaks the word after its hyphen; two have room for it.
  ACHECK_EQ(splash(prev, cur), "INTER-CEPTION!");
  ACHECK_EQ(splash(prev, cur, true, false, kAlertsAll, true), "INTERCEPTION!");
  ACHECK(decide_splash(prev, cur, true).color == 0x000000);  // CIN's color as ESPN sends it
  ACHECK_EQ(splash(prev, cur, true, false, all_but(Alert::INTERCEPTION)), "");
  // The same play read again, or with a replay review added, is not new.
  ACHECK_EQ(splash(cur, cur), "");
  GameSnapshot reviewed = cur;
  reviewed.last_play += " The Replay Official reviewed the runner was not down by contact ruling.";
  ACHECK_EQ(splash(cur, reviewed), "");
  // A read with no last play says nothing about the next one.
  GameSnapshot blank = prev;
  blank.last_play.clear();
  ACHECK_EQ(splash(blank, cur), "");

  // BC's panel: the other side's interception, on its own line when it won't fit.
  GameSnapshot bc = from_scoreboard(sb, "401856777", kBostonCollege);
  bc.state = GameState::IN;
  GameSnapshot bc_prev = bc;
  bc_prev.last_play = prev.last_play;
  bc_prev.possession = 1;
  ACHECK(bc.possession == 2);
  ACHECK_EQ(splash(bc_prev, bc), "CIN\nINTER-CEPTION");
  ACHECK_EQ(splash(bc_prev, bc, true, false, kAlertsAll, true), "CIN INTERCEPTION");
  ACHECK_EQ(splash(bc_prev, bc, false), "");
  // Live games: either side, by name.
  ACHECK_EQ(splash(prev, cur, true, true, kAlertsAll, true), "CIN INTERCEPTION");
  // ESPN left possession out after the play: the side that didn't have it before.
  GameSnapshot no_poss = cur;
  no_poss.possession = 0;
  ACHECK_EQ(splash(prev, no_poss), "INTER-CEPTION!");
  GameSnapshot neither = prev;
  neither.possession = 0;
  ACHECK_EQ(splash(neither, no_poss), "");

  // Without a play type, the words: NFL's capitals, and not a reversed one.
  GameSnapshot nfl = cur;
  nfl.play_type.clear();
  nfl.last_play =
      "(Shotgun) J.Burrow pass deep right intended for J.Chase INTERCEPTED by A.Hooker [S.Thomas] at TEN 9.";
  ACHECK_EQ(splash(prev, nfl), "INTER-CEPTION!");
  nfl.last_play = "G.Smith pass deep left intended for G.Wilson INTERCEPTED by J.Chinn at LV -6. The Replay Official "
                  "reviewed the pass, and the play was REVERSED. G.Smith pass incomplete deep left.";
  ACHECK_EQ(splash(prev, nfl), "");
  // A penalty's "Pass Interference" is not an interception.
  nfl.play_type = "Penalty";
  nfl.last_play = "PENALTY OSU Pass Interference (#6 C.Bodnar) 15 yards from TLS39 to OSU46, 1ST DOWN. NO PLAY";
  ACHECK_EQ(splash(prev, nfl), "");
  // A pick six scores first.
  GameSnapshot six = cur;
  six.play_type = "Interception Return Touchdown";
  six.team_score += 6;
  ACHECK_EQ(splash(prev, six), "TOUCHDOWN!");
  ACHECK_EQ(splash(prev, six, true, false, all_but(Alert::TOUCHDOWN)), "INTER-CEPTION!");
}

void test_fumble() {
  GameSnapshot prev = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401856777", kCincinnati);
  prev.state = GameState::IN;
  prev.possession = 2;
  auto play = [&](const char *type, const char *text, int poss) {
    GameSnapshot c = prev;
    c.play_type = type;
    c.last_play = text;
    c.possession = poss;
    return c;
  };
  // The other side recovered: a turnover, whatever ESPN calls it.
  ACHECK_EQ(splash(prev, play("Fumble Recovery (Opponent)",
                              "(Shotgun) C.Ward pass short right to W.Robinson to CIN 39 for -2 yards (D.Turner). "
                              "FUMBLES (D.Turner), touched at CIN 38, RECOVERED by CIN-J.Newton at CIN 32.",
                              1)),
            "FUMBLE RECOVERY!");
  ACHECK_EQ(splash(prev, play("Sack Opp Fumble Recovery",
                              "(Shotgun) C.Ward sacked at TEN 29 for -9 yards (B.Mafe). FUMBLES (B.Mafe), "
                              "RECOVERED by CIN-D.Lawrence at TEN 25.",
                              1)),
            "FUMBLE RECOVERY!");
  // BC recovering CIN's fumble: BC's on its own line, the phrase under it.
  GameSnapshot lost = play("Fumble Recovery (Opponent)",
                           "J.Fagnano FUMBLES (Aborted) at BUF 48, RECOVERED by BUF-D.Williams at BUF 47.", 2);
  prev.possession = 1;
  ACHECK_EQ(splash(prev, lost), "BC\nFUMBLE RECOVERY");
  ACHECK_EQ(splash(prev, lost, true, false, kAlertsAll, true), "BC\nFUMBLE RECOVERY");
  ACHECK_EQ(splash(prev, lost, false), "");
  ACHECK_EQ(splash(prev, lost, true, false, all_but(Alert::FUMBLE)), "");
  // The offense kept it: no alert.
  ACHECK_EQ(splash(prev, play("Fumble Recovery (Own)",
                              "(Shotgun) R.Dowdle right end to CLV 23 for 14 yards (Q.Williams). FUMBLES (Q.Williams), "
                              "recovered by PIT-T.Fautanu at CLV 19.",
                              1)),
            "");
  ACHECK_EQ(splash(prev, play("Rush", "B.Mayfield to ATL 17 for -2 yards. FUMBLES, and recovers at ATL 18.", 1)), "");
  // Without a play type: the NFL's capital RECOVERED is the other team's.
  ACHECK_EQ(splash(prev, play("", "FUMBLES (D.Overshown), RECOVERED by DAL-L.Overton at ARZ 47.", 2)),
            "BC\nFUMBLE RECOVERY");
  ACHECK_EQ(splash(prev, play("", "FUMBLES (T.Campbell), recovered by PIT-T.Fautanu at CLV 19.", 1)), "");
  ACHECK_EQ(splash(prev, play("", "FUMBLES (T.Eichenberg), RECOVERED by LV-T.Keenan at NYJ 31. The Replay Official "
                                  "reviewed the pass completion ruling, and the play was REVERSED.",
                              2)),
            "");
  // A college play with no type never guesses.
  ACHECK_EQ(splash(prev, play("", "#2 D.Bailey fumbled, forced by #94 L.McFadden-Pressley, recovered by ODU", 2)), "");
}

void test_football_points() {
  // FUR @ TENN, 13:03 4th: the try after a touchdown, which failed.
  GameSnapshot prev = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401856666", 2633);
  ACHECK_EQ(prev.play_type, "Two Point Rush");
  GameSnapshot two = prev;
  two.team_score += 2;
  ACHECK_EQ(splash(prev, two), "2-PT!");
  // ESPN's NFL words for a good try, on the touchdown's own play.
  GameSnapshot nfl = prev;
  nfl.play_type = "Passing Touchdown";
  nfl.last_play = "(Shotgun) C.Ward pass short right to W.Robinson for 15 yards, TOUCHDOWN. TWO-POINT CONVERSION "
                  "ATTEMPT. C.Ward pass to G.Helm is complete. ATTEMPT SUCCEEDS.";
  GameSnapshot nfl2 = nfl;
  nfl2.team_score += 8;
  ACHECK_EQ(splash(prev, nfl2), "TOUCHDOWN!");
  // The touchdown in the read before, the try in this one.
  GameSnapshot after_td = prev;
  after_td.play_type = "Rushing Touchdown";
  after_td.last_play = "(10:40) #13 J.Grier rush left for 28 yards gain to the MNE00 TOUCHDOWN, clock 10:33";
  GameSnapshot try_good = after_td;
  try_good.last_play = "(Two-Point Conversion good)";
  try_good.play_type = "Two Point Pass";
  try_good.opp_score += 2;
  ACHECK_EQ(splash(after_td, try_good), "FUR 2-PT");
  // A safety: two points with no touchdown before, or a play that says so.
  GameSnapshot safety = prev;
  safety.last_play = "(Shotgun) C.Ward sacked in End Zone for -9 yards, SAFETY (B.Mafe).";
  safety.play_type = "Safety";
  safety.opp_score += 2;
  ACHECK_EQ(splash(after_td, safety), "FUR SAFETY");
  GameSnapshot quiet = prev;
  quiet.last_play = "(Shotgun) J.Smith rush middle for 3 yards";
  quiet.play_type = "Rush";
  GameSnapshot plain = quiet;
  plain.team_score += 2;
  ACHECK_EQ(splash(quiet, plain), "SAFETY!");
  ACHECK_EQ(splash(quiet, plain, true, false, all_but(Alert::SAFETY)), "");
  ACHECK_EQ(splash(prev, two, true, false, all_but(Alert::TWO_POINT)), "");
  // Each score's own switch, and SCORE goes with the touchdown's.
  GameSnapshot fg = quiet, xp = quiet, odd = quiet, td = quiet;
  fg.team_score += 3;
  xp.team_score += 1;
  odd.team_score += 5;
  td.team_score += 7;
  ACHECK_EQ(splash(quiet, fg), "FIELD GOAL!");
  ACHECK_EQ(splash(quiet, fg, true, false, all_but(Alert::FIELD_GOAL)), "");
  ACHECK_EQ(splash(quiet, xp), "EXTRA POINT!");
  ACHECK_EQ(splash(quiet, xp, true, false, all_but(Alert::EXTRA_POINT)), "");
  ACHECK_EQ(splash(quiet, odd), "SCORE!");
  ACHECK_EQ(splash(quiet, odd, true, false, all_but(Alert::TOUCHDOWN)), "");
  ACHECK_EQ(splash(quiet, td, true, false, all_but(Alert::TOUCHDOWN)), "");
  ACHECK_EQ(splash(quiet, td, true, false, 0), "");
  // Everything off still leaves the win.
  GameSnapshot fin = quiet, was = quiet;
  fin.state = GameState::POST;
  fin.completed = true;
  fin.team_score = was.team_score = prev.opp_score + 1;
  ACHECK_EQ(splash(was, fin, true, false, 0), "TENN WINS!");
}

// ---- baseball -----------------------------------------------------------------

const uint32_t kYankees = 10, kRays = 30, kBraves = 15, kDodgers = 19;

void test_home_runs() {
  // TB @ NYY, top 6th: Mesa Jr.'s two-run homer, the reads either side of it.
  GameSnapshot before = event("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, kRays);
  GameSnapshot after = event("fixtures/event_mlb_hr.json", Sport::BASEBALL, kRays);
  ACHECK_EQ(splash(before, after), "HOME RUN!");
  ACHECK_EQ(splash(before, after, true, false, all_but(Alert::HOME_RUN)), "2 RUNS!");
  ACHECK_EQ(splash(before, after, true, false, all_but(Alert::HOME_RUN) & ~alert_bit(Alert::RUN)), "");
  GameSnapshot nyy_before = event("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, kYankees);
  GameSnapshot nyy_after = event("fixtures/event_mlb_hr.json", Sport::BASEBALL, kYankees);
  ACHECK_EQ(splash(nyy_before, nyy_after), "TB HOME RUN");
  ACHECK_EQ(splash(nyy_before, nyy_after, false), "");
  // Four runs on a home run is a grand slam, and so is one ESPN calls that.
  GameSnapshot slam = after;
  slam.team_score += 2;
  ACHECK_EQ(splash(before, slam), "GRAND SLAM!");
  ACHECK_EQ(splash(before, slam, true, false, all_but(Alert::GRAND_SLAM)), "HOME RUN!");
  GameSnapshot nyy_slam = nyy_after;
  nyy_slam.opp_score += 2;
  ACHECK_EQ(splash(nyy_before, nyy_slam), "TB\nGRAND SLAM");
  ACHECK_EQ(splash(nyy_before, nyy_slam, true, false, kAlertsAll, true), "TB GRAND SLAM");
  GameSnapshot called = after;
  called.last_play = "Mesa Jr. hit a grand slam to right center (401 feet).";
  called.team_score += 3;  // a missed read: five in all
  ACHECK_EQ(splash(before, called), "GRAND SLAM!");
  // Four runs without a home run are runs.
  GameSnapshot four = before;
  four.team_score += 4;
  ACHECK_EQ(splash(before, four), "4 RUNS!");
}

void test_walk_off() {
  // LAD @ ATL, bottom 6th, tied 1-1; moved on to the bottom of the 9th.
  GameSnapshot prev = event("fixtures/event_mlb_bottom.json", Sport::BASEBALL, kBraves);
  ACHECK(prev.team_home && prev.mlb.half == Half::BOTTOM && prev.team_score == prev.opp_score);
  prev.period = 9;
  prev.short_detail = "Bot 9th";
  GameSnapshot walk = prev;
  walk.team_score += 1;
  ACHECK_EQ(splash(prev, walk), "WALK-OFF!");
  ACHECK_EQ(splash(prev, walk, true, false, all_but(Alert::WALK_OFF)), "RUN!");
  // ESPN has the final already: the walk-off beats the win, which it is.
  GameSnapshot fin = walk;
  fin.state = GameState::POST;
  fin.completed = true;
  fin.mlb.half = Half::NONE;
  ACHECK_EQ(splash(prev, fin), "WALK-OFF!");
  ACHECK(decide_splash(prev, fin, true).color == 0x0c2340);
  ACHECK_EQ(splash(prev, fin, true, false, all_but(Alert::WALK_OFF)), "ATL WINS!");
  // A walk-off read once is not one again at the final.
  ACHECK_EQ(splash(walk, fin), "ATL WINS!");
  // From the visitors' side, and in extra innings.
  GameSnapshot lad_prev = event("fixtures/event_mlb_bottom.json", Sport::BASEBALL, kDodgers);
  lad_prev.period = 11;
  GameSnapshot lad_fin = lad_prev;
  lad_fin.opp_score += 2;
  lad_fin.state = GameState::POST;
  lad_fin.completed = true;
  ACHECK_EQ(splash(lad_prev, lad_fin), "ATL\nWALK-OFF");
  ACHECK_EQ(splash(lad_prev, lad_fin, true, false, kAlertsAll, true), "ATL WALK-OFF");
  ACHECK_EQ(splash(lad_prev, lad_fin, false), "");
  // Not a walk-off: the 8th, the visitors going ahead, or the home side already ahead.
  GameSnapshot eighth = prev, eighth_run = prev;
  eighth.period = eighth_run.period = 8;
  eighth_run.team_score += 1;
  ACHECK_EQ(splash(eighth, eighth_run), "RUN!");
  GameSnapshot top = prev;
  top.mlb.half = Half::TOP;
  GameSnapshot top_run = top;
  top_run.opp_score += 1;
  ACHECK_EQ(splash(top, top_run), "LAD SCORES");
  GameSnapshot ahead = prev;
  ahead.team_score += 1;
  GameSnapshot more = ahead;
  more.team_score += 1;
  ACHECK_EQ(splash(ahead, more), "RUN!");
}

// ---- hockey -------------------------------------------------------------------

void test_hockey_goal() {
  // COL @ WPG, 3:21 1st, 0-0.
  GameSnapshot prev = event("fixtures/event_nhl_live.json", Sport::HOCKEY, 28);
  GameSnapshot goal = prev;
  goal.team_score += 1;
  // ESPN's words for a power play goal (FLA @ WSH, 2026-11-07) say nothing of
  // the power play, so it is a goal like any other.
  goal.last_play = "Brady Tkachuk Goal (10) Snap Shot, assists: Matthew Tkachuk (20), Seth Jones (17)";
  ACHECK_EQ(splash(prev, goal), "GOAL!");
  ACHECK_EQ(splash(prev, goal, true, true), "WPG GOAL");
  ACHECK_EQ(splash(prev, goal, true, false, all_but(Alert::HOCKEY_GOAL)), "");
  ACHECK_EQ(splash(prev, goal, true, false, all_but(Alert::SOCCER_GOAL)), "GOAL!");
  GameSnapshot theirs = prev;
  theirs.opp_score += 1;
  ACHECK_EQ(splash(prev, theirs), "COL GOAL");
  ACHECK_EQ(splash(prev, theirs, false), "");
}

// ---- soccer -------------------------------------------------------------------

void test_soccer() {
  // ARS @ SUN: ARS 1-0 from 58', SUN's red card at 90'+6', then ARS's penalty
  // at 90'+7'. The final document, read back to before each.
  GameSnapshot end = event("fixtures/event_epl_final.json", Sport::SOCCER, 359);
  ACHECK(end.soc.goals.size() == 2 && end.soc.goals.back().penalty && end.soc.opp_reds == 1);
  end.state = GameState::IN;
  end.completed = false;
  GameSnapshot carded = end;
  carded.soc.goals.pop_back();
  carded.team_score -= 1;
  GameSnapshot before = carded;
  before.soc.opp_reds = 0;
  ACHECK_EQ(splash(before, carded), "SUN\nRED CARD");
  ACHECK_EQ(splash(before, carded, false), "SUN\nRED CARD");  // a card is no score
  ACHECK_EQ(splash(before, carded, true, false, kAlertsAll, true), "SUN RED CARD");
  ACHECK(decide_splash(before, carded, true).color == 0xCC0000);
  ACHECK_EQ(splash(before, carded, true, false, all_but(Alert::RED_CARD)), "");
  ACHECK_EQ(splash(carded, end), "PENALTY GOAL!");
  ACHECK_EQ(splash(carded, end, true, false, all_but(Alert::PENALTY_GOAL)), "GOAL!");
  ACHECK_EQ(splash(carded, end, true, false, all_but(Alert::PENALTY_GOAL) & ~alert_bit(Alert::SOCCER_GOAL)), "");
  ACHECK_EQ(splash(carded, end, true, true), "ARS\nPENALTY GOAL");
  ACHECK_EQ(splash(carded, end, true, true, kAlertsAll, true), "ARS PENALTY GOAL");
  // SUN's panel: our own red card names us too, never "RED CARD!".
  GameSnapshot sun = event("fixtures/event_epl_final.json", Sport::SOCCER, 366);
  sun.state = GameState::IN;
  GameSnapshot sun_before = sun;
  sun_before.soc.team_reds = 0;
  sun_before.soc.goals.pop_back();
  sun_before.opp_score -= 1;
  GameSnapshot sun_carded = sun_before;
  sun_carded.soc.team_reds = 1;
  ACHECK_EQ(splash(sun_before, sun_carded), "SUN\nRED CARD");
  ACHECK_EQ(splash(sun_carded, sun), "ARS\nPENALTY GOAL");
  ACHECK_EQ(splash(sun_carded, sun, false), "");
  // MIR @ BRA: MIR's red at 28', then goals that aren't penalties.
  GameSnapshot bra = event("fixtures/event_soccer_second_half.json", Sport::SOCCER, 6079);
  ACHECK(bra.soc.opp_reds == 1);
  GameSnapshot bra_before = bra;
  bra_before.soc.goals.pop_back();
  bra_before.opp_score -= 1;
  ACHECK_EQ(splash(bra_before, bra), "MIR GOAL");
}

// ---- basketball ---------------------------------------------------------------

const uint32_t kPacers = 11, kWolves = 16, kBucks = 15, kThunder = 25, kDream = 20, kLiberty = 9;

GameSnapshot hoops(const char *path, uint32_t team) { return event(path, Sport::BASKETBALL, team); }

void test_lead_change() {
  // MIN @ IND, 8:27 3rd, 74-73: moved on to the last two minutes of the 4th.
  GameSnapshot prev = hoops("fixtures/event_nba_live.json", kPacers);
  prev.period = 4;
  prev.display_clock = "1:58";
  prev.team_score = 100;
  prev.opp_score = 101;
  GameSnapshot ahead = prev;
  ahead.display_clock = "1:41";
  ahead.team_score = 103;
  ACHECK_EQ(splash(prev, ahead), "LEAD CHANGE!");
  ACHECK_EQ(splash(prev, ahead, true, false, all_but(Alert::LEAD_CHANGE)), "");
  // From a tie, too; not one that only ties or adds to a lead.
  GameSnapshot tied = prev;
  tied.team_score = 101;
  GameSnapshot from_tie = tied;
  from_tie.team_score = 103;
  ACHECK_EQ(splash(tied, from_tie), "LEAD CHANGE!");
  ACHECK_EQ(splash(prev, tied), "");
  GameSnapshot more = ahead;
  more.team_score += 2;
  ACHECK_EQ(splash(ahead, more), "");
  // Earlier in the 4th, or in the 3rd, a lead change is just a basket.
  GameSnapshot early = prev, early_ahead = ahead;
  early.display_clock = "2:30";
  early_ahead.display_clock = "2:01";
  ACHECK_EQ(splash(early, early_ahead), "");
  GameSnapshot third = prev, third_ahead = ahead;
  third.period = third_ahead.period = 3;
  ACHECK_EQ(splash(third, third_ahead), "");
  // Overtime: any time.
  GameSnapshot ot = prev, ot_ahead = ahead;
  ot.period = ot_ahead.period = 5;
  ot.display_clock = "4:40";
  ot_ahead.display_clock = "4:20";
  ACHECK_EQ(splash(ot, ot_ahead), "LEAD CHANGE!");
  // The other side, by name.
  GameSnapshot min_ahead = prev;
  min_ahead.display_clock = "1:30";
  min_ahead.opp_score += 3;
  GameSnapshot min_prev = prev;
  min_prev.opp_score = 99;
  ACHECK_EQ(splash(min_prev, min_ahead), "MIN\nLEAD CHANGE");
  ACHECK_EQ(splash(min_prev, min_ahead, true, false, kAlertsAll, true), "MIN LEAD CHANGE");
  ACHECK_EQ(splash(min_prev, min_ahead, false), "");
  // College plays halves: its last two minutes are the 2nd half's.
  GameSnapshot college = prev, college_ahead = ahead;
  college.nba.regulation = college_ahead.nba.regulation = 2;
  college.period = college_ahead.period = 2;
  ACHECK_EQ(splash(college, college_ahead), "LEAD CHANGE!");
}

void test_buzzer_beater() {
  // MIL @ OKC, 12.3 left in the 1st, OKC up 44-35: MIL ties it as the quarter ends.
  GameSnapshot prev = hoops("fixtures/event_nba_clock.json", kBucks);
  ACHECK_EQ(prev.display_clock, "12.3");
  prev.display_clock = "2.6";
  prev.team_score = 41;
  GameSnapshot tie = prev;
  tie.display_clock = "0.0";
  tie.nba.phase = Phase::END_PERIOD;
  tie.team_score = 44;
  ACHECK_EQ(splash(prev, tie), "BUZZER BEATER!");
  ACHECK_EQ(splash(prev, tie, true, false, kAlertsAll, true), "BUZZER BEATER!");
  ACHECK_EQ(splash(prev, tie, true, false, all_but(Alert::BUZZER_BEATER)), "");
  // The clock stopped on the basket with under a second left.
  GameSnapshot stopped = prev;
  stopped.display_clock = "0.6";
  stopped.team_score = 45;
  ACHECK_EQ(splash(prev, stopped), "BUZZER BEATER!");
  // Not one: the last read was too long before the end, free throws with
  // the clock already out, a basket that only closes the gap or adds to a lead.
  GameSnapshot long_ago = prev;
  long_ago.display_clock = "8.0";
  ACHECK_EQ(splash(long_ago, tie), "");
  GameSnapshot at_zero = tie;
  at_zero.team_score = 42;
  GameSnapshot free_throws = at_zero;
  free_throws.team_score = 44;
  ACHECK_EQ(splash(at_zero, free_throws), "");
  GameSnapshot short_of = tie;
  short_of.team_score = 43;
  ACHECK_EQ(splash(prev, short_of), "");
  GameSnapshot okc = hoops("fixtures/event_nba_clock.json", kThunder);
  okc.display_clock = "2.6";
  GameSnapshot okc_more = okc;
  okc_more.display_clock = "0.0";
  okc_more.team_score += 3;
  ACHECK_EQ(splash(okc, okc_more), "");
  // OKC's panel: MIL's, by name.
  GameSnapshot okc_prev = okc;
  okc_prev.opp_score = 41;
  GameSnapshot okc_tie = okc_prev;
  okc_tie.display_clock = "0.0";
  okc_tie.opp_score = 44;
  ACHECK_EQ(splash(okc_prev, okc_tie), "MIL\nBUZZER BEATER");
  ACHECK_EQ(splash(okc_prev, okc_tie, true, false, kAlertsAll, true), "MIL\nBUZZER BEATER");
  ACHECK_EQ(splash(okc_prev, okc_tie, false), "");
  // A game winner at the final beats the win splash; with it off, the win.
  GameSnapshot fourth = prev;
  fourth.period = 4;
  GameSnapshot winner = fourth;
  winner.display_clock = "0.0";
  winner.team_score = 46;
  winner.state = GameState::POST;
  winner.completed = true;
  ACHECK_EQ(splash(fourth, winner), "BUZZER BEATER!");
  ACHECK_EQ(splash(fourth, winner, true, false, all_but(Alert::BUZZER_BEATER)), "MIL WINS!");
}

void test_overtime_and_final() {
  // NY @ ATL, 20.8 left in overtime; read back to the end of the 4th.
  GameSnapshot ot = hoops("fixtures/event_wnba_ot.json", kDream);
  ACHECK(ot.period == 5 && ot.nba.regulation == 4);
  GameSnapshot end4 = ot;
  end4.period = 4;
  end4.display_clock = "0.0";
  end4.nba.phase = Phase::END_PERIOD;
  end4.team_score = end4.opp_score = 90;
  GameSnapshot ot_start = end4;
  ot_start.period = 5;
  ot_start.display_clock = "5:00";
  ot_start.nba.phase = Phase::PLAY;
  ACHECK_EQ(splash(end4, ot_start), "OVERTIME");
  ACHECK_EQ(splash(end4, ot_start, false, true), "OVERTIME");
  ACHECK(decide_splash(end4, ot_start, true).color == 0xe31837);
  ACHECK_EQ(splash(end4, ot_start, true, false, all_but(Alert::OVERTIME)), "");
  // ATL goes ahead in it, but it is overtime only once.
  ACHECK_EQ(splash(ot_start, ot), "LEAD CHANGE!");
  ACHECK_EQ(splash(ot_start, ot, true, false, all_but(Alert::LEAD_CHANGE)), "");
  GameSnapshot ot2 = ot;
  ot2.period = 6;
  ACHECK_EQ(splash(ot, ot2), "OVERTIME");
  // The recorded final: ATL won, NY lost.
  ACHECK_EQ(splash(ot, hoops("fixtures/event_wnba_final.json", kDream)), "ATL WINS!");
  ACHECK_EQ(splash(hoops("fixtures/event_wnba_ot.json", kLiberty), hoops("fixtures/event_wnba_final.json", kLiberty)),
            "FINAL");
  ACHECK_EQ(splash(hoops("fixtures/event_wnba_ot.json", kLiberty), hoops("fixtures/event_wnba_final.json", kLiberty),
                   true, false, all_but(Alert::FINAL)),
            "");
  // Live games: the win says it all.
  ACHECK_EQ(splash(hoops("fixtures/event_wnba_ot.json", kLiberty), hoops("fixtures/event_wnba_final.json", kLiberty),
                   true, true),
            "ATL WINS!");
  // A game ESPN stopped without finishing is no final.
  GameSnapshot halted = hoops("fixtures/event_nba_live.json", kWolves);
  GameSnapshot stopped = halted;
  stopped.state = GameState::POST;
  stopped.completed = false;
  ACHECK_EQ(splash(halted, stopped), "");
}

// ---- detection on its own, favorites and the words ----------------------------

void test_detect() {
  GameSnapshot prev = hoops("fixtures/event_nba_live.json", kPacers);
  GameSnapshot next = prev;
  next.event_id = "1";
  next.team_score += 30;
  ACHECK(detect_big_play(prev, next, kAlertsAll, true).type == Alert::NONE);  // another game
  ACHECK(detect_big_play(GameSnapshot{}, prev, kAlertsAll, true).type == Alert::NONE);
  GameSnapshot hr = event("fixtures/event_mlb_hr.json", Sport::BASEBALL, kYankees);
  BigPlay p = detect_big_play(event("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, kYankees), hr, kAlertsAll,
                              true);
  ACHECK(p.type == Alert::HOME_RUN && p.side == AlertSide::OPP && p.points == 2);
  p = detect_big_play(event("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, kYankees), hr, kAlertsAll, false);
  ACHECK(p.type == Alert::NONE);
  // The bits: one per switch, SCORE sharing the touchdown's.
  ACHECK(alert_bit(Alert::TOUCHDOWN) == 1 && alert_bit(Alert::FINAL) == 1u << 18);
  ACHECK(alert_bit(Alert::SCORE) == alert_bit(Alert::TOUCHDOWN) && alert_bit(Alert::NONE) == 0);
  ACHECK(kAlertsAll == 0x7FFFF);
  // ESPN's basketball clocks, and documents that send something else.
  using espn::detail::clock_tenths;
  ACHECK(clock_tenths("8:27") == 5070 && clock_tenths("12.3") == 123 && clock_tenths("0.0") == 0);
  ACHECK(clock_tenths("1:00") == 600 && clock_tenths("5") == 50);
  for (const char *bad : {"", ".5", "-1", ":30", "1:", "1:60", "99999999999:00", "1:99999999999", "99999999999", "x"})
    ACHECK(clock_tenths(bad) == -1);
}

void test_favorites_only() {
  const std::pair<uint8_t, uint32_t> teams[] = {{(uint8_t) League::NFL, 6}, {(uint8_t) League::NBA, 11}, {0, 0}};
  ACHECK(game_has_team((uint8_t) League::NFL, 21, 6, teams, 3));
  ACHECK(game_has_team((uint8_t) League::NBA, 11, 16, teams, 3));
  ACHECK(!game_has_team((uint8_t) League::NBA, 6, 16, teams, 3));  // the same id in another league
  ACHECK(!game_has_team((uint8_t) League::NFL, 21, 34, teams, 3));
  ACHECK(!game_has_team((uint8_t) League::NFL, 0, 0, teams, 3));  // an empty slot is no team
  ACHECK(!game_has_team((uint8_t) League::NFL, 21, 6, teams, 0));
}

// How LVGL lays a splash out: a line break at "\n", else at a space or after
// a hyphen once the next word won't fit. A word longer than a line would be
// cut in the middle.
bool fits(const std::string &text, size_t width, size_t max_lines) {
  size_t lines = 0;
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    std::string seg = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    start = nl == std::string::npos ? text.size() + 1 : nl + 1;
    std::vector<std::string> words;  // each with the hyphen it ends with
    std::string w;
    for (char c : seg) {
      if (c == ' ') {
        if (!w.empty())
          words.push_back(w);
        w.clear();
      } else {
        w += c;
        if (c == '-') {
          words.push_back(w);
          w.clear();
        }
      }
    }
    if (!w.empty())
      words.push_back(w);
    size_t len = 0;
    lines++;
    for (size_t i = 0; i < words.size(); i++) {
      if (words[i].size() > width)
        return false;
      const bool joined = i > 0 && words[i - 1].back() == '-';
      const size_t need = len == 0 ? words[i].size() : len + (joined ? 0 : 1) + words[i].size();
      if (need > width) {
        lines++;
        len = words[i].size();
      } else {
        len = need;
      }
    }
  }
  return lines <= max_lines;
}

void test_words_fit() {
  ACHECK(fits("DAL TOUCHDOWN", 10, 2) && !fits("DAL TOUCHDOWN", 10, 1));
  ACHECK(fits("INTER-CEPTION!", 10, 2) && !fits("INTERCEPTION!", 10, 3));
  ACHECK(!fits("A B C D", 1, 3));
  GameSnapshot g;
  g.team_color = "002a5c";
  g.opp_color = "004c54";
  // Every type, side and abbreviation length (ESPN's run from 2 to 5
  // letters), on one panel (10 characters, 3 lines of spleen_12 from y 18)
  // and two (16 characters, 3 lines of spleen_16 from y 16).
  for (int t = 1; t <= (int) Alert::SCORE; t++)
    for (AlertSide side : {AlertSide::TEAM, AlertSide::OPP, AlertSide::GAME})
      for (const char *abbr : {"NY", "DAL", "UTAH", "UMASS"})
        for (bool neutral : {false, true})
          for (bool wide : {false, true})
            for (int points : {1, 4, 12}) {
              g.team_abbr = g.opp_abbr = abbr;
              BigPlay p{(Alert) t, side, points};
              const std::string text = big_play_splash(p, g, neutral, wide).text;
              const bool ok = !text.empty() && fits(text, wide ? 16 : 10, 3);
              ACHECK(ok);
              if (!ok)
                printf("  type %d side %d abbr %s wide %d: \"%s\"\n", t, (int) side, abbr, (int) wide, text.c_str());
            }
  ACHECK(big_play_splash(BigPlay{}, g, false, false).text.empty());
}

}  // namespace

int run_alert_tests(int &total_checks) {
  test_interception();
  test_fumble();
  test_football_points();
  test_home_runs();
  test_walk_off();
  test_hockey_goal();
  test_soccer();
  test_lead_change();
  test_buzzer_beater();
  test_overtime_and_final();
  test_detect();
  test_favorites_only();
  test_words_fit();
  total_checks += checks;
  return failures;
}
