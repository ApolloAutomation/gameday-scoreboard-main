// Play animations: classifier, goldens and the drawing's invariants.
// GOLDEN_WRITE=1 rewrites fixtures/picture_golden.txt instead of comparing.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
  // The try or the next play replaced it
  for (const char *t : {"Extra Point Good", "Kickoff", "Two Point Pass", "Pass Reception"})
    PCHECK(anim_of(Alert::TOUCHDOWN, false, td(t, "pass deep right for 30 yards, TOUCHDOWN")).kind ==
           AnimKind::NONE);
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

// Text rows in band aren't mirrored: equal there
bool mirrored(const std::vector<uint16_t> &a, const std::vector<uint16_t> &b, int w, int h, PxBox band = PxBox{}) {
  for (int y = 0; y < h; y++) {
    const bool text = !band.empty() && y >= band.y && y < band.y + band.h;
    for (int x = 0; x < w; x++)
      if (a[(size_t) y * w + x] != b[(size_t) y * w + (text ? x : w - 1 - x)])
        return false;
  }
  return true;
}

bool goal_kind(AnimKind k) { return k == AnimKind::SOCCER_GOAL || k == AnimKind::HOCKEY_GOAL; }

// Every frame as the panel draws it, checked
uint32_t check_scene(AnimKind kind, int w, int h, const Kit2 &kit, int &frames) {
  AnimArgs a;
  a.kind = kind;
  const bool hr = kind == AnimKind::HOME_RUN;
  if (hr) {
    a.dist = 401;
    a.flags = 4 << kAnimFieldShift;
  }
  const Scene sc = scene_make(a, w, h, kit.color, kit.alt, kit.other, kit.other_alt, 7);
  AnimArgs am = a;
  am.flags |= kAnimMirror;
  const Scene scm = scene_make(am, w, h, kit.color, kit.alt, kit.other, kit.other_alt, 7);
  const size_t n = (size_t) w * h;
  std::vector<uint16_t> inc(n), full(n), mir(n);
  const std::vector<uint32_t> ts = scene_times(kind);
  const int limit = sc.g.s == 2 ? 12000 : 3500;
  uint32_t hash = 2166136261u;
  bool complete = true, small = true, mirror_ok = true;
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
      if (!mirrored(full, mir, w, h, scene_pose(sc, t).word))
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
      PCHECK(p.ball_x >= 0 && p.ball_x < w && p.ball_y >= 0 && p.ball_y < h);
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
  for (AnimKind k : {AnimKind::TD_PASS, AnimKind::TD_RUN, AnimKind::TD_RETURN, AnimKind::HOME_RUN,
                     AnimKind::SOCCER_GOAL, AnimKind::HOCKEY_GOAL})
    for (const auto &s : kSceneScreens)
      for (const Kit2 &kit : kReviewKits) {
        int frames = 0;
        const uint32_t hash = check_scene(k, s[0], s[1], kit, frames);
        char line[96];
        snprintf(line, sizeof line, "%s %dx%d %s %d %08x\n", anim_name(k), s[0], s[1], kit.name, frames, hash);
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
  test_word();
  test_kits();
  test_geometry();
  failures += test_goldens();
  total_checks += checks;
  return failures;
}
