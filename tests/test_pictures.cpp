// Play animations: classifier, goldens and the drawing's invariants.
// GOLDEN_WRITE=1 rewrites fixtures/picture_golden.txt instead of comparing.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ArduinoJson.h"
#include "play_anim.h"
#include "split.h"
#include "scene_render.h"
#include "team_colors.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define PCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

bool kick_kind(AnimKind k) { return k == AnimKind::FIELD_GOAL || k == AnimKind::EXTRA_POINT; }

GameSnapshot td(const std::string &type, const std::string &words) {
  GameSnapshot g;
  g.play_type = type;
  g.last_play = words;
  return g;
}

void test_classifier() {
  // ESPN's play types: ids 67 and 68, then the returns
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("Passing Touchdown", "")).kind == AnimKind::TD_PASS);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("Rushing Touchdown", "")).kind == AnimKind::TD_RUN);
  for (const char *t : {"Interception Return Touchdown", "Fumble Return Touchdown", "Kickoff Return Touchdown",
                        "Punt Return Touchdown", "Blocked Punt Touchdown", "Fumble Recovery (Own) Touchdown"})
    PCHECK(anim_of(Alert::TOUCHDOWN, false, td(t, "")).kind == AnimKind::TD_RETURN);
  // The try or next play replaced it; kicks play
  for (const char *t : {"Kickoff", "Two Point Pass", "Pass Reception"})
    PCHECK(anim_of(Alert::TOUCHDOWN, false, td(t, "pass deep right for 30 yards, TOUCHDOWN")).kind ==
           AnimKind::NONE);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("Extra Point Good", "(P. Durkin KICK)")).kind == AnimKind::EXTRA_POINT);
  // Words only, as the demo and the watcher send them
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "Pass deep right for 21 yards, TOUCHDOWN")).kind ==
         AnimKind::TD_PASS);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "Pass deep middle for 44 yards, TOUCHDOWN")).kind ==
         AnimKind::TD_PASS);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "Rush right end for 12 yards, TOUCHDOWN")).kind ==
         AnimKind::TD_RUN);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "Interception returned 31 yards, TOUCHDOWN")).kind ==
         AnimKind::TD_RETURN);
  // The extra point's words follow the touchdown's
  const GameSnapshot nfl = td("", "(Shotgun) J.Smith pass short right to K.Jones for 8 yards, TOUCHDOWN. "
                                  "C.Little extra point is GOOD, Center-R.Matiscik, Holder-L.Cooke.");
  const AnimArgs a = anim_of(Alert::TOUCHDOWN, false, nfl);
  PCHECK(a.kind == AnimKind::TD_PASS);
  PCHECK(a.dist == 8);
  PCHECK((a.flags & kAnimShort) != 0);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "J.Back up the middle for 2 yards, TOUCHDOWN")).kind ==
         AnimKind::TD_RUN);
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "J.Back scrambles left end for 31 yards, TOUCHDOWN")).dist == 31);
  // College words: "5 Yd Run"
  PCHECK(anim_of(Alert::TOUCHDOWN, true, td("Rushing Touchdown", "Kaytron Allen 5 Yd Run (Ryan Barker Kick)")).dist ==
         5);
  PCHECK((anim_of(Alert::TOUCHDOWN, true, td("Rushing Touchdown", "")).flags & kAnimMirror) != 0);
  PCHECK((anim_of(Alert::TOUCHDOWN, false, td("Passing Touchdown", "pass deep left for 12 yards")).flags & kAnimDeep) !=
         0);
  // No touchdown in the words, or not a touchdown
  PCHECK(anim_of(Alert::TOUCHDOWN, false, td("", "End of the 2nd quarter")).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::FIELD_GOAL, false, td("Passing Touchdown", "")).kind == AnimKind::NONE);
  // Home runs: ESPN types them "Play Result" (event_mlb_hr.json)
  const AnimArgs hr = anim_of(Alert::HOME_RUN, true, td("Play Result", "Mesa Jr. homered to right center (401 feet), Caminero scored."));
  PCHECK(hr.kind == AnimKind::HOME_RUN);
  PCHECK(hr.dist == 401);
  PCHECK(((hr.flags >> kAnimFieldShift) & 7) == 4);
  PCHECK(anim_of(Alert::GRAND_SLAM, false, td("", "J.Doe homered to left (612 feet)")).dist == 520);
  PCHECK(((anim_of(Alert::HOME_RUN, false, td("", "J.Doe homered to left center")).flags >> kAnimFieldShift) & 7) == 2);
  // The watcher's guess: no distance
  const AnimArgs guess = anim_of(Alert::HOME_RUN, false, td("", ""));
  PCHECK(guess.kind == AnimKind::HOME_RUN && guess.dist == 0);
  PCHECK(anim_of(Alert::WALK_OFF, false, td("Single", "J.Doe singled to left, R.Roe scored.")).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::WALK_OFF, false, td("", "J.Doe homered to center (420 feet)")).kind == AnimKind::HOME_RUN);
  PCHECK(anim_of(Alert::RUN, false, td("", "J.Doe homered to center (420 feet)")).kind == AnimKind::NONE);
}

int dark(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return (r > g ? (r > b ? r : b) : (g > b ? g : b)) < 0x60;
}
bool greenish(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return g > r + 24 && g > b + 24 && g < 0xD8;
}
int ice_gap(uint32_t c) {
  return abs((int) ((c >> 16) & 255) - (int) ((kIceRgb >> 16) & 255)) +
         abs((int) ((c >> 8) & 255) - (int) ((kIceRgb >> 8) & 255)) + abs((int) (c & 255) - (int) (kIceRgb & 255));
}
uint32_t rgb888(uint16_t c) {
  return ((uint32_t) ((c >> 11) & 31) << 19) | ((uint32_t) ((c >> 5) & 63) << 10) | ((uint32_t) (c & 31) << 3);
}

void test_kits() {
  std::vector<uint32_t> colors = {0x000000, 0x010101, 0x0A0A0A, 0x002244, 0x004C54, 0x125740, 0x203731, 0x00FF00,
                                  0xFFFFFF, 0x808080, 0xFF0000, 0x0000FF, 0x00008B, 0x013369};
  for (const TeamColor &t : kTeamColors)
    colors.push_back(t.rgb);
  for (uint32_t c : colors)
    for (uint32_t alt : {0u, 0xFFFFFFu, 0x000000u, 0x125740u, 0xFFB612u}) {
      const Kit k = kit_colors(c, alt);
      PCHECK(!dark(k.jersey) && !greenish(k.jersey));
      const Kit d = kit_against(k, alt ? alt : 0x123456, c);
      PCHECK(!dark(d.jersey) && !greenish(d.jersey));
      PCHECK(d.jersey != k.jersey);
    }
  // Soccer: a keeper apart from both, letters lit on black
  for (uint32_t c : colors)
    for (uint32_t other : {0xFFFFFFu, 0xF0D020u, 0x30C8E8u, 0xD040D0u, 0xF07818u, 0x000000u}) {
      AnimArgs a;
      a.kind = AnimKind::SOCCER_GOAL;
      const Scene sc = scene_make(a, 128, 64, c, 0, other, 0, 3);
      const uint32_t keeper = rgb888(sc.keeper[4]);
      PCHECK(sc.keeper[4] != sc.off[4] && sc.keeper[4] != sc.def[4] && !greenish(keeper) && !dark(keeper));
      PCHECK(!dark(rgb888(sc.word[4])) && sc.word_hi[4] == rgb565(0xFFFFFF));
    }
  // Hockey: both sides clear of the ice and apart
  for (uint32_t c : colors)
    for (uint32_t alt : {0u, 0xFFFFFFu, 0x000000u, 0xC0C0C0u, 0x99D9D9u}) {
      AnimArgs a;
      a.kind = AnimKind::HOCKEY_GOAL;
      const Scene sc = scene_make(a, 128, 64, c, alt, alt ? alt : 0xA2AAAD, c, 5);
      const uint32_t mine = rgb888(sc.off[4]), theirs = rgb888(sc.def[4]);
      PCHECK(ice_gap(mine) >= 140 && ice_gap(theirs) >= 140 && sc.off[4] != sc.def[4]);
      PCHECK(!dark(rgb888(sc.word[4])));
      const Kit k = ice_kit(kit_colors(c, alt), c, alt);
      PCHECK(ice_gap(k.jersey) >= 150);
    }
}

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

GameSnapshot read_event(const char *path, Sport sport, uint32_t team) {
  GameSnapshot s;
  PCHECK(parse_event_str(slurp(path), sport, team, s));
  return s;
}

AnimKind anim_kind(const GameSnapshot &prev, const GameSnapshot &cur) {
  return (AnimKind) decide_splash(prev, cur, true).anim;
}

void test_goal_classifier() {
  // ARS @ SUN: the penalty at 90'+7' (test_alerts.cpp)
  GameSnapshot end = read_event("fixtures/event_epl_final.json", Sport::SOCCER, 359);
  PCHECK(end.soc.goals.size() == 2 && end.soc.goals.back().penalty);
  end.state = GameState::IN;
  end.completed = false;
  GameSnapshot before = end;
  before.soc.goals.pop_back();
  before.team_score -= 1;
  const Splash pen = decide_splash(before, end, true);
  PCHECK(pen.text == "PENALTY GOAL!" && pen.anim == (uint8_t) AnimKind::SOCCER_GOAL);
  PCHECK(decide_splash(before, end, true, false, kAlertsAll & ~alert_bit(Alert::PENALTY_GOAL)).anim ==
         (uint8_t) AnimKind::SOCCER_GOAL);
  PCHECK((anim_of(Alert::SOCCER_GOAL, true, end, &before, 0).flags & kAnimMirror) != 0);
  // An own goal plays for the side it counts for
  GameSnapshot own = end;
  own.soc.goals.back().own_goal = true;
  own.soc.goals.back().penalty = false;
  PCHECK(anim_kind(before, own) == AnimKind::SOCCER_GOAL);
  GameSnapshot theirs = before;
  theirs.opp_score += 1;
  SoccerGoal og;
  og.ours = false;
  og.own_goal = true;
  theirs.soc.goals.push_back(og);
  PCHECK(decide_splash(before, theirs, true).text == "SUN GOAL" && anim_kind(before, theirs) == AnimKind::SOCCER_GOAL);
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, theirs, &before, 1).kind == AnimKind::SOCCER_GOAL);
  // The newest goal is the other side's: no scene
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, theirs, &before, 0).kind == AnimKind::NONE);
  GameSnapshot ours_said = end;
  ours_said.soc.goals.back().ours = false;
  PCHECK(anim_kind(before, ours_said) == AnimKind::NONE);
  // Both scored between reads, no goal words: no scene
  GameSnapshot both = before;
  both.team_score += 1;
  both.opp_score += 1;
  PCHECK(decide_splash(before, both, true).text == "GOAL!" && anim_kind(before, both) == AnimKind::NONE);
  // Never a shootout, nor without the read or side
  GameSnapshot so = end;
  so.soc.team_shootout = 3;
  so.soc.opp_shootout = 2;
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, so, &before, 0).kind == AnimKind::NONE);
  GameSnapshot so_status = end;
  so_status.soc.status = "STATUS_SHOOTOUT";
  PCHECK(anim_of(Alert::PENALTY_GOAL, false, so_status, &before, 0).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, end).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, end, &before, -1).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::SOCCER_GOAL, false, before, &before, 0).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::RED_CARD, false, end, &before, 0).kind == AnimKind::NONE);

  // COL @ WPG (test_alerts.cpp)
  GameSnapshot prev = read_event("fixtures/event_nhl_live.json", Sport::HOCKEY, 28);
  GameSnapshot goal = prev;
  goal.team_score += 1;
  PCHECK(anim_kind(prev, goal) == AnimKind::HOCKEY_GOAL);
  GameSnapshot col = prev;
  col.opp_score += 1;
  PCHECK(decide_splash(prev, col, true).text == "COL GOAL" && anim_kind(prev, col) == AnimKind::HOCKEY_GOAL);
  GameSnapshot two = goal;
  two.opp_score += 1;
  PCHECK(anim_kind(prev, two) == AnimKind::NONE);
  // The shootout is period 5, but 2OT in the playoffs
  GameSnapshot so0 = prev, so1 = goal;
  so0.period = so1.period = 5;
  PCHECK(anim_of(Alert::HOCKEY_GOAL, false, so1, &so0, 0).kind == AnimKind::NONE);
  so0.nhl.playoffs = so1.nhl.playoffs = true;
  PCHECK(anim_of(Alert::HOCKEY_GOAL, false, so1, &so0, 0).kind == AnimKind::HOCKEY_GOAL);
  PCHECK(std::string(anim_name(AnimKind::SOCCER_GOAL)) == "soccer" &&
         std::string(anim_name(AnimKind::HOCKEY_GOAL)) == "hockey");
}

// ESPN's football reads keep the play type through parse_event
GameSnapshot from_scoreboard(const char *path, const char *id, uint32_t team) {
  JsonDocument doc;
  PCHECK(!deserializeJson(doc, slurp(path), DeserializationOption::NestingLimit(40)));
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (std::string(ev["id"] | "") != id)
      continue;
    std::string one;
    serializeJson(ev, one);
    GameSnapshot s;
    PCHECK(parse_event_str(one, Sport::FOOTBALL, team, s, id));
    s.state = GameState::IN;
    return s;
  }
  PCHECK(false);
  return GameSnapshot{};
}

std::string first_say(const std::string &say) { return say.substr(0, say.find('|')); }

// Each choice is shorter than the one before
bool ladder(const std::string &say) {
  size_t prev = 999;
  for (size_t at = 0; at <= say.size();) {
    const size_t e = say.find('|', at);
    const size_t n = (e == std::string::npos ? say.size() : e) - at;
    if (n == 0 || n > prev + 12)
      return false;
    prev = n;
    if (e == std::string::npos)
      break;
    at = e + 1;
  }
  return !say.empty();
}

void test_captions() {
  // Names from ESPN's words
  PCHECK(espn::detail::initial_name("(Shotgun) J.Burrow pass deep middle to J.Chase for 30 yards, TOUCHDOWN.", 69) ==
         "Chase");
  PCHECK(espn::detail::initial_name("(P. Durkin KICK)", 16) == "Durkin");
  PCHECK(espn::detail::initial_name("J.Goff pass short left intended for A.St. Brown INTERCEPTED", 59) == "St. Brown");
  PCHECK(espn::detail::yd_name("Ja'Marr Chase 30 Yd pass from Joe Burrow (Evan McPherson Kick)") == "Chase");
  PCHECK(espn::detail::yd_name("James Mitchell Jr. 0 Yd Fumble Recovery") == "Mitchell");
  PCHECK(espn::detail::say_name("O'Neil") == "O'NEIL" && espn::detail::say_name("J. HUGHES") == "J. HUGHES");
  PCHECK(espn::detail::say_name("Wolfeschlegelstein").empty() && espn::detail::say_name("A").empty());
  PCHECK(espn::detail::say_name("Guimar\xC3\xA3""es").empty());
  // A touchdown: the scorer, his yards, the kind
  GameSnapshot prev = td("Pass Reception", "J.Burrow pass short left to T.Higgins for 7 yards");
  prev.play_id = "1";
  prev.team_abbr = "CIN";
  prev.team_score = 14;
  GameSnapshot cur = td("Passing Touchdown",
                        "(Shotgun) J.Burrow pass deep middle to J.Chase for 30 yards, TOUCHDOWN. E.McPherson extra "
                        "point is GOOD, Center-W.Wagner, Holder-R.Rehkow.");
  cur.play_id = "2";
  cur.team_abbr = "CIN";
  cur.team_score = 21;
  Splash s{"TOUCHDOWN!", 0, "CIN", false, 0, "+7"};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::TEAM);
  PCHECK(s.anim == (uint8_t) AnimKind::TD_PASS && s.anim_dist == 30);
  PCHECK(first_say(s.anim_say) == "CHASE 30 YD TD PASS" && ladder(s.anim_say));
  PCHECK(s.anim_score == "CIN 21 +7");
  // A kickoff return: the return's yards, not the kick's
  cur = td("Kickoff Return Touchdown", "A.McNamara kicks 60 yards from NYJ 35 to LV 5. D.Laube for 95 yards, "
                                       "TOUCHDOWN. M.Gay extra point is GOOD, Center-A.Ward, Holder-A.Cole.");
  cur.team_score = 21;
  cur.team_abbr = "LV";
  s = Splash{"TOUCHDOWN!", 0, "LV", false, 0, "+7"};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::TEAM);
  PCHECK(s.anim_dist == 95 && first_say(s.anim_say) == "LAUBE 95 YD KICKOFF RETURN TD");
  PCHECK(s.anim_say.find("|95 YD KICK RET TD") != std::string::npos);
  // The other side's pick six, in their score
  cur = td("Interception Return Touchdown", "");
  cur.opp_abbr = "BC";
  cur.opp_score = 15;
  s = Splash{"BC TOUCHDOWN", 0, "BC", false, 3, "+7"};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::OPP);
  PCHECK(s.anim_say == "PICK SIX" && s.anim_score == "BC 15 +7");
  // College words: "#13 J.Grier", a summary's full name
  GameSnapshot app = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401866627", 2026);
  GameSnapshot app0 = app;
  app0.team_score -= 6;
  app0.play_id = "0";
  s = decide_splash(app0, app, true);
  PCHECK(s.anim == (uint8_t) AnimKind::TD_RUN && first_say(s.anim_say) == "GRIER 28 YD TD RUN");
  PCHECK(s.anim_score == "APP 51 +6");
  cur = td("Rushing Touchdown", "Kaytron Allen 5 Yd Run (Ryan Barker Kick)");
  s = Splash{"TOUCHDOWN!", 0, "PSU", false, 0, "+7"};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::TEAM);
  PCHECK(first_say(s.anim_say) == "ALLEN 5 YD TD RUN");
  // A favorite player's name wins
  s = Splash{"ALLEN TOUCHDOWN", 0, "PSU", false, 0, "+7"};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::TEAM, "K. ALLEN");
  PCHECK(first_say(s.anim_say) == "K. ALLEN 5 YD TD RUN");
  // No name, no yards: the play alone
  cur = td("Rushing Touchdown", "");
  cur.team_score = 21;
  s = Splash{"TOUCHDOWN!", 0, "", false, 0, ""};
  splash_anim(s, Alert::TOUCHDOWN, prev, cur, AlertSide::TEAM);
  PCHECK(s.anim_say == "TD RUN|TD" && s.anim_score == "21");
  // Home runs: runs, distance, field, the batter
  cur = td("Play Result", "Mesa Jr. homered to right center (401 feet), Caminero scored.");
  cur.team_abbr = "MIA";
  cur.team_score = 5;
  s = Splash{"HOME RUN!", 0, "MIA", false, 0, "+2 RUNS"};
  splash_anim(s, Alert::HOME_RUN, prev, cur, AlertSide::TEAM);
  PCHECK(first_say(s.anim_say) == "MESA 2-RUN HOME RUN 401 FT RIGHT CENTER" && s.anim_score == "MIA 5 +2");
  s = Splash{"GRAND SLAM!", 0, "MIA", false, 0, "+4 RUNS"};
  splash_anim(s, Alert::GRAND_SLAM, prev, cur, AlertSide::TEAM);
  PCHECK(first_say(s.anim_say) == "MESA GRAND SLAM 401 FT RIGHT CENTER");
  s = Splash{"WALK-OFF!", 0, "MIA", false, 0, "+1"};
  splash_anim(s, Alert::WALK_OFF, prev, cur, AlertSide::TEAM);
  PCHECK(s.anim_say.find("|WALK-OFF HR") != std::string::npos);
  // Soccer and hockey (test_alerts.cpp's games)
  GameSnapshot end = read_event("fixtures/event_epl_final.json", Sport::SOCCER, 359);
  end.state = GameState::IN;
  end.completed = false;
  GameSnapshot before = end;
  before.soc.goals.pop_back();
  before.team_score -= 1;
  s = decide_splash(before, end, true);
  PCHECK(s.anim_say.find("PENALTY GOAL") != std::string::npos);
  PCHECK(s.anim_score == end.team_abbr + " " + std::to_string(end.team_score) + " +1");
  GameSnapshot own = end;
  own.soc.goals.back().own_goal = true;
  own.soc.goals.back().penalty = false;
  PCHECK(decide_splash(before, own, true).anim_say == "OWN GOAL");
  GameSnapshot hk = read_event("fixtures/event_nhl_live.json", Sport::HOCKEY, 28);
  GameSnapshot goal = hk;
  goal.team_score += 1;
  goal.play_id = hk.play_id + "9";
  goal.last_play = "Gabriel Landeskog Goal (4) Wrist Shot, assists: Cale Makar (20)";
  PCHECK(decide_splash(hk, goal, true).anim_say == "LANDESKOG GOAL|GOAL");
  goal.last_play = "Faceoff won by WPG";
  PCHECK(decide_splash(hk, goal, true).anim_say == "GOAL");
  // Score lines, with and without a team
  PCHECK(say_score("", -1, 6) == "+6" && say_score("TA&M", 7, 0) == "TA&M 7");
  PCHECK(say_score("BUF", 21, 6) == "BUF 21 +6");
  // Every Try it play has a caption
  for (const ScenePlay &p : kScenePlays) {
    const AnimSample a = anim_sample(p.play, "BUF", 3);
    PCHECK(ladder(a.say) && a.score.find("BUF ") == 0 && a.args.kind != AnimKind::NONE);
    PCHECK(anim_sample(p.play, "", 0).score[0] == '+');
  }
}

// The read before cur: side has pts fewer
GameSnapshot before_score(const GameSnapshot &cur, int side, int pts, const char *type, const char *words) {
  GameSnapshot p = cur;
  (side ? p.opp_score : p.team_score) -= pts;
  p.play_type = type;
  p.last_play = words;
  p.play_id = "prev";
  return p;
}

// Made kicks only, from ESPN's types and words
void test_kicks() {
  // TEX @ TXST: "Field Goal Good", college words
  GameSnapshot fg = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401856667", 251);
  PCHECK(fg.play_type == "Field Goal Good");
  const int side = fg.team_abbr == "TEX" ? 0 : 1;
  Splash s = decide_splash(before_score(fg, side, 3, "Rush", "rush for 2 yards"), fg, true);
  PCHECK(s.anim == (uint8_t) AnimKind::FIELD_GOAL && s.anim_dist == 48);
  PCHECK(first_say(s.anim_say) == "SPETIC 48 YD FIELD GOAL" && ladder(s.anim_say));
  PCHECK(s.anim_score == "TEX 52 +3");
  // NFL words alone, and ESPN's summary
  PCHECK(anim_of(Alert::FIELD_GOAL, false, td("", "J.Slye 46 yard field goal is GOOD, Center-M.Cox, Holder-T.Morstead."))
             .dist == 46);
  PCHECK(anim_of(Alert::FIELD_GOAL, true, td("", "Joey Slye 46 Yd Field Goal")).kind == AnimKind::FIELD_GOAL);
  PCHECK((anim_of(Alert::FIELD_GOAL, true, td("Field Goal Good", "")).flags & kAnimMirror) != 0);
  PCHECK(anim_of(Alert::FIELD_GOAL, false, td("Field Goal Good", "")).dist == 0);
  // Never a miss, a block, a return or a fake
  for (const char *t : {"Field Goal Missed", "Blocked Field Goal", "Missed Field Goal Return", "Kickoff",
                        "Blocked Field Goal Touchdown", "Rush"})
    PCHECK(anim_of(Alert::FIELD_GOAL, false, td(t, "J.Slye 46 yard field goal is GOOD")).kind == AnimKind::NONE);
  for (const char *w : {"C.Boswell 57 yard field goal is No Good, Wide Left, Center-C.Kuntz, Holder-C.Johnston.",
                        "T.Loop 34 yard field goal is BLOCKED (D.Hunter), RECOVERED by BLT", "Field goal missed",
                        "End of the 2nd quarter", "FAKE FIELD GOAL. J.Stout pass to J.Tucker for 9 yards"})
    PCHECK(anim_of(Alert::FIELD_GOAL, false, td("", w)).kind == AnimKind::NONE);

  // CSU @ WYO: ESPN's own extra point play
  GameSnapshot pat = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401860878", 36);
  PCHECK(pat.play_type == "Extra Point Good" && pat.last_play == "(K. Nguma KICK)");
  const int ps = pat.team_abbr == "CSU" ? 0 : 1;
  s = decide_splash(before_score(pat, ps, 1, "Passing Touchdown", "pass for 9 yards, TOUCHDOWN"), pat, true);
  PCHECK(s.anim == (uint8_t) AnimKind::EXTRA_POINT && first_say(s.anim_say) == "NGUMA EXTRA POINT GOOD");
  PCHECK(s.anim_score == "CSU 7 +1");
  // Touchdown and kick in one read: the kick plays
  s = decide_splash(before_score(pat, ps, 7, "Rush", "rush for 3 yards"), pat, true);
  PCHECK(s.text.find("TOUCHDOWN") != std::string::npos && s.anim == (uint8_t) AnimKind::EXTRA_POINT);
  PCHECK(s.anim_say == "TOUCHDOWN + PAT GOOD|TD + PAT GOOD|TD + PAT" && s.anim_score == "CSU 7 +7");
  // Six with the kick's words: no scene, nothing guessed
  s = decide_splash(before_score(pat, ps, 6, "Rush", "rush for 3 yards"), pat, true);
  PCHECK(s.anim == 0);
  // NFL: the kick joins the touchdown's words later
  GameSnapshot td1 = before_score(pat, ps, 0, "Passing Touchdown",
                                  "(Shotgun) J.Burrow pass deep middle to J.Chase for 30 yards, TOUCHDOWN.");
  td1.play_id = "4018";
  (ps ? td1.opp_score : td1.team_score) -= 1;
  GameSnapshot td0 = before_score(td1, ps, 6, "Rush", "J.Burrow scrambles for 4 yards");
  td0.play_id = "4017";
  s = decide_splash(td0, td1, true);
  PCHECK(s.anim == (uint8_t) AnimKind::TD_PASS);
  GameSnapshot td2 = td1;
  td2.last_play += " E.McPherson extra point is GOOD, Center-W.Wagner, Holder-R.Rehkow.";
  (ps ? td2.opp_score : td2.team_score) += 1;
  s = decide_splash(td1, td2, true);
  PCHECK(s.text.find("EXTRA POINT") != std::string::npos && s.anim == (uint8_t) AnimKind::EXTRA_POINT);
  PCHECK(first_say(s.anim_say) == "MCPHERSON EXTRA POINT GOOD");
  // Its summary words, and a missed or blocked kick
  PCHECK(anim_of(Alert::EXTRA_POINT, false, td("Rushing Touchdown", "Tony Pollard 40 Yd Rush (Joey Slye Kick)")).kind ==
         AnimKind::EXTRA_POINT);
  for (const char *w : {"... TOUCHDOWN. J.Slye extra point is No Good, Wide Right, Center-M.Cox.",
                        "... TOUCHDOWN. J.Slye extra point is BLOCKED, Center-M.Cox.", "(Kick failed)",
                        "... TOUCHDOWN. TWO-POINT CONVERSION ATTEMPT. C.Ward rushes left end. ATTEMPT SUCCEEDS."})
    PCHECK(anim_of(Alert::EXTRA_POINT, false, td("Rushing Touchdown", w)).kind == AnimKind::NONE);
  for (const char *t : {"Extra Point Missed", "Kickoff", "Two Point Pass"})
    PCHECK(anim_of(Alert::EXTRA_POINT, false, td(t, "(P. Durkin KICK)")).kind == AnimKind::NONE);
  PCHECK(std::string(anim_name(AnimKind::FIELD_GOAL)) == "fg" && std::string(anim_name(AnimKind::EXTRA_POINT)) == "pat");
  PCHECK(scene_ms(AnimKind::EXTRA_POINT) < scene_ms(AnimKind::FIELD_GOAL));
}

// Only a made two-point try; a return is the defense's
void test_two_point() {
  // TENN @ FUR: ESPN's failed try, then a good one
  GameSnapshot fur = from_scoreboard("fixtures/scoreboard_ncaa_live.json", "401856666", 2633);
  PCHECK(fur.play_type == "Two Point Rush" && fur.last_play == "(Two-Point Conversion failed)");
  PCHECK(anim_of(Alert::TWO_POINT, false, fur).kind == AnimKind::NONE);
  const int fs = fur.team_abbr == "FUR" ? 0 : 1;
  GameSnapshot good = fur;
  good.last_play = "(Two-Point Conversion good)";
  good.play_type = "Two Point Pass";
  Splash s = decide_splash(before_score(good, fs, 2, "Passing Touchdown", "pass for 9 yards, TOUCHDOWN"), good, true);
  PCHECK(s.anim == (uint8_t) AnimKind::TWO_POINT && (s.anim_flags & kAnimTwoRun) == 0);
  PCHECK(s.anim_say == "2-PT CONVERSION GOOD|2-PT GOOD");
  PCHECK(s.anim_score == "FUR " + std::to_string(fs ? good.opp_score : good.team_score) + " +2");
  good.play_type = "Two Point Rush";
  PCHECK((anim_of(Alert::TWO_POINT, false, good).flags & kAnimTwoRun) != 0);
  // The touchdown and its two points in one read
  s = decide_splash(before_score(good, fs, 8, "Rush", "rush for 3 yards"), good, true);
  PCHECK(s.anim == (uint8_t) AnimKind::TWO_POINT && s.anim_say == "TOUCHDOWN + 2-PT GOOD|TD + 2-PT GOOD|TD + 2-PT");
  PCHECK(s.anim_score.size() > 3 && s.anim_score.compare(s.anim_score.size() - 3, 3, " +8") == 0);
  s = decide_splash(before_score(good, fs, 6, "Rush", "rush for 3 yards"), good, true);
  PCHECK(s.anim == 0);
  // NFL words: the runner, the receiver
  GameSnapshot prev = td("Rush", "C.Ward rushes for 3 yards");
  prev.play_id = "1";
  GameSnapshot cur = td("Rushing Touchdown", "T.Pollard left end for 1 yard, TOUCHDOWN. TWO-POINT CONVERSION ATTEMPT. "
                                             "C.Ward rushes left end. ATTEMPT SUCCEEDS.");
  cur.team_abbr = "TEN";
  cur.team_score = 22;
  s = Splash{"2-PT!", 0, "TEN", false, 0, "+2"};
  splash_anim(s, Alert::TWO_POINT, prev, cur, AlertSide::TEAM);
  PCHECK(s.anim == (uint8_t) AnimKind::TWO_POINT && (s.anim_flags & kAnimTwoRun) != 0);
  PCHECK(first_say(s.anim_say) == "WARD 2-PT CONVERSION GOOD" && s.anim_score == "TEN 22 +2");
  cur.last_play = "J.Allen pass short right to K.Shakir for 6 yards, TOUCHDOWN. TWO-POINT CONVERSION ATTEMPT. "
                  "J.Allen pass to S.Diggs is complete. ATTEMPT SUCCEEDS.";
  s = Splash{"2-PT!", 0, "BUF", false, 0, "+2"};
  splash_anim(s, Alert::TWO_POINT, prev, cur, AlertSide::TEAM);
  PCHECK((s.anim_flags & kAnimTwoRun) == 0 && first_say(s.anim_say) == "DIGGS 2-PT CONVERSION GOOD");
  // ESPN's scoring summaries, both kinds
  cur.last_play = "Tony Pollard 1 Yd Rush (Cam Ward Run for Two-Point Conversion)";
  splash_anim(s, Alert::TWO_POINT, prev, cur, AlertSide::TEAM);
  PCHECK((s.anim_flags & kAnimTwoRun) != 0 && first_say(s.anim_say) == "WARD 2-PT CONVERSION GOOD");
  cur.last_play = "Tee Higgins 17 Yd pass from Joe Burrow (Joe Burrow Pass to Ja'Marr Chase for Two-Point Conversion)";
  splash_anim(s, Alert::TWO_POINT, prev, cur, AlertSide::TEAM);
  PCHECK((s.anim_flags & kAnimTwoRun) == 0 && first_say(s.anim_say) == "CHASE 2-PT CONVERSION GOOD");
  // Failed tries, and a safety, never play
  for (const char *w : {"... TOUCHDOWN. TWO-POINT CONVERSION ATTEMPT. C.Ward rushes left end. ATTEMPT FAILS.",
                        "... TWO-POINT CONVERSION ATTEMPT. J.Allen pass to S.Diggs is incomplete. ATTEMPT FAILS.",
                        "(Two-Point Pass Conversion Failed)", "(Two-Point Conversion no good)",
                        "C.Ward sacked in End Zone for -9 yards, SAFETY"})
    PCHECK(anim_of(Alert::TWO_POINT, false, td("", w)).kind == AnimKind::NONE);
  PCHECK(anim_of(Alert::SAFETY, false, td("", "(Two-Point Conversion good)")).kind == AnimKind::NONE);
  // The defense's return: their run, credited to them
  cur.last_play = "TWO-POINT CONVERSION ATTEMPT. J.Allen pass to S.Diggs INTERCEPTED by K.Joseph. DEFENSIVE TWO-POINT "
                  "ATTEMPT. K.Joseph for 100 yards. ATTEMPT SUCCEEDS.";
  cur.opp_abbr = "DET";
  cur.opp_score = 23;
  s = Splash{"DET 2-PT", 0, "DET", false, 4, "+2"};
  splash_anim(s, Alert::TWO_POINT, prev, cur, AlertSide::OPP);
  PCHECK(s.anim == (uint8_t) AnimKind::TD_RETURN && s.anim_say == "DEFENSIVE 2-PT RETURN|DEF 2-PT RETURN|DEF 2-PT");
  PCHECK(s.anim_score == "DET 23 +2");
  PCHECK(std::string(anim_name(AnimKind::TWO_POINT)) == "two");
  PCHECK(anim_sample("two", "", 0).args.kind == AnimKind::TWO_POINT && anim_sample("two", "", 0).score == "+2");
  PCHECK((anim_sample("tworun", "", 0).args.flags & kAnimTwoRun) != 0);
}

// Lines fit the setup, clear of posts, word, ball
void test_caption_fit() {
  for (const ScenePlay &p : kScenePlays)
    for (const auto &sz : kSceneScreens)
      for (int named = 0; named < 2; named++)
        for (int m = 0; m < 2; m++) {
          const Scene sc = scene_of(p, sz[0], sz[1], kReviewKits[2], named != 0, m != 0);
          const StageGeom &g = sc.g;
          PCHECK(sc.say[0] != 0 && sc.score[0] != 0);
          const bool football = sc.args.kind == AnimKind::TD_PASS || sc.args.kind == AnimKind::TD_RUN ||
                                sc.args.kind == AnimKind::TD_RETURN || sc.args.kind == AnimKind::TWO_POINT ||
                                kick_kind(sc.args.kind);
          for (uint32_t t : scene_times(sc.args.kind)) {
            const ScenePose ps = scene_pose(sc, t);
            for (const PxBox &c : ps.cap) {
              if (c.empty())
                continue;
              PCHECK(c.x >= 0 && c.x + c.w <= g.w && c.y >= 0);
              // The posts stand in the last 6s columns
              if (football)
                PCHECK(m ? c.x >= 6 * g.s : c.x + c.w <= g.w - 6 * g.s);
              PCHECK(!box_overlap(c, ps.word));
              PCHECK(c.y + c.h <= g.crowd_y || (c.y >= g.crowd_y && c.y + c.h <= g.field_y));
              // A pass or a kick flies under it
              if ((sc.args.kind == AnimKind::TD_PASS || sc.args.kind == AnimKind::TWO_POINT || kick_kind(sc.args.kind)) &&
                  ps.ball_free && c.y < g.crowd_y)
                PCHECK(ps.ball_y - 2 * g.s >= c.y + c.h);
            }
          }
        }
}

// G, then O's, A's and L's; longer when wider
void test_word() {
  int prev_n = 0;
  for (const auto &s : kSceneScreens) {
    const StageGeom g = stage_geom(s[0], s[1]);
    const int n = word_letters(g);
    PCHECK(n >= 6 && (n * 10 - 1) * g.s <= g.w);
    if (g.s == 1) {
      PCHECK(n > prev_n);
      prev_n = n;
    }
    int counts[4] = {0, 0, 0, 0}, last = 0;
    for (int i = 0; i < n; i++) {
      const int k = word_glyph(i, n);
      PCHECK(k >= last);
      last = k;
      counts[k]++;
    }
    PCHECK(counts[0] == 1 && counts[1] >= counts[2] && counts[2] >= 1 && counts[3] >= 1);
  }
  PCHECK(word_letters(stage_geom(256, 64)) >= 24);
}

// Text isn't mirrored: the word's rows and captions match unmirrored
bool mirrored(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int w, int h, const ScenePose &pa,
              const ScenePose &pb) {
  const PxBox band = pa.word;
  for (int y = 0; y < h; y++) {
    const bool text = !band.empty() && y >= band.y && y < band.y + band.h;
    for (int x = 0; x < w; x++) {
      int bx = text ? x : w - 1 - x;
      for (int k = 0; k < 3; k++) {
        const PxBox &c = pa.cap[k];
        if (!c.empty() && x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h)
          bx = pb.cap[k].x + (x - c.x);
      }
      if (a[(size_t) y * w + x] != b[(size_t) y * w + bx])
        return false;
    }
  }
  return true;
}

bool goal_kind(AnimKind k) { return k == AnimKind::SOCCER_GOAL || k == AnimKind::HOCKEY_GOAL; }

// Every frame as the panel draws it, checked
uint32_t check_scene(const ScenePlay &play, int w, int h, const Kit2 &kit, bool named, int &frames) {
  const Scene sc = scene_of(play, w, h, kit, named, false);
  const AnimKind kind = sc.args.kind;
  const bool hr = kind == AnimKind::HOME_RUN;
  const Scene scm = scene_of(play, w, h, kit, named, true);
  const size_t n = (size_t) w * h;
  std::vector<uint16_t> inc(n), full(n), mir(n);
  const std::vector<uint32_t> ts = scene_times(kind);
  const int limit = sc.g.s == 2 ? 12000 : 3500;
  uint32_t hash = 2166136261u;
  bool complete = true, small = true, mirror_ok = true;
  PCHECK(sc.say[0] != 0 && sc.score[0] != 0);
  for (size_t i = 0; i < ts.size(); i++) {
    const uint32_t t = ts[i];
    scene_full(full, sc, t);
    if (i == 0) {
      inc = full;
    } else {
      const int px = scene_step(inc, sc, ts[i - 1], t);
      const bool flash = scene_pose(sc, t).flash || scene_pose(sc, ts[i - 1]).flash;
      const bool pulse = px > limit && (flash || (!goal_kind(kind) && scene_pose(sc, t).hero_x >= sc.g.goal_x - 16 * sc.g.s));
      if (px > limit && !pulse) {
        small = false;
        printf("  %s %dx%d t=%u: %d dirty px\n", anim_name(kind), w, h, t, px);
      }
    }
    if (inc != full && complete) {
      complete = false;
      printf("  %s %dx%d %s: trails at t=%u\n", anim_name(kind), w, h, kit.name, t);
    }
    if (i % 7 == 0 && !hr) {
      scene_full(mir, scm, t);
      if (!mirrored(full, mir, w, h, scene_pose(sc, t), scene_pose(scm, t)))
        mirror_ok = false;
    }
    hash = fnv1a(hash, full);
  }
  PCHECK(complete);
  PCHECK(small);
  PCHECK(mirror_ok);
  PCHECK(scene_ms(kind) <= kAnimMaxMs);
  // Ends in the end zone, the net, or beyond
  const ScenePose end = scene_pose(sc, scene_ms(kind));
  if (goal_kind(kind)) {
    PCHECK(end.ball_free && end.ball_x > sc.g.goal_x && end.ball_x < sc.g.back_x);
    PCHECK(end.ball_y > sc.g.bar_y && end.ball_y <= sc.g.feet_y);
    PCHECK(end.hero_x >= 7 * sc.g.s && end.hero_x + 9 * sc.g.s <= w);
    // The word fills the width, in the dark above
    PCHECK(!end.word.empty() && end.word.x >= 0 && end.word.x + end.word.w <= w);
    PCHECK(end.word.w >= w - 12 * sc.g.s && end.word.y + end.word.h <= sc.g.crowd_y);
  } else if (kick_kind(kind)) {
    // Through the uprights, above the bar, and gone
    const int s = sc.g.s, post = w - 3 * s, bar = sc.g.crowd_y + 2 * s;
    PCHECK(!end.ball_free);
    bool through = false;
    for (uint32_t t = 0; t < scene_ms(kind) && !through; t += 5) {
      const ScenePose p = scene_pose(sc, t);
      if (p.ball_free && p.ball_x >= post) {
        through = true;
        PCHECK(p.ball_y - 2 * s > 3 * s && p.ball_y + 2 * s < bar);
      }
    }
    PCHECK(through);
    PCHECK(!end.cap[2].empty());
  } else if (!hr) {
    PCHECK(end.hero_x >= sc.g.goal_x);
    PCHECK(end.hero_x + 9 * sc.g.s <= w);
  }
  int far = 0;
  for (uint32_t t : ts) {
    const ScenePose p = scene_pose(sc, t);
    if (p.ball_free && p.ball_x > far)
      far = p.ball_x;
    if (p.ball_free && !hr)
      PCHECK(p.ball_x >= 0 && (p.ball_x < w || kick_kind(kind)) && p.ball_y >= 0 && p.ball_y < h);
  }
  if (hr)
    PCHECK(far >= w - (w / 10 < 6 ? 6 : w / 10 > 24 ? 24 : w / 10));
  // Same inputs, same bytes
  std::vector<uint16_t> again(n);
  scene_full(again, sc, ts[ts.size() / 2]);
  scene_full(full, sc, ts[ts.size() / 2]);
  PCHECK(again == full);
  frames = (int) ts.size();
  return hash;
}

int test_goldens() {
  std::ostringstream o;
  for (const ScenePlay &p : kScenePlays)
    for (const auto &s : kSceneScreens)
      for (int i = 0; i < 4; i++) {
        const Kit2 &kit = kReviewKits[i];
        int frames = 0;
        const uint32_t hash = check_scene(p, s[0], s[1], kit, i != 0, frames);
        char line[96];
        snprintf(line, sizeof line, "%s %dx%d %s %d %08x\n", p.play, s[0], s[1], kit.name, frames, hash);
        o << line;
      }
  const std::string got = o.str();
  const char *path = "fixtures/picture_golden.txt";
  const char *write = getenv("GOLDEN_WRITE");
  if (write != nullptr && write[0] == '1') {
    std::ofstream f(path, std::ios::binary);
    f << got;
    printf("wrote %s\n", path);
    return 0;
  }
  const std::string want = slurp(path);
  checks++;
  if (got == want)
    return 0;
  std::istringstream g(got), w(want);
  std::string lg, lw;
  while (std::getline(g, lg)) {
    if (!std::getline(w, lw) || lg != lw) {
      printf("FAIL picture golden: got \"%s\", want \"%s\" (GOLDEN_WRITE=1 after reviewing the GIFs)\n", lg.c_str(),
             lw.c_str());
      break;
    }
  }
  return 1;
}

void test_geometry() {
  for (const auto &s : kSceneScreens) {
    const StageGeom g = stage_geom(s[0], s[1]);
    PCHECK(g.crowd_y < g.field_y && g.field_y < g.feet_y && g.feet_y < g.h);
    PCHECK(g.feet_y - 24 * g.s >= g.field_y - 4 * g.s);
    PCHECK(g.goal_x + 9 * g.s <= g.w - 1 + 1);
  }
  PCHECK(stage_geom(128, 128).s == 2);
  PCHECK(stage_geom(256, 64).ez == 28);
  PCHECK(scene_ms(AnimKind::NONE) == 0);
  // The splash stays its full time after the scene
  const uint32_t stay = kSplashMs + scene_ms(AnimKind::TD_PASS);
  PCHECK(stay <= kSplashMs + kAnimMaxMs);
  PCHECK(splash_up(1000 + kSplashMs + 100, 1000, stay));
  PCHECK(!splash_up(1000 + stay, 1000, stay));
  PCHECK(!splash_up(1000 + kSplashMs + 100, 1000));
  PCHECK(area_covered(SplitAt::BOTTOM, 1000 + kSplashMs + 100, 1000, stay));
  PCHECK(kWatchSplashMs >= kSplashMs + kAnimMaxMs);
}

}  // namespace

int run_picture_tests(int &total_checks) {
  test_classifier();
  test_goal_classifier();
  test_captions();
  test_kicks();
  test_two_point();
  test_caption_fit();
  test_word();
  test_kits();
  test_geometry();
  failures += test_goldens();
  total_checks += checks;
  return failures;
}
