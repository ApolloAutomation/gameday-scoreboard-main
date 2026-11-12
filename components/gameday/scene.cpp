#include "scene.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace espn {

namespace {

constexpr uint32_t kTurfA = 0x0E5A1E, kTurfB = 0x0A4818, kTurfLine = 0x5E8A5E, kGoalLine = 0xC8C8C8;
constexpr int kEzDim = 30;
constexpr uint32_t kPost = 0xE8C820, kRail = 0x2A2A2A, kShadow = 0x041A08;
constexpr uint32_t kBall[3] = {0xB0602A, 0xD08040, 0x8A4A1C};
constexpr uint32_t kSkin[3][2] = {{0xF1C27D, 0xC68642}, {0xC68642, 0x8D5524}, {0x8D5524, 0x5C3A1E}};
constexpr uint32_t kHair[4] = {0x8A5A30, 0x5C3A1E, 0xC8A050, 0x3C2A1C};
constexpr uint32_t kPitchLine = 0x8AA88A, kMesh = 0x8C8C8C, kGoalFrame = 0xF0F0F0;
// Keepers wear the first of these clear of both kits
constexpr uint32_t kKeeperKits[] = {0xF0D020, 0xF07818, 0x30C8E8, 0xD040D0};
// Goal scenes: the play, then the word
constexpr uint32_t kSoccerMs = 3000, kWordMs = 1800;
constexpr uint32_t kShotMs = 850, kNetMs = 2050, kSlideMs = 2100;

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int lerp(int a, int b, int num, int den) { return den <= 0 ? b : a + (b - a) * clampi(num, 0, den) / den; }
int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int maxc(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return r > g ? (r > b ? r : b) : (g > b ? g : b);
}
int dist(uint32_t a, uint32_t b) {
  return abs((int) ((a >> 16) & 255) - (int) ((b >> 16) & 255)) +
         abs((int) ((a >> 8) & 255) - (int) ((b >> 8) & 255)) + abs((int) (a & 255) - (int) (b & 255));
}
uint32_t lift(uint32_t c, int floor) {
  int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  const int m = maxc(c);
  if (m == 0)
    return 0xFFFFFF;
  if (m < floor) {
    r = r * floor / m;
    g = g * floor / m;
    b = b * floor / m;
  }
  return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
}
// Lost on black, or on the turf
bool vanishes(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  if (maxc(c) < 0x60)
    return true;
  return g > r + 24 && g > b + 24 && g < 0xD8;
}
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

void palette(uint16_t pal[16], const Kit &k, uint32_t seed) {
  const int tone = (int) (hash32(seed) % 3);
  const uint32_t rgb[16] = {0,        0x0A0A0A, kSkin[tone][0], kSkin[tone][1], k.jersey, k.trim,   k.helmet, 0xD0D0D0,
                            k.jersey, kBall[0], 0xF0F0F0,       0xC8A060,       0x6A4A2A, 0x9098A8, 0xFFFFFF, 0x000000};
  for (int i = 0; i < 16; i++)
    pal[i] = rgb565(rgb[i]);
}

// Soccer: H hair, P shorts in the trim, B gloves
void palette_soccer(uint16_t pal[16], const Kit &k, uint32_t seed) {
  palette(pal, k, seed);
  pal[6] = rgb565(kHair[hash32(seed * 7 + 3) % 4]);
  pal[7] = rgb565(k.trim);
  pal[11] = rgb565(0xF0F0F0);
}

// Letters: J fill, T shade; hi is the white flash
void palette_word(uint16_t pal[16], uint16_t hi[16], const Kit &k) {
  const uint32_t shade = dist(k.trim, k.jersey) > 120 ? k.trim : dim_rgb(k.jersey, 50);
  for (int i = 0; i < 16; i++)
    pal[i] = hi[i] = 0;
  pal[4] = rgb565(k.jersey);
  pal[5] = rgb565(shade);
  hi[4] = rgb565(0xFFFFFF);
  hi[5] = rgb565(k.jersey);
}

bool greenish(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  return g > r + 24 && g > b + 24;
}

uint32_t keeper_rgb(uint32_t a, uint32_t b) {
  for (uint32_t c : kKeeperKits)
    if (dist(c, a) >= 200 && dist(c, b) >= 200 && !greenish(c))
      return c;
  return dist(0xF0D020, a) + dist(0xF0D020, b) > dist(0xD040D0, a) + dist(0xD040D0, b) ? 0xF0D020 : 0xD040D0;
}

struct Actor {
  const Sprite *spr{nullptr};
  int x{0}, y{0};
  bool def{false}, flip{false};
  bool keeper{false};
};

struct Stage {
  Actor actors[4];
  int n{0};
  bool ball{false};  // a free ball at (bx, by)
  int bx{0}, by{0}, shade{0};
  int trail_x[2]{}, trail_y[2]{};
  int trails{0};
  int shadow_x{-1};
  int pulse{kEzDim};  // end zone brightness, percent
  int hero_x{0};
  int ball_px{0};      // a baseball's size, 0 a football
  bool spark{false};   // bat meets ball
  bool caption{false};
  int ball_kind{0};    // 1 a soccer ball, else as ball_px says
  int spin{0};         // the ball's frame
  int net_amp{0}, net_y{0};  // the net's bulge and where
  bool in_net{false};  // the ball behind the mesh
};

const Sprite &run_frame(uint32_t t) { return kSprRun[(t / 100) % 4]; }
const Sprite &recv_frame(uint32_t t) { return kSprRecv[(t / 100) % 4]; }

int pulse_at(uint32_t t, uint32_t from) {
  if (t < from)
    return kEzDim;
  static const int kSteps[] = {kEzDim, 40, 50, 60, 60, 50, 40, kEzDim};
  const uint32_t k = (t - from) / 80;
  return k < 8 ? kSteps[k] : kEzDim;
}

// Keeps a 16-wide figure on the stage
int on_stage(const StageGeom &g, int x) { return clampi(x, 7 * g.s, g.w - 9 * g.s); }

void add(Stage &st, const Sprite &spr, int x, int y, bool def, bool flip = false) {
  if (st.n < 4)
    st.actors[st.n++] = Actor{&spr, x, y, def, flip, false};
}

void add_keeper(Stage &st, const Sprite &spr, int x, int y) {
  if (st.n < 4)
    st.actors[st.n++] = Actor{&spr, x, y, true, true, true};
}

int pass_span(const Scene &sc) {
  const int run = sc.g.w - sc.g.ez;
  const int pct = (sc.args.flags & kAnimDeep) ? 85 : (sc.args.flags & kAnimShort) ? 45 : 65;
  return run * pct / 100;
}

int catch_x(const StageGeom &g) { return on_stage(g, g.goal_x + g.ez / 2); }

void ball_arc(const Scene &sc, uint32_t t, int &x, int &y) {
  const StageGeom &g = sc.g;
  const int s = g.s;
  const int qx = on_stage(g, g.goal_x - pass_span(sc)) - 5 * s;
  const int x0 = qx + 8 * s, y0 = g.feet_y - 23 * s;
  const int x1 = catch_x(g), y1 = g.feet_y - 24 * s;
  const int apex = (sc.args.flags & kAnimDeep) ? 2 * s : (sc.args.flags & kAnimShort) ? 20 * s : 10 * s;
  const int u = clampi((int) t - 900, 0, 1500);
  x = x0 + (x1 - x0) * u / 1500;
  const int H = (y0 + y1) / 2 - apex;
  y = y0 + (y1 - y0) * u / 1500 - (int) (4LL * H * u * (1500 - u) / (1500LL * 1500));
}

void stage_pass(const Scene &sc, uint32_t t, Stage &st) {
  const StageGeom &g = sc.g;
  const int s = g.s, feet = g.feet_y;
  const int qx = on_stage(g, g.goal_x - pass_span(sc));
  const int cx = catch_x(g);
  const int rx0 = on_stage(g, qx + 12 * s);
  // Defender: ahead at the snap, beaten by 2,200 ms
  const int dx0 = on_stage(g, rx0 + 14 * s), dx1 = on_stage(g, cx - 14 * s);
  if (t < 2200) {
    add(st, recv_frame(t + 50), lerp(dx0, dx1, (int) t, 2200), feet, true);
  } else {
    const int x = lerp(dx1, dx1 + 6 * s, (int) t - 2200, 250);
    add(st, kSprDive[0], clampi(x, 12 * s, g.w - 13 * s), feet, true);
  }
  // Quarterback: set, drop, wind up, release
  const Sprite *q = &kSprQb[0];
  int qdx = 0;
  if (t < 200) {
  } else if (t < 400) {
    q = &kSprQb[1];
    qdx = 2 * s;
  } else if (t < 600) {
    q = &kSprQb[2];
    qdx = 4 * s;
  } else if (t < 880) {
    q = &kSprQb[3];
    qdx = 5 * s;
  } else if (t < 1300) {
    q = &kSprQb[4];
    qdx = 5 * s;
  } else {
    q = &kSprRecv[3];
    qdx = 5 * s;
  }
  add(st, *q, qx - qdx, feet, false);
  // Receiver: go route, catch, celebrate
  if (t < 300) {
    add(st, kSprRecv[3], rx0, feet, false);
  } else if (t < 2400) {
    add(st, recv_frame(t), lerp(rx0, cx, (int) t - 300, 2100), feet, false);
  } else {
    add(st, kSprRun[4], cx, feet, false);
  }
  st.hero_x = t < 2400 ? lerp(rx0, cx, (int) t - 300, 2100) : cx;
  if (t >= 900 && t < 2400) {
    st.ball = true;
    ball_arc(sc, t, st.bx, st.by);
    st.shade = (int) ((t / 66) % 3);
    st.shadow_x = st.bx;
    if (g.w >= 128)
      for (int k = 1; k <= 2; k++)
        if (t >= 900 + 50u * k) {
          ball_arc(sc, t - 50 * k, st.trail_x[k - 1], st.trail_y[k - 1]);
          st.trails = k;
        }
  } else if (t >= 2400 && t < 2600) {
    st.ball = true;
    st.bx = cx;
    st.by = feet - 24 * s;
  } else if (t >= 2600) {
    // Spike: two bounces in front of him
    const int u = (int) t - 2600;
    st.ball = true;
    st.bx = clampi(cx + 4 * s + u * 6 * s / 600, 0, g.w - 4 * s);
    const int ground = feet - 2 * s;
    const int top = feet - 20 * s;
    int y;
    if (u < 200)
      y = lerp(top, ground, u, 200);
    else if (u < 400)
      y = ground - (int) (4LL * (ground - (feet - 8 * s)) * (u - 200) * (400 - u) / (200 * 200));
    else
      y = ground - (int) (4LL * (ground - (feet - 4 * s)) * (u - 400) * (600 - u) / (200 * 200));
    st.by = y;
    st.shade = (int) ((t / 66) % 3);
    st.shadow_x = st.bx;
  }
  st.pulse = pulse_at(t, 2600);
}

// TD_RUN and TD_RETURN: the carrier's x at t
int carrier_x(const Scene &sc, uint32_t t, int x0, uint32_t go, uint32_t score) {
  const StageGeom &g = sc.g;
  const int end = catch_x(g);
  if (t < go)
    return lerp(x0, x0 + 6 * g.s, (int) t, (int) go);
  if (t < score)
    return lerp(x0 + 6 * g.s, g.goal_x, (int) (t - go), (int) (score - go));
  return lerp(g.goal_x, end, (int) (t - score), 300);
}

void stage_run(const Scene &sc, uint32_t t, Stage &st, bool ret) {
  const StageGeom &g = sc.g;
  const int s = g.s, feet = g.feet_y;
  const uint32_t go = ret ? 0 : 300, score = ret ? 2400 : 2200;
  const int d = sc.args.dist;
  const int span = ret ? g.goal_x : d >= 30 ? g.w * 8 / 10 : (d > 0 && d < 10) ? g.w * 45 / 100 : g.w * 6 / 10;
  const int x0 = on_stage(g, g.goal_x - span);
  const int x = carrier_x(sc, t, x0, go, score);
  // Defender A meets him at 60% and dives behind him
  const uint32_t dive = go + (score - go) * 6 / 10;
  const int meet = carrier_x(sc, dive, x0, go, score) + 16 * s;
  const int da0 = on_stage(g, x0 + (g.goal_x - x0) * 3 / 4);
  if (t < dive) {
    add(st, recv_frame(t), on_stage(g, lerp(da0, meet, (int) t, (int) dive)), feet, true, true);
  } else {
    const int dx = lerp(meet, meet - 12 * s, (int) (t - dive), 300);
    add(st, kSprDive[0], clampi(dx, 12 * s, g.w - 13 * s), feet, true, true);
  }
  // Defender B chases from behind
  const int db = on_stage(g, x0 - 14 * s + (x - x0) * 85 / 100);
  add(st, recv_frame(t + 30), db, feet, true);
  if (!ret) {
    const int qx = on_stage(g, x0 + 8 * s);
    add(st, t < 280 ? kSprQb[0] : kSprRecv[3], qx, feet, false);
  }
  const Sprite &me = (!ret && t < 280) ? kSprRecv[0] : t >= score + 300 ? kSprRun[5] : run_frame(t);
  add(st, me, x, feet, false);
  st.hero_x = x;
  st.pulse = pulse_at(t, score);
}

// Wall, then how far past it the ball lands
int fence_x(const StageGeom &g) { return g.w - clampi(g.w / 10, 6, 24); }
int wall_top(const StageGeom &g) { return g.feet_y - 14 * g.s; }

void hr_arc(const Scene &sc, uint32_t t, int &x, int &y) {
  const StageGeom &g = sc.g;
  const int s = g.s, fx = fence_x(g);
  const int x0 = on_stage(g, 14 * s) + 8 * s, y0 = g.feet_y - 13 * s;
  const int feet = sc.args.dist > 0 ? sc.args.dist : 400;
  int x1, y1;
  if (feet >= 420) {
    x1 = g.w + 12 * s;
    y1 = -8 * s;
  } else if (feet >= 380) {
    x1 = clampi(fx + 8 * s, fx + 3 * s, g.w - 2 * s);
    y1 = (g.crowd_y + g.field_y) / 2;
  } else {
    x1 = clampi(fx + 5 * s, fx + 3 * s, g.w - 2 * s);
    y1 = wall_top(g) - 2 * s;
  }
  const int u = clampi((int) t - 700, 0, 2100);
  x = x0 + (x1 - x0) * u / 2100;
  const int H = (y0 + y1) / 2 - 2 * s;
  y = y0 + (y1 - y0) * u / 2100 - (int) (4LL * H * u * (2100 - u) / (2100LL * 2100));
}

void stage_hr(const Scene &sc, uint32_t t, Stage &st) {
  const StageGeom &g = sc.g;
  const int s = g.s, feet = g.feet_y, fx = fence_x(g);
  const int bx = on_stage(g, 14 * s);
  const int px = on_stage(g, std::max(g.w * 35 / 100, bx + 22 * s));
  // Outfielder: to the wall, leaps and misses
  const int of0 = on_stage(g, g.w * 72 / 100), of1 = on_stage(g, fx - 7 * s);
  if (t < 2300) {
    const Sprite &f = t < 800 ? kSprFielder[1] : kSprFielder[(t / 120) % 2];
    add(st, f, lerp(of0, of1, (int) t - 800, 1500), feet, true);
  } else if (t < 2700) {
    const int u = (int) t - 2300;
    add(st, kSprFielder[2], of1, feet - (int) (4LL * 8 * s * u * (400 - u) / (400 * 400)), true);
  } else {
    add(st, kSprFielder[1], of1, feet, true);
  }
  add(st, t >= 250 && t < 700 ? kSprPitch[1] : kSprPitch[0], px, feet, true, true);
  // Batter: stance, load, swing, watch, flip, trot
  const Sprite *b = &kSprBat[0];
  int x = bx;
  if (t >= 400 && t < 600)
    b = &kSprBat[1];
  else if (t >= 600 && t < 750)
    b = &kSprBat[2];
  else if (t >= 750 && t < 2800)
    b = &kSprBat[3];
  else if (t >= 2800 && t < 3000)
    b = &kSprBat[4];
  else if (t >= 3000) {
    b = &kSprTrot[(t / 150) % 2];
    x = lerp(bx, bx + g.w / 10, (int) t - 3000, 600);
  }
  add(st, *b, x, feet, false);
  st.hero_x = x;
  st.ball_px = 3 * s;
  if (t >= 300 && t < 650) {
    st.ball = true;
    st.bx = lerp(px - 5 * s, bx + 8 * s, (int) t - 300, 350);
    st.by = lerp(feet - 15 * s, feet - 13 * s, (int) t - 300, 350);
  } else if (t >= 650) {
    hr_arc(sc, t, st.bx, st.by);
    st.ball = st.bx < g.w + 3 * s && st.by > -3 * s;
    const int past = st.bx - fx;
    st.ball_px = past < 4 * s ? 3 * s : past < 12 * s ? 2 * s : s;
    st.spark = t < 760;
    if (st.ball && st.bx < fx)
      st.shadow_x = st.bx;
  }
  st.caption = t >= 2800 && sc.args.dist > 0 && g.w >= 128;
}

void hr_caption(const Scene &sc, char *out, size_t n) {
  static const char *const kFields[] = {"", " LEFT", " LEFT CENTER", " CENTER", " RIGHT CENTER", " RIGHT"};
  const int field = (sc.args.flags >> kAnimFieldShift) & 7;
  snprintf(out, n, "%d FT%s", sc.args.dist, sc.g.w >= 192 && field < 6 ? kFields[field] : "");
}

PxBox caption_box(const Scene &sc) {
  char text[32];
  hr_caption(sc, text, sizeof text);
  const int s = sc.g.s, w = px_text_w(text, s);
  return PxBox{(int16_t) ((sc.g.w - w) / 2), (int16_t) (4 * s), (int16_t) w, (int16_t) (5 * s)};
}

// The net's back at row y, before any bulge
int net_back(const StageGeom &g, int y) {
  return g.roof_x + (g.back_x - g.roof_x) * (y - g.bar_y) / (g.feet_y - g.bar_y);
}

// How far the back bulges at row y
int net_push(const StageGeom &g, const Stage &st, int y) {
  const int r = 8 * g.s, dy = abs(y - st.net_y);
  return st.net_amp == 0 || dy >= r ? 0 : st.net_amp * (r - dy) / r;
}

// A ripple from the hit: out, back, out again
int ripple_at(const StageGeom &g, uint32_t t, uint32_t from) {
  static const int kSteps[] = {2, 4, 5, 4, 2, 0, 1, 3, 2, 1};
  if (t < from)
    return 0;
  const uint32_t k = (t - from) / 66;
  return k < sizeof(kSteps) / sizeof(kSteps[0]) ? kSteps[k] * g.s : 0;
}

int kick_x(const StageGeom &g) { return on_stage(g, std::max(16 * g.s, g.w * 16 / 100)); }

// The shot: a rising drive into the top corner
void shot_at(const Scene &sc, uint32_t t, int &x, int &y) {
  const StageGeom &g = sc.g;
  const int s = g.s, U = (int) (kNetMs - kShotMs);
  const int x0 = kick_x(g) + s, y0 = g.feet_y - 2 * s;
  const int y1 = g.bar_y + 5 * s, x1 = net_back(g, y1) - s;
  const int u = clampi((int) t - (int) kShotMs, 0, U);
  x = x0 + (x1 - x0) * u / U;
  y = y0 + (y1 - y0) * u / U - (int) (4LL * 8 * s * u * (U - u) / ((long long) U * U));
}

int keeper_x(const StageGeom &g) { return g.goal_x - 7 * g.s; }

// The dive peaks as the ball passes his hands
uint32_t dive_ms(const Scene &sc) {
  const StageGeom &g = sc.g;
  int x0, x1, y;
  shot_at(sc, kShotMs, x0, y);
  shot_at(sc, kNetMs, x1, y);
  const int hands = keeper_x(g) - 8 * g.s;
  const int u = x1 > x0 ? clampi((hands - x0) * 1000 / (x1 - x0), 0, 1000) : 1000;
  return kShotMs + (kNetMs - kShotMs) * (uint32_t) u / 1000 - 250;
}

void stage_soccer(const Scene &sc, uint32_t t, Stage &st) {
  const StageGeom &g = sc.g;
  const int s = g.s, feet = g.feet_y, kx = kick_x(g), sx = kx - 5 * s;
  const int x0 = on_stage(g, sx - 26 * s);
  // Keeper: set, then a dive under the ball
  const int kp = keeper_x(g);
  const uint32_t dive = dive_ms(sc);
  if (t < dive) {
    add_keeper(st, kSprKpr[SPR_KPR_SET], kp, feet);
  } else {
    const int u = std::min((int) (t - dive), 500);
    const int x = clampi(lerp(kp + 2 * s, kp - 3 * s, u, 500), 12 * s, g.w - 13 * s);
    const int up = 3 * s * (500 - u) / 500 + (int) (4LL * 7 * s * u * (500 - u) / (500 * 500));
    add_keeper(st, kSprKpr[SPR_KPR_DIVE], x, feet - up);
  }
  // A defender chases, a step too late
  if (g.w >= 128) {
    const int d1 = on_stage(g, sx - 13 * s), d0 = on_stage(g, x0 - 14 * s);
    if (t < kShotMs)
      add(st, kSprSoc[(t / 100 + 2) % 4], lerp(d0, d1, (int) t, (int) kShotMs), feet, true);
    else
      add(st, kSprSoc[SPR_SOC_STAND], d1, feet, true);
  }
  // Striker: runs onto it, strikes, slides on his knees
  const int slide = clampi(g.w / 6, 8 * s, 30 * s);
  int x = sx;
  const Sprite *me = &kSprSoc[SPR_SOC_STAND];
  if (t < 750) {
    x = lerp(x0, sx, (int) t, 750);
    me = &kSprSoc[(t / 100) % 4];
  } else if (t < kShotMs) {
    me = &kSprSoc[SPR_SOC_PLANT];
  } else if (t < kShotMs + 300) {
    me = &kSprSoc[SPR_SOC_KICK];
  } else if (t >= kSlideMs && t < kSlideMs + 400) {
    const int u = (int) (t - kSlideMs);
    x = sx + slide - slide * (400 - u) * (400 - u) / (400 * 400);
    me = &kSprSoc[SPR_SOC_SLIDE];
  } else if (t >= kSlideMs) {
    x = sx + slide;
    me = &kSprSoc[((t - kSlideMs) / 300) % 2 ? SPR_SOC_SLIDE : SPR_SOC_CHEER];
  }
  add(st, *me, x, feet, false);
  st.hero_x = x;
  // The ball: rolls in, flies, bulges the net, drops
  st.ball = true;
  st.ball_kind = 1;
  if (t < kShotMs) {
    const int u = std::min((int) t, 750);
    st.bx = lerp(x0 + 9 * s, kx, 750 * 750 - (750 - u) * (750 - u), 750 * 750);
    st.by = feet - 2 * s;
    st.spin = (st.bx / (2 * s)) & 3;
    return;
  }
  if (t < kNetMs) {
    shot_at(sc, t, st.bx, st.by);
    st.spin = (int) ((t / 50) & 3);
    st.shadow_x = st.bx;
    if (g.w >= 128)
      for (int k = 1; k <= 2; k++)
        if (t >= kShotMs + 50u * k) {
          shot_at(sc, t - 50 * k, st.trail_x[k - 1], st.trail_y[k - 1]);
          st.trails = k;
        }
    st.in_net = st.bx > g.goal_x + s;
    return;
  }
  int x1, y1;
  shot_at(sc, kNetMs, x1, y1);
  const int u = std::min((int) (t - kNetMs), 500), rest = feet - 2 * s;
  st.bx = lerp(x1, (g.goal_x + g.back_x) / 2 + s, u, 500);
  if (u < 300)
    st.by = y1 + (rest - y1) * u * u / (300 * 300);
  else
    st.by = rest - (int) (4LL * 3 * s * (u - 300) * (500 - u) / (200 * 200));
  st.spin = u < 500 ? (int) ((t / 80) & 3) : 0;
  st.in_net = true;
  st.net_amp = ripple_at(g, t, kNetMs);
  st.net_y = y1;
}

void stage_at(const Scene &sc, uint32_t t, Stage &st) {
  switch (sc.args.kind) {
    case AnimKind::TD_PASS:
      stage_pass(sc, t, st);
      break;
    case AnimKind::TD_RUN:
      stage_run(sc, t, st, false);
      break;
    case AnimKind::TD_RETURN:
      stage_run(sc, t, st, true);
      break;
    case AnimKind::HOME_RUN:
      stage_hr(sc, t, st);
      break;
    case AnimKind::SOCCER_GOAL:
      stage_soccer(sc, t, st);
      break;
    default:
      break;
  }
}

// ---- The GOAL word ----

uint32_t play_ms(AnimKind k) { return k == AnimKind::SOCCER_GOAL ? kSoccerMs : 0; }

struct WordAt {
  int n{0}, x0{0}, y0{0}, step{0};
  uint32_t t0{0};
};

WordAt word_of(const Scene &sc) {
  WordAt w;
  const StageGeom &g = sc.g;
  w.n = word_letters(g);
  w.x0 = (g.w - (w.n * 10 - 1) * g.s) / 2;
  w.y0 = (g.crowd_y - 16 * g.s) / 2;
  w.step = std::min(90, 900 / w.n);
  w.t0 = play_ms(sc.args.kind);
  return w;
}

constexpr uint32_t kPopMs = 66, kWordWinMs = 200, kPulseMs = 1150;

// 0 hidden, 1 settled, 2 and 3 popping in
int letter_state(const WordAt &w, int i, uint32_t t) {
  const uint32_t at = w.t0 + (uint32_t) (i * w.step);
  if (t < at)
    return 0;
  if (t - at < kPopMs / 2)
    return 3;
  return t - at < kPopMs ? 2 : 1;
}

// White steps of the closing pulse
bool word_white(const WordAt &w, uint32_t t) {
  if (t < w.t0 + kPulseMs)
    return false;
  const uint32_t k = (t - w.t0 - kPulseMs) / 100;
  return k == 0 || k == 2;
}

bool word_pulsing(const WordAt &w, uint32_t t) { return t >= w.t0 + kPulseMs && t < w.t0 + kPulseMs + 300; }

PxBox letter_box(const StageGeom &g, const WordAt &w, int i, int j) {
  const int s = g.s;
  return PxBox{(int16_t) (w.x0 + i * 10 * s), (int16_t) (w.y0 - 2 * s), (int16_t) (((j - i) * 10 + 9) * s),
               (int16_t) (18 * s)};
}

// What changed in the word near t, on the picture
PxBox word_box(const Scene &sc, uint32_t t) {
  if (play_ms(sc.args.kind) == 0)
    return PxBox{};
  const WordAt w = word_of(sc);
  if (t < w.t0)
    return PxBox{};
  if (word_pulsing(w, t))
    return letter_box(sc.g, w, 0, w.n - 1);
  int i0 = -1, i1 = -1;
  for (int i = 0; i < w.n; i++) {
    const uint32_t at = w.t0 + (uint32_t) (i * w.step);
    if (t >= at && t - at < kWordWinMs) {
      if (i0 < 0)
        i0 = i;
      i1 = i;
    }
  }
  return i0 < 0 ? PxBox{} : letter_box(sc.g, w, i0, i1);
}

PxBox ball_box(const StageGeom &g, int x, int y) {
  return PxBox{(int16_t) (x - 2 * g.s), (int16_t) (y - 1 * g.s), (int16_t) (5 * g.s), (int16_t) (3 * g.s)};
}

uint32_t crowd_rgb(const Scene &sc, int lx, int y) {
  const int s = sc.g.s;
  const uint32_t hv = hash32((uint32_t) (lx / s) * 0x9E3779B1u ^ (uint32_t) (y / s) * 0x85EBCA77u ^ sc.seed);
  if (hv % 100 < 12)
    return (hv >> 8) & 1 ? sc.crowd_a : sc.crowd_b;
  return 0;
}

// Ballpark: wall in green, yellow top, stands past it
uint32_t park_rgb(const Scene &sc, int lx, int y) {
  const StageGeom &g = sc.g;
  const int s = g.s, fx = fence_x(g), top = wall_top(g);
  if (y < g.crowd_y)
    return 0;
  if (lx >= fx && lx < fx + 3 * s && y >= top)
    return y < top + s ? kPost : 0x283850;
  if (y < g.field_y || lx >= fx + 3 * s)
    return y == g.field_y - 1 && lx < fx ? kRail : crowd_rgb(sc, lx, y);
  if (lx < 30 * s && y >= g.feet_y - 3 * s)
    return 0x6A4424;
  const int u = lx - floordiv(g.feet_y - y, 3 * s);
  return floordiv(u, g.stripe) & 1 ? kTurfB : kTurfA;
}

// Pitch: a goal box, the goal line and the net
uint32_t pitch_rgb(const Scene &sc, int lx, int y, const Stage &st) {
  const StageGeom &g = sc.g;
  const int s = g.s, post = g.goal_x;
  if (lx >= post && lx < post + s && y >= g.bar_y && y < g.feet_y + s)
    return kGoalFrame;
  if (y >= g.bar_y && y < g.bar_y + s && lx >= post && lx <= g.roof_x)
    return kGoalFrame;
  uint32_t c;
  if (y < g.crowd_y) {
    c = 0;
  } else if (y < g.field_y) {
    c = y == g.field_y - 1 ? kRail : crowd_rgb(sc, lx, y);
  } else {
    const int u = lx - floordiv(g.feet_y - y, 3 * s), box = post - clampi(g.w / 6, 12 * s, 40 * s);
    const bool line = (u >= post && u < post + s) || (u >= box && u < box + s && y >= g.field_y + 2 * s) ||
                      (y >= g.field_y + 2 * s && y < g.field_y + 3 * s && u >= box && u < post);
    c = line ? kPitchLine : floordiv(u, g.stripe) & 1 ? kTurfB : kTurfA;
  }
  if (y <= g.bar_y || y > g.feet_y || lx <= post)
    return c;
  // Inside the net, stretched where it bulges
  const int edge0 = net_back(g, y), edge = edge0 + net_push(g, st, y);
  if (lx > edge)
    return c;
  if (lx == edge)
    return kMesh;
  const int ux = post + (lx - post) * (edge0 - post) / (edge - post);
  const int mx = (ux - post) / s, my = (y - g.bar_y) / s;
  if (((mx + my) & 3) == 0 || ((mx - my + 4096) & 3) == 0)
    return kMesh;
  return dim_rgb(c, 45);
}

uint32_t bg_rgb(const Scene &sc, int lx, int y, const Stage &st) {
  const StageGeom &g = sc.g;
  const int s = g.s, pulse = st.pulse;
  if (sc.args.kind == AnimKind::HOME_RUN)
    return park_rgb(sc, lx, y);
  if (sc.args.kind == AnimKind::SOCCER_GOAL)
    return pitch_rgb(sc, lx, y, st);
  // Goal post at the end line, uprights into the sky
  const int post = g.w - 3 * s, bar_y = g.crowd_y - 2 * s;
  if (lx >= post && lx < post + s && y >= bar_y && y < g.field_y + 6 * s)
    return kPost;
  if (y >= bar_y && y < bar_y + s && lx >= post - 3 * s && lx < post + 3 * s)
    return kPost;
  if (y >= 3 * s && y < bar_y && ((lx >= post - 3 * s && lx < post - 2 * s) || (lx >= post + 2 * s && lx < post + 3 * s)))
    return kPost;
  if (y < g.crowd_y)
    return 0;
  if (y < g.field_y)
    return y == g.field_y - 1 ? kRail : crowd_rgb(sc, lx, y);
  // Lines lean one pixel every three rows
  const int u = lx - floordiv(g.feet_y - y, 3 * s);
  if (u >= g.goal_x && u < g.goal_x + s)
    return kGoalLine;
  if (u >= g.goal_x)
    return dim_rgb(sc.ez_rgb, pulse);
  const int back = g.goal_x - u;
  if (back % (2 * g.stripe) < s)
    return kTurfLine;
  return floordiv(back, g.stripe) & 1 ? kTurfB : kTurfA;
}

// The spinning ball, hidden by the net's mesh
void soccer_ball(Px565 &p, const Scene &sc, const Stage &st) {
  const Sprite &b = kSprSball[st.spin & 3];
  const int s = sc.g.s;
  const PxBox box = sprite_box(b, st.bx, st.by, s, false);
  for (int sy = 0; sy < b.h; sy++)
    for (int sx = 0; sx < b.w; sx++) {
      const int i = sy * b.w + sx;
      const uint8_t v = (b.px[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
      if (v == 0)
        continue;
      const int x = box.x + sx * s, y = box.y + sy * s;
      if (st.in_net && pitch_rgb(sc, x, y, st) == kMesh)
        continue;
      p.fill(x, y, s, s, sc.off[v]);
    }
}

// Left to right even when mirrored
void draw_word(Px565 &p, const Scene &sc, uint32_t t) {
  if (play_ms(sc.args.kind) == 0 || t < play_ms(sc.args.kind))
    return;
  const WordAt w = word_of(sc);
  const bool m = p.mirror, white = word_white(w, t);
  p.mirror = false;
  const int s = sc.g.s;
  for (int i = 0; i < w.n; i++) {
    const int k = letter_state(w, i, t);
    if (k == 0)
      break;
    const uint16_t *pal = white || k >= 2 ? sc.word_hi : sc.word;
    const int lift = k >= 2 ? (k - 1) * s : 0;
    px_sprite(p, kSprWord[word_glyph(i, w.n)], w.x0 + i * 10 * s, w.y0 - lift, pal, s, false);
  }
  p.mirror = m;
}

}  // namespace

StageGeom stage_geom(int w, int h) {
  StageGeom g;
  g.w = (int16_t) w;
  g.h = (int16_t) h;
  g.s = (int16_t) (h >= 128 && w >= 128 ? 2 : 1);
  if (g.s == 2) {
    g.crowd_y = (int16_t) (h * 56 / 128);
    g.field_y = (int16_t) (h * 72 / 128);
  } else {
    g.crowd_y = (int16_t) (h * 22 / 64);
    g.field_y = (int16_t) (h * 34 / 64);
  }
  g.feet_y = (int16_t) (h - 4 * g.s);
  const int ez = clampi(w / 8, 8, 28);
  g.ez = (int16_t) (ez > 10 * g.s ? ez : 10 * g.s);
  g.goal_x = (int16_t) (w - g.ez);
  g.stripe = (int16_t) clampi((w - g.ez) / 12, 4 * g.s, 20);
  return g;
}

Kit kit_colors(uint32_t primary, uint32_t alt) {
  Kit k{primary, 0xFFFFFF, 0xA8A8A8};
  const bool alt_ok = alt != 0 && !vanishes(alt);
  if (vanishes(primary)) {
    const uint32_t lifted = lift(primary, 0xC0);
    if (alt_ok && dist(alt, 0xFFFFFF) > 60)
      k.jersey = alt;
    else if (!vanishes(lifted))
      k.jersey = lifted;
    else
      k.jersey = 0xFFFFFF;
  }
  if (alt_ok && dist(alt, k.jersey) > 120)
    k.trim = alt;
  else if (dist(k.jersey, 0xFFFFFF) < 120)
    k.trim = lift(primary, 0xC0) != k.jersey && !vanishes(lift(primary, 0xC0)) ? lift(primary, 0xC0) : 0x404040;
  k.helmet = dist(k.trim, k.jersey) > 120 ? k.trim : 0xA8A8A8;
  return k;
}

Kit kit_against(const Kit &mine, uint32_t primary, uint32_t alt) {
  Kit k = kit_colors(primary, alt);
  if (dist(k.jersey, mine.jersey) < 160) {
    const bool light = dist(mine.jersey, 0xFFFFFF) < 160;
    k.jersey = light ? 0xC02828 : 0xFFFFFF;
    k.trim = light ? 0xFFFFFF : mine.jersey;
    k.helmet = light ? 0xFFFFFF : 0x606060;
  }
  return k;
}

int word_letters(const StageGeom &g) { return clampi((g.w - 2 * g.s) / (10 * g.s), 4, 40); }

int word_glyph(int i, int n) {
  const int rest = n - 1, l = std::max(1, rest * 25 / 100), a = std::max(1, rest * 30 / 100);
  const int o = rest - a - l;
  return i <= 0 ? 0 : i <= o ? 1 : i <= o + a ? 2 : 3;
}

Scene scene_make(const AnimArgs &a, int w, int h, uint32_t color, uint32_t alt, uint32_t other, uint32_t other_alt,
                 uint32_t seed) {
  Scene sc;
  sc.args = a;
  if (a.kind == AnimKind::HOME_RUN)
    sc.args.flags &= (uint8_t) ~kAnimMirror;
  sc.g = stage_geom(w, h);
  sc.seed = seed;
  const Kit off = kit_colors(color, alt);
  const Kit def = kit_against(off, other, other_alt);
  palette(sc.off, off, seed);
  palette(sc.def, def, seed + 1);
  if (a.kind == AnimKind::SOCCER_GOAL) {
    StageGeom &g = sc.g;
    const int s = g.s, d = clampi(w / s / 14, 7, 14) * s;
    g.back_x = (int16_t) (w - 2 * s);
    g.goal_x = (int16_t) (g.back_x - d);
    g.roof_x = (int16_t) (g.goal_x + d / 2);
    g.bar_y = (int16_t) (g.feet_y - 28 * s);
    g.ez = (int16_t) (w - g.goal_x);
    palette_soccer(sc.off, off, seed);
    palette_soccer(sc.def, def, seed + 1);
    Kit k{keeper_rgb(off.jersey, def.jersey), 0x202020, 0};
    palette_soccer(sc.keeper, k, seed + 2);
  }
  palette_word(sc.word, sc.word_hi, off);
  sc.ez_rgb = off.jersey;
  sc.crowd_a = dim_rgb(off.jersey, 40);
  sc.crowd_b = dim_rgb(def.jersey, 40);
  return sc;
}

uint32_t scene_ms(AnimKind k) {
  switch (k) {
    case AnimKind::TD_PASS:
      return 3200;
    case AnimKind::TD_RUN:
      return 2800;
    case AnimKind::TD_RETURN:
      return 3000;
    case AnimKind::HOME_RUN:
      return 3600;
    case AnimKind::SOCCER_GOAL:
      return kSoccerMs + kWordMs;
    default:
      return 0;
  }
}

int scene_boxes(const Scene &sc, uint32_t t, PxBox *out) {
  Stage st;
  stage_at(sc, t, st);
  Px565 frame;
  frame.w = sc.g.w;
  frame.h = sc.g.h;
  frame.mirror = (sc.args.flags & kAnimMirror) != 0;
  int n = 0;
  const int s = sc.g.s;
  for (int i = 0; i < st.n; i++) {
    const Actor &a = st.actors[i];
    out[n++] = px_phys(frame, sprite_box(*a.spr, a.x, a.y, s, a.flip));
  }
  if (st.ball && st.ball_kind == 1) {
    PxBox b = sprite_box(kSprSball[0], st.bx, st.by, s, false);
    for (int k = 0; k < st.trails; k++)
      b = box_join(b, PxBox{(int16_t) st.trail_x[k], (int16_t) st.trail_y[k], (int16_t) s, (int16_t) s});
    out[n++] = px_phys(frame, b);
  } else if (st.ball && st.ball_px > 0) {
    out[n++] = px_phys(frame, PxBox{(int16_t) (st.bx - 3 * s), (int16_t) (st.by - 3 * s), (int16_t) (7 * s),
                                    (int16_t) (7 * s)});
  } else if (st.ball) {
    PxBox b = ball_box(sc.g, st.bx, st.by);
    for (int k = 0; k < st.trails; k++)
      b = box_join(b, PxBox{(int16_t) st.trail_x[k], (int16_t) st.trail_y[k], (int16_t) s, (int16_t) s});
    out[n++] = px_phys(frame, b);
  }
  if (st.shadow_x >= 0)
    out[n++] = px_phys(frame, PxBox{(int16_t) (st.shadow_x - s), (int16_t) (sc.g.feet_y + s), (int16_t) (3 * s),
                                    (int16_t) s});
  if (st.caption)
    out[n++] = px_phys(frame, caption_box(sc));
  if (st.pulse != kEzDim)
    out[n++] = px_phys(frame, PxBox{(int16_t) (sc.g.goal_x - sc.g.h / 3), sc.g.field_y, (int16_t) (sc.g.ez + sc.g.h / 3),
                                    (int16_t) (sc.g.h - sc.g.field_y)});
  if (st.net_amp != 0)
    out[n++] = px_phys(frame, PxBox{sc.g.goal_x, (int16_t) (st.net_y - 8 * s),
                                    (int16_t) (sc.g.back_x - sc.g.goal_x + 6 * s), (int16_t) (16 * s + 1)});
  out[n++] = word_box(sc, t);
  int m = 0;
  for (int i = 0; i < n; i++)
    if (!out[i].empty())
      out[m++] = out[i];
  return m;
}

void scene_draw(Px565 &p, const Scene &sc, uint32_t t) {
  Stage st;
  stage_at(sc, t, st);
  p.mirror = (sc.args.flags & kAnimMirror) != 0;
  const PxBox c = box_clip(p.clip, PxBox{0, 0, p.w, p.h});
  for (int y = c.y; y < c.y + c.h; y++)
    for (int x = c.x; x < c.x + c.w; x++)
      p.px[y * p.stride + x] = rgb565(bg_rgb(sc, p.mirror ? p.w - 1 - x : x, y, st));
  const int s = sc.g.s;
  if (st.shadow_x >= 0)
    p.fill(st.shadow_x - s, sc.g.feet_y + s, 3 * s, s, rgb565(kShadow));
  for (int i = 0; i < st.n; i++)
    if (st.actors[i].def)
      px_sprite(p, *st.actors[i].spr, st.actors[i].x, st.actors[i].y, st.actors[i].keeper ? sc.keeper : sc.def, s,
                st.actors[i].flip);
  for (int i = 0; i < st.n; i++)
    if (!st.actors[i].def)
      px_sprite(p, *st.actors[i].spr, st.actors[i].x, st.actors[i].y, sc.off, s, st.actors[i].flip);
  if (st.ball && st.ball_kind == 1) {
    for (int k = st.trails - 1; k >= 0; k--)
      p.fill(st.trail_x[k], st.trail_y[k], s, s, rgb565(k ? 0x404040 : 0x808080));
    soccer_ball(p, sc, st);
  } else if (st.ball && st.ball_px > 0) {
    const int d = st.ball_px, x = st.bx - d / 2, y = st.by - d / 2;
    p.fill(x, y, d, d, rgb565(0xF0F0F0));
    if (d >= 3 * s) {
      const uint16_t edge = rgb565(0x909090);
      for (int cx : {x, x + d - s})
        for (int cy : {y, y + d - s})
          p.fill(cx, cy, s, s, edge);
    }
    if (st.spark)
      for (int k = -2; k <= 2; k++) {
        p.fill(st.bx + k * s, st.by, s, s, rgb565(0xFFF080));
        p.fill(st.bx, st.by + k * s, s, s, rgb565(0xFFF080));
      }
  } else if (st.ball) {
    for (int k = st.trails - 1; k >= 0; k--)
      p.fill(st.trail_x[k], st.trail_y[k], s, s, rgb565(dim_rgb(kBall[0], k ? 30 : 55)));
    const uint16_t c0 = rgb565(kBall[st.shade]), lace = rgb565(0xF0F0F0);
    const int x = st.bx - 2 * s, y = st.by - s;
    p.fill(x + s, y, 3 * s, s, c0);
    p.fill(x, y + s, 5 * s, s, c0);
    p.fill(x + s, y + 2 * s, 3 * s, s, c0);
    if (st.shade != 2)
      p.fill(x + 2 * s, y + s, s, s, lace);
  }
  if (st.caption) {
    char text[32];
    hr_caption(sc, text, sizeof text);
    const PxBox b = caption_box(sc);
    px_text(p, b.x, b.y, text, rgb565(0xFFD000), s);
  }
  draw_word(p, sc, t);
}

ScenePose scene_pose(const Scene &sc, uint32_t t) {
  Stage st;
  stage_at(sc, t, st);
  ScenePose r;
  r.hero_x = (int16_t) st.hero_x;
  r.ball_free = st.ball;
  if (play_ms(sc.args.kind) != 0 && t >= play_ms(sc.args.kind)) {
    const WordAt w = word_of(sc);
    r.word = letter_box(sc.g, w, 0, w.n - 1);
    r.flash = word_pulsing(w, t);
  }
  if (st.ball) {
    r.ball_x = (int16_t) st.bx;
    r.ball_y = (int16_t) st.by;
  }
  return r;
}

}  // namespace espn
