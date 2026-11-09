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
#include "board_layout.h"
#include "panel_text.h"
#include "splash_layout.h"

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
  ACHECK_EQ(cur.play_id, "401856777515");
  ACHECK_EQ(cur.team_abbr, "CIN");
  ACHECK(cur.possession == 1);
  ACHECK(cur.play_side == 1);  // the play ended with CIN, which started it as BC's ball
  cur.state = GameState::IN;  // ESPN's end of the quarter is still "in"
  GameSnapshot prev = cur;
  prev.last_play = "Shotgun #10 M.McKenzie pass complete short left to #4 R.Cronin for 7 yards";
  prev.play_type = "Pass Reception";
  prev.play_id = "401856777514";
  prev.possession = 2;
  prev.play_side = 2;
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
  // ESPN fixing the play's words (the return's yards) keeps its id: not new.
  GameSnapshot corrected = cur;
  const size_t yards = corrected.last_play.find("return 24 yard");
  ACHECK(yards != std::string::npos);
  corrected.last_play.replace(yards, 14, "return 26 yard");
  ACHECK_EQ(splash(cur, corrected), "");
  // Without ids, the words decide: an append is the same play, a change a new one.
  GameSnapshot no_id = cur, no_id_fixed = corrected;
  no_id.play_id.clear();
  no_id_fixed.play_id.clear();
  ACHECK_EQ(splash(no_id, reviewed), "");
  ACHECK_EQ(splash(no_id, no_id_fixed), "INTER-CEPTION!");  // what the id saves
  GameSnapshot no_id_prev = prev;
  no_id_prev.play_id.clear();
  ACHECK_EQ(splash(no_id_prev, no_id), "INTER-CEPTION!");
  // A read with no last play says nothing about the next one.
  GameSnapshot blank = prev;
  blank.last_play.clear();
  blank.play_id.clear();
  ACHECK_EQ(splash(blank, cur), "");

  // BC's panel: the other side's interception, on its own line when it won't fit.
  GameSnapshot bc = from_scoreboard(sb, "401856777", kBostonCollege);
  bc.state = GameState::IN;
  GameSnapshot bc_prev = bc;
  bc_prev.last_play = prev.last_play;
  bc_prev.play_id = prev.play_id;
  bc_prev.possession = 1;
  ACHECK(bc.possession == 2 && bc.play_side == 2);
  ACHECK_EQ(splash(bc_prev, bc), "CIN\nINTER-CEPTION");
  ACHECK_EQ(splash(bc_prev, bc, true, false, kAlertsAll, true), "CIN INTERCEPTION");
  ACHECK_EQ(splash(bc_prev, bc, false), "");
  // Live games: either side, by name.
  ACHECK_EQ(splash(prev, cur, true, true, kAlertsAll, true), "CIN INTERCEPTION");
  // ESPN left possession out after the play: the play's own end team says
  // who has the ball, even when the read before had it wrong.
  GameSnapshot no_poss = cur;
  no_poss.possession = 0;
  ACHECK_EQ(splash(prev, no_poss), "INTER-CEPTION!");
  GameSnapshot stale = prev;
  stale.possession = 1;
  ACHECK_EQ(splash(stale, no_poss), "INTER-CEPTION!");
  // Neither: the side that didn't have it before; nothing known, nothing.
  GameSnapshot bare = no_poss;
  bare.play_side = 0;
  ACHECK_EQ(splash(prev, bare), "INTER-CEPTION!");
  GameSnapshot neither = prev;
  neither.possession = 0;
  ACHECK_EQ(splash(neither, bare), "");
  // A punt read with possession left out still names the team with the ball.
  GameSnapshot punt = from_scoreboard(sb, "401862694", 2643);
  ACHECK(punt.possession == 0 && punt.play_side == 1 && punt.play_type == "Punt");

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
  prev.possession = prev.play_side = 2;
  // A new play (its own id) that ended with side poss holding the ball.
  int plays = 0;
  auto play = [&](const char *type, const char *text, int poss) {
    GameSnapshot c = prev;
    c.play_type = type;
    c.last_play = text;
    c.possession = c.play_side = poss;
    c.play_id = "40185677760" + std::to_string(++plays);
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

  // The offense falling on its own fumble in the end zone is its touchdown,
  // never a fumble recovery, whichever switches are off. ESPN leaves
  // possession out after a score; the play's end team is the offense's.
  // BC's panel, CIN's ball: CIN scores.
  GameSnapshot bc = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401856777", kBostonCollege);
  bc.state = GameState::IN;
  bc.possession = bc.play_side = 2;
  GameSnapshot own_td = bc;
  own_td.play_type = "Fumble Recovery (Own) Touchdown";
  own_td.last_play = "C.Ward rush up the middle for 3 yards, FUMBLES, recovered by CIN-T.Jones in the End Zone, TOUCHDOWN.";
  own_td.play_id = "401856777601";
  own_td.possession = 0;
  own_td.opp_score += 6;
  ACHECK_EQ(splash(bc, own_td), "CIN TOUCHDOWN");
  ACHECK_EQ(splash(bc, own_td, false), "");
  ACHECK_EQ(splash(bc, own_td, true, true, kAlertsAll, true), "CIN TOUCHDOWN");
  ACHECK_EQ(splash(bc, own_td, true, true, all_but(Alert::TOUCHDOWN)), "");
  ACHECK_EQ(splash(bc, own_td, true, false, all_but(Alert::TOUCHDOWN)), "");
  // A punt the receiving team muffs and the kicking team recovers stays out.
  GameSnapshot muff = own_td;
  muff.play_type = "Punt Team Fumble Recovery";
  muff.opp_score = bc.opp_score;
  ACHECK_EQ(splash(bc, muff), "");
  // The defense taking it back for a touchdown is a lost fumble: with
  // TOUCHDOWN off, the side the play ended with, not the one with it before.
  GameSnapshot ret = own_td;
  ret.play_type = "Fumble Return Touchdown";
  ret.opp_score = bc.opp_score;
  ret.team_score += 6;
  ret.play_side = 1;
  ACHECK_EQ(splash(bc, ret), "TOUCHDOWN!");
  ACHECK_EQ(splash(bc, ret, false, false, all_but(Alert::TOUCHDOWN)), "FUMBLE RECOVERY!");
  ret.play_type = "Fumble Recovery (Opponent) Touchdown";
  ACHECK_EQ(splash(bc, ret, false, false, all_but(Alert::TOUCHDOWN)), "FUMBLE RECOVERY!");
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
  // The real touchdown read (APP @ ME): ESPN sent no possession and no down
  // with it, and the try's read had no situation at all.
  GameSnapshot app_td = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401866627", 2026);
  ACHECK(app_td.play_type == "Rushing Touchdown" && app_td.possession == 0 && app_td.down_distance.empty());
  GameSnapshot app_try = app_td;
  app_try.last_play.clear();
  app_try.play_type.clear();
  app_try.play_id.clear();
  app_try.team_score += 2;
  ACHECK_EQ(splash(app_td, app_try), "2-PT!");
  // Neither read says anything (the situation left out across the review
  // and the try): a conversion, much the more common two points.
  GameSnapshot silent = app_try;
  silent.team_score -= 2;
  ACHECK_EQ(splash(silent, app_try), "2-PT!");
  // A safety: a play that says so, whatever came before.
  GameSnapshot safety = prev;
  safety.last_play = "(Shotgun) C.Ward sacked in End Zone for -9 yards, SAFETY (B.Mafe).";
  safety.play_type = "Safety";
  safety.opp_score += 2;
  ACHECK_EQ(splash(after_td, safety), "FUR SAFETY");
  // Or two points straight from the middle of a drive: the read before had
  // a team with the ball and a down, which the try after a touchdown never has.
  GameSnapshot quiet = prev;
  quiet.last_play = "(Shotgun) J.Smith rush middle for 3 yards";
  quiet.play_type = "Rush";
  quiet.possession = 2;
  quiet.down_distance = "3rd & 8 at FUR 4";
  GameSnapshot plain = quiet;
  plain.team_score += 2;
  plain.possession = 0;
  plain.down_distance.clear();
  ACHECK_EQ(splash(quiet, plain), "SAFETY!");
  GameSnapshot no_down = quiet;
  no_down.down_distance.clear();
  ACHECK_EQ(splash(no_down, plain), "2-PT!");
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
  // A home run ESPN says scored four is a grand slam, and so is one it calls that.
  ACHECK(after.play_points == 2);  // the homer's own runs, from ESPN's play
  GameSnapshot slam = after;
  slam.team_score += 2;
  slam.play_points = 4;
  ACHECK_EQ(splash(before, slam), "GRAND SLAM!");
  ACHECK_EQ(splash(before, slam, true, false, all_but(Alert::GRAND_SLAM)), "HOME RUN!");
  // A two-run homer with two runs from an earlier play in the same update is no slam.
  GameSnapshot two_plays = after;
  two_plays.team_score += 2;
  ACHECK_EQ(splash(before, two_plays), "HOME RUN!");
  GameSnapshot nyy_slam = nyy_after;
  nyy_slam.opp_score += 2;
  nyy_slam.play_points = 4;
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
  // Reads missed from the bottom of the 8th to the final: the lead may have
  // come in the 8th, with no bottom of the 9th played, so only the win.
  GameSnapshot bot8 = event("fixtures/event_mlb_bottom.json", Sport::BASEBALL, kBraves);
  bot8.period = 8;
  bot8.short_detail = "Bot 8th";
  GameSnapshot fin9 = bot8;
  fin9.period = 9;
  fin9.team_score += 2;
  fin9.state = GameState::POST;
  fin9.completed = true;
  fin9.mlb.half = Half::NONE;
  ACHECK_EQ(splash(bot8, fin9), "ATL WINS!");
  GameSnapshot lad_bot8 = event("fixtures/event_mlb_bottom.json", Sport::BASEBALL, kDodgers);
  lad_bot8.period = 8;
  GameSnapshot lad_fin9 = lad_bot8;
  lad_fin9.period = 9;
  lad_fin9.opp_score += 2;
  lad_fin9.state = GameState::POST;
  lad_fin9.completed = true;
  ACHECK_EQ(splash(lad_bot8, lad_fin9), "");
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
  // Not one that only ties, breaks a tie or adds to a lead: IND up 100-98,
  // MIN ties it, IND ahead again never lost the lead to MIN.
  GameSnapshot tied = prev;
  tied.team_score = 101;
  GameSnapshot from_tie = tied;
  from_tie.team_score = 103;
  ACHECK_EQ(splash(tied, from_tie), "");
  ACHECK_EQ(splash(prev, tied), "");
  GameSnapshot up = prev, level = prev, again = prev;
  up.display_clock = "1:40";
  up.team_score = 100;
  up.opp_score = 98;
  level.display_clock = "1:25";
  level.team_score = level.opp_score = 100;
  again.display_clock = "1:12";
  again.team_score = 102;
  again.opp_score = 100;
  ACHECK_EQ(splash(up, level), "");
  ACHECK_EQ(splash(level, again), "");
  ACHECK_EQ(splash(level, again, true, true), "");
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
  prev.display_clock = "0.8";
  prev.team_score = 41;
  GameSnapshot tie = prev;
  tie.display_clock = "0.0";
  tie.nba.phase = Phase::END_PERIOD;
  tie.team_score = 44;
  ACHECK_EQ(splash(prev, tie), "BUZZER BEATER!");
  ACHECK_EQ(splash(prev, tie, true, false, kAlertsAll, true), "BUZZER BEATER!");
  ACHECK_EQ(splash(prev, tie, true, false, all_but(Alert::BUZZER_BEATER)), "");
  // The clock stopped on the basket with a second or less left, however
  // long before the read was (a timeout with 8 seconds, then the shot).
  GameSnapshot stopped = prev;
  stopped.display_clock = "0.6";
  stopped.team_score = 45;
  ACHECK_EQ(splash(prev, stopped), "BUZZER BEATER!");
  GameSnapshot timeout = prev;
  timeout.display_clock = "8.0";
  ACHECK_EQ(splash(timeout, stopped), "BUZZER BEATER!");
  // Not one: the clock ran out from more than a second (a layup at 2.5
  // seconds, or free throws with the clock stopped at 2.5 and the horn after
  // the inbound), free throws with the clock already out, a basket that
  // only closes the gap or adds to a lead.
  GameSnapshot long_ago = prev;
  long_ago.display_clock = "8.0";
  ACHECK_EQ(splash(long_ago, tie), "");
  GameSnapshot early = prev;
  early.display_clock = "2.9";
  early.team_score = 43;
  GameSnapshot layup = tie;
  layup.team_score = 45;
  ACHECK_EQ(splash(early, layup), "");
  GameSnapshot line = early;
  line.display_clock = "2.5";
  line.team_score = 44;
  ACHECK_EQ(splash(line, layup), "");
  GameSnapshot at_zero = tie;
  at_zero.team_score = 42;
  GameSnapshot free_throws = at_zero;
  free_throws.team_score = 44;
  ACHECK_EQ(splash(at_zero, free_throws), "");
  GameSnapshot short_of = tie;
  short_of.team_score = 43;
  ACHECK_EQ(splash(prev, short_of), "");
  GameSnapshot okc = hoops("fixtures/event_nba_clock.json", kThunder);
  okc.display_clock = "0.9";
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
  // Free throws with the clock stopped at 2.5, the horn after: the win.
  GameSnapshot fourth_line = fourth;
  fourth_line.display_clock = "2.5";
  fourth_line.team_score = 44;
  GameSnapshot ft_win = winner;
  ft_win.team_score = 45;
  ACHECK_EQ(splash(fourth_line, ft_win), "MIL WINS!");
  // ESPN still has the game on when the buzzer beater lands, then marks
  // it final: the buzzer beater, then the win.
  GameSnapshot horn = winner;
  horn.state = GameState::IN;
  horn.completed = false;
  ACHECK_EQ(splash(fourth, horn), "BUZZER BEATER!");
  ACHECK_EQ(splash(horn, winner), "MIL WINS!");
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
  // ATL breaks the tie in it: no lead change, and it is overtime only once.
  ACHECK_EQ(splash(ot_start, ot), "");
  // From behind, it is one.
  GameSnapshot ot_behind = ot_start;
  ot_behind.display_clock = "1:00";
  ot_behind.team_score = 96;
  ot_behind.opp_score = 98;
  ACHECK_EQ(splash(ot_behind, ot), "LEAD CHANGE!");
  ACHECK_EQ(splash(ot_behind, ot, true, false, all_but(Alert::LEAD_CHANGE)), "");
  // The first read in overtime already has a basket in it: overtime says so.
  GameSnapshot ot_basket = ot_start;
  ot_basket.display_clock = "4:41";
  ot_basket.team_score = 92;
  ACHECK_EQ(splash(end4, ot_basket), "OVERTIME");
  ACHECK_EQ(splash(ot_basket, ot), "");
  // Reads missed from late in the 4th, ATL behind, to ATL ahead in
  // overtime: overtime first; with its switch off, the lead change.
  GameSnapshot late4 = end4;
  late4.display_clock = "1:00";
  late4.nba.phase = Phase::PLAY;
  late4.team_score = 86;
  ACHECK_EQ(splash(late4, ot_basket), "OVERTIME");
  ACHECK_EQ(splash(late4, ot_basket, true, false, all_but(Alert::OVERTIME)), "LEAD CHANGE!");
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
  ACHECK(alert_bit(Alert::PLAYER) == 1u << 19);  // a favorite player's score (players.h)
  ACHECK(alert_bit(Alert::SCORE) == alert_bit(Alert::TOUCHDOWN) && alert_bit(Alert::NONE) == 0);
  ACHECK(kAlertsAll == 0xFFFFF);
  // A panel's saved mask holds the types turned off, so one from before the
  // Favorite player type has it on.
  const uint32_t saved_off = alert_bit(Alert::SAFETY);
  ACHECK((kAlertsAll & ~saved_off & alert_bit(Alert::PLAYER)) != 0);
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

using splash_test::fits;
using splash_test::layout;
using Lines = std::vector<std::string>;

void test_words_fit() {
  using esphome::gameday::panel_text;
  ACHECK(fits("DAL TOUCHDOWN", 10, 2) && !fits("DAL TOUCHDOWN", 10, 1));
  ACHECK(fits("INTER-CEPTION!", 10, 2) && !fits("INTERCEPTION!", 10, 3));
  ACHECK(!fits("A B C D", 1, 3));
  // The splash goes to the panel through panel_text, which keeps its line
  // break; the ticker's turns it into a space, and LVGL then breaks the
  // phrase where it likes.
  ACHECK(layout(panel_text("NYY\nWALK-OFF", true), 10) == (Lines{"NYY", "WALK-OFF"}));
  ACHECK(layout(panel_text("NYY\nWALK-OFF"), 10) == (Lines{"NYY WALK-", "OFF"}));
  ACHECK(layout(panel_text("PHI\nFUMBLE RECOVERY", true), 16) == (Lines{"PHI", "FUMBLE RECOVERY"}));
  ACHECK(layout(panel_text("CIN\nINTER-CEPTION", true), 10) == (Lines{"CIN", "INTER-", "CEPTION"}));
  ACHECK(layout(panel_text("DAL TOUCHDOWN", true), 10) == (Lines{"DAL", "TOUCHDOWN"}));
  GameSnapshot g;
  g.team_color = "002a5c";
  g.opp_color = "004c54";
  // Every type, side and abbreviation length (ESPN's run from 2 to 5
  // letters), as the panel gets it, on every board: one panel (10
  // characters, 3 lines of spleen_12 from y 18), two panels or a 2x2 (16
  // characters, 3 lines of spleen_16 from y 16 of the board's 64 rows), and
  // three or four in a row, where the words sized for two panels run in a
  // label 192 or 256 px wide (24 or 32 characters; board_layout.h). A phrase
  // put under the abbreviation stays there.
  for (int t = 1; t <= (int) Alert::SCORE; t++)
    for (AlertSide side : {AlertSide::TEAM, AlertSide::OPP, AlertSide::GAME})
      for (const char *abbr : {"NY", "DAL", "UTAH", "UMASS"})
        for (bool neutral : {false, true})
          for (int w : {64, 128, 192, 256})
            for (int points : {1, 4, 12}) {
              const bool wide = w >= 128;  // GamedayComponent::wide_(): two panels or more
              g.team_abbr = g.opp_abbr = abbr;
              BigPlay p{(Alert) t, side, points};
              const std::string raw = big_play_splash(p, g, neutral, wide).text;
              const std::string text = panel_text(raw, true);
              const Lines lines = layout(text, esphome::gameday::splash_chars(w));
              const bool own_line = raw.find('\n') != std::string::npos;
              const bool ok = !text.empty() && !lines.empty() && lines.size() <= 3 &&
                              (!own_line || lines[0] == abbr);
              ACHECK(ok);
              if (!ok)
                printf("  type %d side %d abbr %s width %d: \"%s\"\n", t, (int) side, abbr, w, text.c_str());
              // Never more lines than on two panels.
              if (w > 128)
                ACHECK(lines.size() <= layout(text, 16).size());
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
