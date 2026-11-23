// The Flight Tracker's moving sky (sky.h): drawn for this project.
#include "sky.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include <cmath>
#include <cstring>

#include "flights.h"

namespace espn {

namespace {

using flights::PlaneKind;
using flights::PlaneMotion;
using flights::PlaneSprite;

// The seed and slot k stirred into 32 bits
uint32_t stir(uint32_t seed, uint32_t k) {
  uint32_t x = seed ^ (k * 0x9E3779B9u);
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// From the seed for slot k: 0 to n - 1.
uint32_t pick(uint32_t seed, uint32_t k, uint32_t n) { return n > 1 ? stir(seed, k) % n : 0; }

// The tail's row: the leftmost ink below the fin's top third.
int tail_row(const PlaneSprite &sp) {
  int row = 0, left = sp.w;
  for (int y = sp.h / 3; y < sp.h; y++)
    for (int x = 0; x < left; x++)
      if (flights::sprite_px(sp, x, y) != 0) {
        left = x;
        row = y;
        break;
      }
  return row;
}

// A propeller pixel's turn: all, the middle, the tips, the middle.
bool prop_on(const PlaneSprite &sp, const flights::PropSpot &ps, int y, uint32_t frame) {
  int n = 0, j = 0;
  for (int r = ps.y0; r <= ps.y1; r++)
    if (flights::sprite_px(sp, ps.x, r) == flights::kArtDetail) {
      if (r < y)
        j++;
      n++;
    }
  if (frame == 0)
    return true;
  if (frame == 2)
    return j == 0 || j == n - 1;
  return n % 2 ? j == n / 2 : j == n / 2 - 1 || j == n / 2;
}

const uint8_t kRotorHalfs[4] = {9, 6, 3, 6};
// Birds glide a pixel this often
const uint32_t kBirdMs = 250;

// The sun's elevations the sky's colors are keyed at
const int8_t kElev[7] = {-18, -9, -3, 0, 6, 15, 45};
// Zenith, mid, low and horizon at each, evening side
const uint8_t kEve[7][4][3] = {
    {{2, 4, 24}, {4, 8, 36}, {6, 14, 52}, {12, 24, 72}},
    {{4, 6, 32}, {16, 12, 56}, {56, 20, 72}, {120, 40, 60}},
    {{6, 12, 52}, {48, 22, 88}, {164, 44, 64}, {236, 104, 32}},
    {{10, 24, 84}, {88, 40, 104}, {224, 76, 44}, {255, 160, 36}},
    {{8, 48, 124}, {36, 84, 156}, {204, 128, 72}, {255, 190, 64}},
    {{6, 58, 148}, {16, 88, 168}, {44, 122, 184}, {96, 158, 200}},
    {{2, 54, 158}, {8, 82, 178}, {26, 114, 194}, {66, 148, 210}},
};
// Morning's low and horizon, keys 1 to 4
const uint8_t kMorn[4][2][3] = {
    {{48, 20, 80}, {112, 44, 84}},
    {{150, 52, 96}, {232, 112, 96}},
    {{214, 92, 96}, {255, 170, 110}},
    {{190, 130, 110}, {255, 200, 120}},
};
// Under cloud the sky leans to these
const int8_t kSlateElev[4] = {-18, -9, 6, 15};
const uint32_t kSlate[4] = {0x0A0E22, 0x383254, 0x383254, 0x3E5070};
// Each scenery's land and its lit top by day
const uint32_t kLandDay[5][2] = {
    {0x0A4014, 0x247424}, {0x1E2C54, 0x46608C}, {0x283048, 0x404C68}, {0x0A4014, 0x247424}, {0x0E3C10, 0x3C8228}};
const uint32_t kLandDusk = 0x1A0A28, kRimDusk = 0x3C1838, kRimNight = 0x0A0E28, kFarNight = 0x080A1E;
const uint32_t kLamp = 0xFF9628, kWindow = 0xFFAA28, kMast = 0xDC1E14;
const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
// Scenery columns start here, clear of zero
const uint32_t kWorld = 0x40000000u;

int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// a toward b by t of 256, each channel
uint32_t mix(uint32_t a, uint32_t b, int t) {
  const uint32_t u = (uint32_t) (256 - t), v = (uint32_t) t;
  return ((((a & 0xFF00FF) * u + (b & 0xFF00FF) * v) >> 8) & 0xFF00FF) |
         ((((a & 0x00FF00) * u + (b & 0x00FF00) * v) >> 8) & 0x00FF00);
}

uint32_t rgb_of(const uint8_t c[3]) { return ((uint32_t) c[0] << 16) | ((uint32_t) c[1] << 8) | c[2]; }

// RGB565 an LED lights: channels off or 8 up.
// d the dither step, 0 to 15; 8 rounds.
uint16_t enc(uint32_t c, int d) {
  const int r = (int) ((c >> 16) & 255), g = (int) ((c >> 8) & 255), b = (int) (c & 255);
  int r5 = r < 8 ? 0 : ((r * 498 >> 8) + d) >> 4, g6 = g < 8 ? 0 : ((g * 1012 >> 8) + d) >> 4;
  int b5 = b < 8 ? 0 : ((b * 498 >> 8) + d) >> 4;
  if (r >= 8 && r5 < 1)
    r5 = 1;
  if (g >= 8 && g6 < 2)
    g6 = 2;
  if (b >= 8 && b5 < 1)
    b5 = 1;
  return (uint16_t) ((r5 > 31 ? 31 : r5) << 11 | (g6 > 63 ? 63 : g6) << 5 | (b5 > 31 ? 31 : b5));
}

uint32_t dec(uint16_t c) {
  const uint32_t r = c >> 11, gr = (c >> 5) & 63, b = c & 31;
  return ((r << 3 | r >> 2) << 16) | ((gr << 2 | gr >> 4) << 8) | (b << 3 | b >> 2);
}

// One paint's scene; the loop alone paints
struct Scene {
  uint32_t stop[4];  // zenith, mid, low, horizon
  uint32_t near, rim, home;
  uint16_t *px;
  int16_t w, h, stride, hy, amp, gx;
  int32_t fade;  // the glow's falloff a column, of 65536
  int8_t elev;
  uint8_t land, floor;
  bool glow, fog;
};
Scene g;

uint16_t &at(int x, int y) { return g.px[y * g.stride + x]; }
bool inside(int x, int y) { return x >= 0 && y >= 0 && x < g.w && y < g.h; }
// The pixel toward c by t of 256
void blend(int x, int y, uint32_t c, int t) {
  if (inside(x, y))
    at(x, y) = enc(mix(dec(at(x, y)), c, t), 8);
}

// b's keys at elev into out, n of them
int key_at(const int8_t *keys, int n, int elev, int &t) {
  int i = 0;
  while (i < n - 2 && elev >= keys[i + 1])
    i++;
  t = clampi((elev - keys[i]) * 256 / (keys[i + 1] - keys[i]), 0, 256);
  return i;
}

uint32_t sky_key(int i, int j, bool am) {
  return rgb_of(am && j >= 2 && i >= 1 && i <= 4 ? kMorn[i - 1][j - 2] : kEve[i][j]);
}

// Smoothstepped value noise, 0 to 255, wl columns a wave
int vnoise(uint32_t k, uint32_t wx, int wl) {
  const uint32_t i = wx / (uint32_t) wl;
  const int a = (int) pick(g.home, k * 4099u + i, 256), b = (int) pick(g.home, k * 4099u + i + 1, 256);
  int u = (int) (wx % (uint32_t) wl) * 256 / wl;
  u = u * u * (768 - 2 * u) >> 16;
  return a + (b - a) * u / 256;
}

// A length at 21 rows, grown with the box
int grown(int n) { return g.h > 21 ? n * g.h / 21 : n; }

// Rows the near land rises over its base at wx
int near_h(uint32_t wx) {
  const int amp = g.amp;
  switch (g.land) {
    case 1: {
      const int n = vnoise(3, wx, grown(16)), ridged = 255 - (2 * n > 255 ? 2 * n - 255 : 255 - 2 * n);
      return (amp * (200 * ridged + 56 * vnoise(4, wx, grown(5))) + 32768) >> 16;
    }
    case 2: {
      // Blocks 3 to 6 wide, taller downtown
      const int k = grown(1), cw = 6 * k;
      const uint32_t c = wx / (uint32_t) cw;
      if ((int) (wx % (uint32_t) cw) >= (3 + (int) pick(g.home, 9000 + c, 4)) * k)
        return 1;
      const int e = vnoise(5, c * (uint32_t) cw, grown(44));
      return 2 + (amp * 5 / 4 - 2) * (96 + e * 200 / 256) * (130 + (int) pick(g.home, 9100 + c, 127)) / (296 * 256);
    }
    case 3: {
      // A headland over two fifths of the shore
      const int n = vnoise(6, wx, grown(48));
      return n <= 150 ? 0 : 1 + (amp * (n - 150) * (160 + vnoise(7, wx, grown(7))) >> 15);
    }
    case 4: {
      // Flat, a tree every 30 to 50 columns
      const int k = grown(1), cw = 40 * k;
      const int f = (int) (wx % (uint32_t) cw) - (int) pick(g.home, 9900 + wx / (uint32_t) cw, (uint32_t) (20 * k));
      const int tree = f < 0 || f >= 3 * k ? 0 : f / k == 1 ? 3 * k : 2 * k;
      return (vnoise(8, wx, grown(37)) > 150) + tree;
    }
    default: {
      const int n = (166 * vnoise(1, wx, grown(9)) + 90 * vnoise(2, wx, grown(4))) >> 8;
      return (amp * clampi((n - 56) * 2, 0, 255) + 128) >> 8;
    }
  }
}

// The far ridge's rows, hazed behind the near land
int far_h(uint32_t wx) {
  if (g.h < 21)
    return 0;
  const int n = (166 * vnoise(11, wx, grown(20)) + 90 * vnoise(12, wx, grown(9))) >> 8;
  const int amp = g.land == 1 ? g.amp * 3 / 2 : g.amp;
  return amp / 3 + (amp * n >> 8);
}

// A sky row's color by the sun, and far off
void sky_row(int y, uint32_t &sun, uint32_t &far) {
  const int span = g.hy > 1 ? g.hy - 1 : 1, f = y >= span ? 256 : y * 256 / span;
  const int i = f < 115 ? 0 : f < 200 ? 1 : 2;
  static const int16_t kAt[4] = {0, 115, 200, 256};
  sun = mix(g.stop[i], g.stop[i + 1], (f - kAt[i]) * 256 / (kAt[i + 1] - kAt[i]));
  // Away from the sun the warm rows cool
  far = g.glow && i > 0 ? g.stop[1] : sun;
  // Never past the plane's white, up high
  const int cap = f < 200 ? 200 : 255;
  for (uint32_t *c : {&sun, &far}) {
    if (g.fog && y >= g.h / 2)
      *c = mix(*c, g.stop[3], 128);
    int r = (int) (*c >> 16), gr = (int) ((*c >> 8) & 255), b = (int) (*c & 255);
    r = r > cap ? cap : r;
    gr = gr > cap ? cap : gr;
    b = b > cap ? cap : b < g.floor ? g.floor : b;
    *c = ((uint32_t) r << 16) | ((uint32_t) gr << 8) | (uint32_t) b;
  }
}

// The row's color at a column, the glow fading
uint32_t sky_col(uint32_t sun, uint32_t far, int x) {
  if (sun == far)
    return sun;
  const int d = x > g.gx ? x - g.gx : g.gx - x;
  return mix(sun, far, clampi((d - g.w / 4) * g.fade >> 8, 0, 256));
}

// The sky's color at a pixel, weather and glow in
uint32_t sky_at(int x, int y) {
  uint32_t sun, far;
  sky_row(y, sun, far);
  return sky_col(sun, far, x);
}

// Stops, land colors and sizes for this box
void scene_setup(const AppSky &s, uint16_t *px, int w, int h, int stride) {
  const bool grey = s.cover >= 3 || s.fall != 0, am = (s.flags & kSkyAm) != 0;
  const int elev = s.elev;
  g.px = px;
  g.w = (int16_t) w;
  g.h = (int16_t) h;
  g.stride = (int16_t) stride;
  g.home = s.home;
  g.elev = s.elev;
  g.land = (uint8_t) ((s.flags >> kSkyLandShift) % 5);
  g.amp = (int16_t) ((3 * h + 8) / 16);
  g.hy = (int16_t) (h - (h / 8 < 2 ? 2 : h / 8));
  g.gx = (int16_t) (am ? w / 5 : w * 4 / 5);
  g.glow = !grey && elev < 20;
  g.fade = 512 * 256 / w;
  g.fog = s.fog != 0;
  // Dithered down, still 24 and 26 on the panel
  g.floor = grey ? 28 : 26;
  int t;
  const int i = key_at(kElev, 7, elev, t);
  int ts;
  const int k = key_at(kSlateElev, 4, elev, ts);
  const uint32_t slate = mix(kSlate[k], kSlate[k + 1], ts);
  for (int j = 0; j < 4; j++) {
    uint32_t c = mix(sky_key(i, j, am), sky_key(i + 1, j, am), t);
    if (grey)
      c = mix(mix(c, slate, s.fall != 0 ? 192 : 160), 0, s.fall != 0 ? 32 : 0);
    g.stop[j] = c;
  }
  // Day to dusk to night over 10 degrees each way
  const int lit = clampi(elev * 256 / 10, 0, 256), dark = clampi(-elev * 256 / 10, 0, 256);
  g.near = mix(mix(kLandDusk, kLandDay[g.land][0], lit), 0, dark);
  g.rim = mix(mix(kRimDusk, kLandDay[g.land][1], lit), kRimNight, dark);
  if (grey) {
    g.near = mix(g.near, slate, 128);
    g.rim = mix(g.rim, slate, 128);
  }
}

}  // namespace

uint32_t sky_seed(const char *hex, int64_t now) {
  return logo_key(hex, hex != nullptr ? strlen(hex) : 0) ^ (uint32_t) (now / 600);
}

namespace {

// The sine of the sun's elevation; ha its hour angle
float sun_sine(int32_t lat_e4, int32_t lon_e4, int64_t epoch, float &ha) {
  constexpr float kRad = 0.017453293f;
  // Days from J2000 in float: whole days, then the fraction
  const int64_t s = epoch - 946728000;
  const int64_t d = s / 86400 - (s % 86400 < 0 ? 1 : 0);
  const float f = (float) (s - d * 86400) / 86400.0f, n = (float) d + f;
  const float gm = fmodf(357.528f + 0.9856003f * n, 360.0f) * kRad;
  const float lam = (fmodf(280.460f + 0.9856474f * n, 360.0f) + 1.915f * sinf(gm) + 0.020f * sinf(2 * gm)) * kRad;
  const float eps = 23.439f * kRad;
  const float sd = sinf(eps) * sinf(lam), cd = sqrtf(1.0f - sd * sd);
  const float ra = atan2f(cosf(eps) * sinf(lam), cosf(lam));
  const float gmst = fmodf(280.46062f + 360.0f * f + 0.98564737f * n, 360.0f) * kRad;
  const float lat = (float) lat_e4 * 1e-4f * kRad;
  ha = gmst + (float) lon_e4 * 1e-4f * kRad - ra;
  return sinf(lat) * sd + cosf(lat) * cd * cosf(ha);
}

}  // namespace

bool sun_up(int32_t lat_e4, int32_t lon_e4, int64_t epoch) {
  float ha;
  // Above -0.833 degrees: its sine
  return sun_sine(lat_e4, lon_e4, epoch, ha) > -0.014538f;
}

int sun_elev(int32_t lat_e4, int32_t lon_e4, int64_t epoch, bool &rising) {
  float ha;
  const float sine = sun_sine(lat_e4, lon_e4, epoch, ha);
  rising = sinf(ha) < 0;
  return (int) lroundf(asinf(sine < -1 ? -1 : sine > 1 ? 1 : sine) * 57.29578f);
}

uint32_t sky_home(int32_t lat_e4, int32_t lon_e4) {
  if (lat_e4 == INT32_MIN)
    return 0;
  // A tenth of a degree: one view a home
  const int32_t la = (lat_e4 + 900500) / 1000, lo = (lon_e4 + 1800500) / 1000;
  return stir((uint32_t) la * 4001u + (uint32_t) lo, 77) | 1u;
}

uint8_t moon_phase(int64_t epoch) {
  // A new moon (2000-01-06 18:14 UTC), a month in seconds
  constexpr int64_t kNew = 947182440, kMonth = 2551443;
  int64_t age = (epoch - kNew) % kMonth;
  if (age < 0)
    age += kMonth;
  return (uint8_t) ((age * 8 + kMonth / 2) / kMonth % 8);
}

void sky_fill(AppSky &s, const char *hex, int32_t lat_e4, int32_t lon_e4, int64_t now) {
  s.seed = sky_seed(hex, now);
  s.flags = app_env().still ? kSkyStill : 0;
  SkyNow n;
  const App *wx = app_slots().get(Display::WEATHER);
  if (wx != nullptr && wx->sky(now, n)) {
    s.flags |= kSkyWx;
  } else {
    n.cover = (uint8_t) pick(s.seed, 200, 3);
    n.wind = pick(s.seed, 201, 4) == 0;
  }
  bool rising;
  int elev;
  if (lat_e4 != INT32_MIN) {
    elev = sun_elev(lat_e4, lon_e4, now, rising);
  } else {
    // No home: the clock's hours, noon highest
    const struct tm tm = app_local(now);
    const int m = tm.tm_hour * 60 + tm.tm_min;
    rising = m < 720;
    elev = 50 - (m < 720 ? 720 - m : m - 720) * 100 / 360;
  }
  s.elev = (int8_t) clampi(elev, -30, 60);
  if (elev > 0)
    s.flags |= kSkyDay;
  if (rising)
    s.flags |= kSkyAm;
  s.home = sky_home(lat_e4, lon_e4);
  s.flags |= (uint8_t) ((s.home % 5) << kSkyLandShift);
  s.cover = n.cover;
  s.fall = n.fall;
  s.fog = n.fog;
  s.wind = n.wind;
  s.moon = moon_phase(now);
}

void sky_plane_at(const AppSky &s, const PlaneSprite &sp, uint32_t t, int &x, int &y) {
  const int sc = s.scale < 1 ? 1 : s.scale;
  x = (s.w / sc - sp.w) / 2;
  y = (s.h / sc - sp.h) / 2;
  // A slow wave up and down, given room
  const PlaneMotion m = flights::plane_motion((PlaneKind) s.kind);
  const int room = s.h / sc - sp.h - 2;
  const int amp = m == PlaneMotion::STILL || room < 2 ? 0 : room < 6 ? 1 : 2;
  // Up a quarter of the land, clear of the ridge
  const int lift = ((s.h / 8 < 2 ? 2 : s.h / 8) + (3 * s.h + 8) / 16) / 4 / sc;
  y -= lift < y - amp ? lift : y - amp > 0 ? y - amp : 0;
  if (amp == 0)
    return;
  const uint32_t per = m == PlaneMotion::JET ? 8000 : m == PlaneMotion::PROP ? 7000 : 6000;
  const float a = 6.2831853f * (float) (t % per) / (float) per;
  y += (int) lroundf(amp * sinf(a));
}

namespace {

// The gradient, then sun or moon, stars and clouds
void paint_sky(const AppSky &s, uint32_t t) {
  const int W = g.w, H = g.h, hy = g.hy, elev = g.elev;
  const uint32_t seed = s.seed;
  const bool grey = s.cover >= 3 || s.fall != 0;
  for (int y = 0; y < H; y++) {
    uint32_t sun, far;
    sky_row(y, sun, far);
    // One color across: its four dither steps, once
    uint16_t flat[4];
    for (int k = 0; k < 4 && sun == far; k++)
      flat[k] = enc(sun, kBayer[(y & 3) * 4 + k]);
    for (int x = 0; x < W; x++)
      at(x, y) = sun == far ? flat[x & 3] : enc(sky_col(sun, far, x), kBayer[(y & 3) * 4 + (x & 3)]);
  }
  const int tall = H >= 40;
  if (grey) {
    // A scalloped deck, a step lighter
    const int deep = tall ? H / 8 : 2;
    for (int x = 0; x < W; x++)
      for (int y = 0, n = 1 + (vnoise(20, kWorld + (uint32_t) x + t / 1600, grown(7)) * deep >> 8); y < n; y++)
        blend(x, y, 0x98A4BC, 56);
    return;
  }
  // The sun's disc, where the box is tall
  if (tall && elev > -2) {
    // At the top by 25 degrees, over the plane
    const int r = 4, sy = hy - 1 - (elev < 25 ? elev : 25) * (hy - 2 - r) / 25;
    for (int y = -r; y <= r; y++)
      for (int x = -r; x <= r; x++)
        if (x * x + y * y <= r * r + 1 && inside(g.gx + x, sy + y))
          at(g.gx + x, sy + y) = enc(elev < 8 ? 0xFFD66E : 0xFFECAA, 8);
  }
  // Stars and the moon come in below the horizon
  const int night = clampi((-elev - 3) * 32, 0, 256);
  if (night > 0) {
    const int n = clampi(W * hy / 60, 4, 40);
    for (int k = 0; k < n; k++) {
      const int x = (int) pick(seed, 100 + k, (uint32_t) W), y = (int) pick(seed, 200 + k, (uint32_t) (hy * 3 / 4));
      const uint32_t per = 5000 + pick(seed, 300 + k, 2001);
      int v = (70 + (int) pick(seed, 500 + k, 110)) * night >> 8;
      v = v * (256 - 160 * y / hy) >> 8;
      // A short dip, a step down
      if (t != 0 && (t + pick(seed, 400 + k, per)) % per < 400)
        v = v * 3 / 4;
      if (s.cover >= 2)
        v = v * 5 / 8;
      const uint32_t was = dec(at(x, y));
      if (v >= 40 && v > (int) (was & 255))
        at(x, y) = enc(((uint32_t) v << 16) | ((uint32_t) v << 8) | (uint32_t) (v > 200 ? 255 : v + 24), 8);
    }
    const uint8_t ph = s.moon & 7;
    if (ph != 0) {
      Px565 p;
      p.px = g.px;
      p.w = g.w;
      p.h = g.h;
      p.stride = g.stride;
      p.clip = PxBox{0, 0, g.w, g.h};
      const int mx = g.gx > W / 2 ? 2 : W - 7;
      uint16_t pal[16] = {};
      pal[5] = enc(mix(sky_at(mx, 2), 0xD8D4B0, night), 8);
      px_sprite(p, kSprSky[ph == 1 || ph == 7 ? SPR_SKY_CRESCENT : ph == 2 || ph == 6 ? SPR_SKY_HALF : SPR_SKY_FULL], mx,
                1, pal, 1, ph > 4);
    }
  }
  // Thin streaks of cloud, lit from the horizon when low
  const uint32_t tint = elev < -6 ? 0x282C48 : elev < 8 ? g.stop[3] : 0xE8ECF0;
  const int th = tall ? 2 : 1;
  for (int i = 0, n = s.cover == 0 ? 0 : s.cover == 1 ? 2 : 3 + (int) pick(seed, 3, 2); i < n; i++) {
    const int len = clampi(W * (40 + (int) pick(seed, 50 + i, 50)) >> 9, 3, 40), span = W + 2 * len;
    const int drift = (int) (t / (uint32_t) (H > 21 ? 1600 * 21 / H : 1600));
    const int slot = i * span / n + (int) pick(seed, 55 + i, (uint32_t) (span / n));
    const int cx = ((slot + (s.flip ? drift : -drift)) % span + span) % span - len;
    const int cy = 1 + (int) pick(seed, 60 + i, (uint32_t) (hy * 2 / 5 > th ? hy * 2 / 5 - th : 1));
    for (int j = 0; j < th; j++) {
      const int lj = j == th - 1 ? len : len / 2;
      for (int x = -lj; x <= lj; x++)
        blend(cx + x, cy + j, tint, 128 - 96 * x * x / (lj * lj));
    }
  }
}

// Ridges, water, buildings and their lights
void paint_land(const AppSky &s, uint32_t t) {
  const int W = g.w, H = g.h, hy = g.hy;
  const int step = H > 21 ? H : 21;
  const uint32_t sn = t * (uint32_t) step / (800 * 21), sf = t * (uint32_t) step / (2400 * 21);
  const bool grey = s.cover >= 3 || s.fall != 0;
  const int lamp = clampi((2 - g.elev) * 256 / 6, 0, 256), dark = clampi(-g.elev * 256 / 10, 0, 256);
  // Windows a fifth of the box apart, or a third
  const uint32_t gap = (uint32_t) (H < 21 ? W / 3 : W / 5), mw = 2 * (uint32_t) W;
  for (int x = 0; x < W; x++) {
    const uint32_t wn = kWorld + (uint32_t) x + (s.flip ? 0u - sn : sn), wf = kWorld + (uint32_t) x + (s.flip ? 0u - sf : sf);
    const int hn = near_h(wn), top = hy - hn, ftop = hy - far_h(wf);
    const bool water = g.land == 3 && hn == 0;
    const uint32_t hz = sky_at(x, hy - 1);
    const uint32_t far = mix(mix(g.near, hz, 128), kFarNight, dark);
    for (int y = ftop < top ? ftop : top; y < H; y++) {
      if (y < 0)
        continue;
      uint32_t c;
      if (water && y >= hy) {
        // The sky above, upside down and darker
        c = mix(dec(at(x, hy - 1 - (y - hy) < 0 ? 0 : hy - 1 - (y - hy))), 0, 64);
        const int d = x > g.gx ? x - g.gx : g.gx - x;
        if (!grey && g.elev > -6 && d < W / 6 && pick(s.home, (uint32_t) (y * 131 + x) + 7000 + t / 500 * 977, 3) == 0)
          c = mix(c, hz, 160);
        if ((c & 255) < 16)
          c |= 16;
      } else if (y >= top) {
        c = y == top ? g.rim : g.near;
      } else {
        c = far;
      }
      if (g.fog && y >= H / 2)
        c = mix(c, g.stop[3], 128);
      at(x, y) = enc(c, 8);
    }
    if (lamp == 0 || water || hn + (H - hy) < 2)
      continue;
    const int rows = hn + (H - hy) - 1;
    if (g.land == 2) {
      // Lit windows; few where the box is small
      if (H >= 40) {
        for (int y = top + 1; y < H - 1; y++)
          if (((wn + (uint32_t) y) & 1) == 0 && pick(g.home, 8000 + wn * 57 + (uint32_t) y, 9) == 0)
            blend(x, y, kWindow, lamp);
      } else if (wn % gap == pick(g.home, 8100 + wn / gap, gap / 2)) {
        blend(x, top + 1 + (int) pick(g.home, 8150 + wn, (uint32_t) rows), kWindow, lamp);
      }
      // One red mast light, a second on and off
      if (hn >= 2 && wn % mw == pick(g.home, 8400 + wn / mw, mw / 2) && (t / 1000 & 1) == 0)
        blend(x, top, kMast, lamp);
    } else if (wn % 24 == pick(g.home, 8200 + wn / 24, 8)) {
      blend(x, top + 1 + (int) pick(g.home, 8300 + wn, (uint32_t) rows), kLamp, lamp);
    }
  }
}

}  // namespace

void sky_paint(Px565 &out, const AppSky &s, uint32_t t) {
  const int sc = s.scale < 1 ? 1 : s.scale;
  const int W = out.w, H = out.h;
  const PlaneKind kind = (PlaneKind) s.kind;
  const PlaneSprite *sp = flights::plane_sprite(kind);
  const PlaneMotion m = flights::plane_motion(kind);
  const bool still = (s.flags & kSkyStill) != 0 || (sp != nullptr && m == PlaneMotion::STILL);
  if (still)
    t = 0;
  // Too small for a scene: dark
  if (W < 8 || H < 8) {
    out.fill(0, 0, W, H, 0);
    return;
  }
  scene_setup(s, out.px, W, H, out.stride);
  paint_sky(s, t);
  paint_land(s, t);
  const uint32_t seed = s.seed;
  auto r = [seed](uint32_t k, uint32_t n) { return (int) pick(seed, k, n); };
  Px565 p = out;
  p.clip = PxBox{0, 0, (int16_t) W, (int16_t) H};
  p.mirror = false;
  if (s.wind)
    for (int i = 0, n = 1 + r(5, 2); i < n; i++) {
      const int len = 4 + r(65 + i, 4), wspan = W + len + 24;
      const int x = (i * wspan / 2 + r(70 + i, wspan / 4) + wspan - (int) ((t / 200) % wspan)) % wspan - len;
      for (int k = 0, y = 2 + r(75 + i, H > 4 ? H - 4 : 1); k < len; k++)
        blend(s.flip ? W - 1 - x - k : x + k, y, 0xC8D2E1, 64);
    }
  // Now and then a few birds cross the top, by day
  if (g.elev > 0 && !still) {
    const uint32_t per = 45000 + r(6, 45001), at_ms = (t + r(7, per)) % per;
    const int nb = 2 + r(8, 2);
    uint16_t pal[16] = {};
    pal[3] = enc(0x1C1C28, 8);
    if (at_ms < (uint32_t) (W + 8 * nb) * kBirdMs)
      for (int k = 0; k < nb; k++) {
        const int x = W - (int) (at_ms / kBirdMs) + 8 * k;
        px_sprite(p, kSprSky[((at_ms / 2000 + k) & 1) ? SPR_SKY_UP : SPR_SKY_DOWN], x, 1 + (k & 1) * 2, pal, 1, false);
      }
  }
  if (s.fall != 0)
    for (int k = 0, n = 3 + r(9, 4) + W * H / 400; k < n; k++) {
      const int x = k * W / n + r(80 + k, W / n > 0 ? W / n : 1);
      if (s.fall == 1)
        p.vline(x, (int) ((r(90 + k, H + 2) + t / 150) % (H + 2)) - 2, 2, enc(0x466096, 8));
      else
        p.put(x + (int) ((t / 1200 + k) & 1), (int) ((r(90 + k, H) + t / 400) % H), enc(0xB8C0D0, 8));
    }
  if (sp == nullptr)
    return;
  int px, py;
  sky_plane_at(s, *sp, t, px, py);
  px *= sc;
  py *= sc;
  // Off the sky's steps; short blades as it bobs
  const uint32_t frame = (t + kSkySpinMs * 5 / 2) / kSkySpinMs % 4;
  const flights::PropSpot *ps = flights::prop_spot(kind);
  const bool rotor = m == PlaneMotion::ROTOR;
  const int half = kRotorHalfs[frame] * flights::kRotorHalf / 9;
  // The ink at a sprite cell this frame; 0 none
  auto ink_at = [&](int x, int y) -> uint8_t {
    if (x < 0 || y < 0 || x >= sp->w || y >= sp->h)
      return 0;
    if (rotor && y == 0)
      return x >= flights::kRotorMid - half && x <= flights::kRotorMid + half ? flights::kArtDetail : 0;
    // The tail rotor turns: up a pixel every other step
    if (rotor && x == 0 && (frame & 1) && (y == 2 || y == 3)) {
      const uint8_t low = flights::sprite_px(*sp, 0, 3);
      return y == 3 ? 0 : low != 0 ? low : flights::sprite_px(*sp, 0, 2);
    }
    const uint8_t v = flights::sprite_px(*sp, x, y);
    if (ps != nullptr && x == ps->x && y >= ps->y0 && y <= ps->y1 && v == flights::kArtDetail &&
        !prop_on(*sp, *ps, y, frame))
      return 0;
    return v;
  };
  // A fading, dashed contrail sliding away behind a jet
  if (m == PlaneMotion::JET)
    for (int d = 0, n = (6 + r(11, 5)) * sc, ty = py + tail_row(*sp) * sc + (sc - 1) / 2; d < n; d++)
      if (((d / sc + 4 - (int) ((t / 250) % 4)) & 3) != 3)
        for (int j = 0; j < (sc + 1) / 2; j++)
          blend(s.flip ? W - px + d : px - 1 - d, ty + j, 0xDCE0E8, 128 * (n - d) / n);
  // A dark halo keeps the plane clear of the scene
  const uint32_t tail = s.tail & 0xFFFFFF;
  const bool deep = g.elev >= 10 && (tail & 255) > ((tail >> 16) & 255) && (tail & 255) > ((tail >> 8) & 255);
  // Three cells down a column: lit bits, then tail bits
  auto column = [&](int cx, int cy) -> unsigned {
    unsigned m = 0;
    for (int d = 0; d < 3; d++) {
      const uint8_t v = ink_at(cx, cy - 1 + d);
      m |= (unsigned) (v != 0) << d | (unsigned) (v == flights::kArtTail) << (4 + d);
    }
    return m;
  };
  // A column's bits spread to the cells round the middle
  auto spread = [](unsigned m, int dx) -> unsigned {
    return (m & 1) << dx | (m >> 1 & 1) << (3 + dx) | (m >> 2 & 1) << (6 + dx);
  };
  for (int cy = -1; cy <= sp->h; cy++) {
    unsigned a = 0, b = 0, c = column(0, cy);
    for (int cx = -1; cx <= sp->w; cx++, a = b, b = c, c = column(cx + 1, cy)) {
      if ((b & 2) != 0 || ((a | b | c) & 7) == 0)
        continue;
      // The eight cells round this empty one, a bit each
      const unsigned on = spread(a, 0) | spread(b, 1) | spread(c, 2);
      const unsigned by_tail = spread(a >> 4, 0) | spread(b >> 4, 1) | spread(c >> 4, 2);
      // Its pixels one away from a lit cell
      for (int j = 0; j < sc; j++)
        for (int i = 0; i < sc; i++) {
          unsigned m = 0;
          for (int k = 0; k < 9; k++) {
            const int kx = k % 3, ky = k / 3;
            if ((kx == 1 || (kx == 0 ? i == 0 : i == sc - 1)) && (ky == 1 || (ky == 0 ? j == 0 : j == sc - 1)))
              m |= 1u << k;
          }
          if ((on & m) == 0)
            continue;
          const int x = px + cx * sc + i;
          blend(s.flip ? W - 1 - x : x, py + cy * sc + j, 0, deep && (by_tail & m) != 0 ? 256 : 200);
        }
    }
  }
  const uint16_t ink[4] = {0, rgb565(flights::kColBody), rgb565(s.tail), rgb565(flights::kColDetail)};
  p.mirror = s.flip != 0;
  for (int y = 0; y < sp->h; y++)
    for (int x = 0; x < sp->w; x++)
      if (const uint8_t v = ink_at(x, y))
        p.fill(px + x * sc, py + y * sc, sc, sc, ink[v]);
}

}  // namespace espn
