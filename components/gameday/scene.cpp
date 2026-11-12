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

struct Actor {
  const Sprite *spr{nullptr};
  int x{0}, y{0};
  bool def{false}, flip{false};
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
    st.actors[st.n++] = Actor{&spr, x, y, def, flip};
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
    default:
      break;
  }
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

uint32_t bg_rgb(const Scene &sc, int lx, int y, int pulse) {
  const StageGeom &g = sc.g;
  const int s = g.s;
  if (sc.args.kind == AnimKind::HOME_RUN)
    return park_rgb(sc, lx, y);
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
  if (st.ball && st.ball_px > 0) {
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
      p.px[y * p.stride + x] = rgb565(bg_rgb(sc, p.mirror ? p.w - 1 - x : x, y, st.pulse));
  const int s = sc.g.s;
  if (st.shadow_x >= 0)
    p.fill(st.shadow_x - s, sc.g.feet_y + s, 3 * s, s, rgb565(kShadow));
  for (int i = 0; i < st.n; i++)
    if (st.actors[i].def)
      px_sprite(p, *st.actors[i].spr, st.actors[i].x, st.actors[i].y, sc.def, s, st.actors[i].flip);
  for (int i = 0; i < st.n; i++)
    if (!st.actors[i].def)
      px_sprite(p, *st.actors[i].spr, st.actors[i].x, st.actors[i].y, sc.off, s, st.actors[i].flip);
  if (st.ball && st.ball_px > 0) {
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
}

ScenePose scene_pose(const Scene &sc, uint32_t t) {
  Stage st;
  stage_at(sc, t, st);
  ScenePose r;
  r.hero_x = (int16_t) st.hero_x;
  r.ball_free = st.ball;
  if (st.ball) {
    r.ball_x = (int16_t) st.bx;
    r.ball_y = (int16_t) st.by;
  }
  return r;
}

}  // namespace espn
