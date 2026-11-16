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

// A number from the seed for slot k, 0 to n - 1.
uint32_t pick(uint32_t seed, uint32_t k, uint32_t n) {
  uint32_t x = seed ^ (k * 0x9E3779B9u);
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return n > 1 ? x % n : 0;
}

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

const uint8_t kRotorHalfs[4] = {9, 6, 2, 6};
// Birds glide a pixel this often
const uint32_t kBirdMs = 250;

}  // namespace

uint32_t sky_seed(const char *hex, int64_t now) {
  return logo_key(hex, hex != nullptr ? strlen(hex) : 0) ^ (uint32_t) (now / 600);
}

bool sun_up(int32_t lat_e4, int32_t lon_e4, int64_t epoch) {
  constexpr float kRad = 0.017453293f;
  // Days from J2000 in float: whole days, then the fraction
  const int64_t s = epoch - 946728000;
  const int64_t d = s / 86400 - (s % 86400 < 0 ? 1 : 0);
  const float f = (float) (s - d * 86400) / 86400.0f, n = (float) d + f;
  const float g = fmodf(357.528f + 0.9856003f * n, 360.0f) * kRad;
  const float lam = (fmodf(280.460f + 0.9856474f * n, 360.0f) + 1.915f * sinf(g) + 0.020f * sinf(2 * g)) * kRad;
  const float eps = 23.439f * kRad;
  const float sd = sinf(eps) * sinf(lam), cd = sqrtf(1.0f - sd * sd);
  const float ra = atan2f(cosf(eps) * sinf(lam), cosf(lam));
  const float gmst = fmodf(280.46062f + 360.0f * f + 0.98564737f * n, 360.0f) * kRad;
  const float lat = (float) lat_e4 * 1e-4f * kRad, ha = gmst + (float) lon_e4 * 1e-4f * kRad - ra;
  // Above -0.833 degrees: its sine
  return sinf(lat) * sd + cosf(lat) * cd * cosf(ha) > -0.014538f;
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
    n.day = lat_e4 != INT32_MIN ? sun_up(lat_e4, lon_e4, now) : (unsigned) (app_local(now).tm_hour - 7) < 12;
    n.cover = (uint8_t) pick(s.seed, 200, 3);
    n.wind = pick(s.seed, 201, 4) == 0;
  }
  if (n.day)
    s.flags |= kSkyDay;
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
  // A helicopter bobs, up 2 s, down 2 s, given room
  if ((PlaneKind) s.kind == PlaneKind::HELI && s.h / sc >= 20 && (t / 2000) % 2 == 1)
    y--;
}

void sky_paint(Px565 &out, const AppSky &s, uint32_t t) {
  const int sc = s.scale < 1 ? 1 : s.scale;
  Px565 p = out;
  p.w = (int16_t) (out.w / sc);
  p.h = (int16_t) (out.h / sc);
  p.clip = PxBox{0, 0, p.w, p.h};
  p.mirror = s.flip != 0;
  const int W = p.w, H = p.h;
  out.fill(0, 0, out.w, out.h, 0);
  const PlaneKind kind = (PlaneKind) s.kind;
  const PlaneSprite *sp = flights::plane_sprite(kind);
  const PlaneMotion m = flights::plane_motion(kind);
  const bool still = (s.flags & kSkyStill) != 0 || (sp != nullptr && m == PlaneMotion::STILL);
  if (still)
    t = 0;
  const uint32_t seed = s.seed;
  auto r = [seed](uint32_t k, uint32_t n) { return (int) pick(seed, k, n); };
  const bool day = (s.flags & kSkyDay) != 0, grey = s.cover >= 3 || s.fall != 0;
  // Slots: cloud top, cloud underside, bird, sun, moon
  uint16_t pal[16] = {0, rgb565(grey ? 0x2E2E2E : 0x3C3C3C), rgb565(grey ? 0x262626 : 0x2E2E2E), rgb565(0x504838),
                      rgb565(0x705408), rgb565(0x585858)};
  if (s.fog) {
    p.hline(0, H / 3, W, rgb565(0x181818));
    p.hline(0, 2 * H / 3, W, rgb565(0x181818));
  }
  // The sun or moon in a top corner, stars at night; none overcast
  const int corner = r(1, 2) ? W - 6 : 1;
  if (!grey && day) {
    px_sprite(p, kSprSky[SPR_SKY_SUN], corner, 1, pal, 1, false);
  } else if (!grey) {
    for (int k = 0, n = 3 + r(2, 3); k < n; k++) {
      const uint32_t per = 4000 + r(10 + k, 3001), f = (t + r(20 + k, per)) % per;
      const uint32_t v = 0x30 + (f * 2 < per ? f * 64 / per : (per - f) * 64 / per);
      p.put(r(30 + k, W), r(40 + k, H / 2 > 0 ? H / 2 : 1), rgb565(v * 0x010101u));
    }
    const uint8_t ph = s.moon & 7;
    if (ph != 0)
      px_sprite(p, kSprSky[ph == 1 || ph == 7 ? SPR_SKY_CRESCENT : ph == 2 || ph == 6 ? SPR_SKY_HALF : SPR_SKY_FULL],
                corner, 1, pal, 1, ph > 4);
  }
  // Clouds drift toward the tail, slower behind slower craft
  const uint32_t drift = sp == nullptr ? 800 : m == PlaneMotion::JET ? 300 : m == PlaneMotion::PROP ? 600 : 800;
  const int span = W + 16, nc = s.cover >= 3 ? 3 : s.cover + r(3, 2);
  for (int i = 0; i < nc; i++) {
    const Sprite &c = kSprSky[SPR_SKY_CLOUD1 + r(50 + i, H < 14 ? 2 : 3)];
    const int x0 = i * span / nc + r(55 + i, span / nc), x = (x0 + span - (int) ((t / drift) % span)) % span - c.w;
    const int y = ((i + r(4, 2)) & 1) ? r(60 + i, 2) : H - c.h - r(60 + i, 2);
    px_sprite(p, c, x, y, pal, 1, false);
  }
  if (s.wind)
    for (int i = 0, n = 1 + r(5, 2); i < n; i++) {
      const int len = 4 + r(65 + i, 4), wspan = W + len + 24;
      const int x = (i * wspan / 2 + r(70 + i, wspan / 4) + wspan - (int) ((t / 200) % wspan)) % wspan - len;
      p.hline(x, 2 + r(75 + i, H > 4 ? H - 4 : 1), len, rgb565(0x202830));
    }
  // Now and then a few birds cross the top, by day
  if (day && !still) {
    const uint32_t per = 45000 + r(6, 45001), at = (t + r(7, per)) % per;
    const int nb = 2 + r(8, 2);
    if (at < (uint32_t) (W + 8 * nb) * kBirdMs)
      for (int k = 0; k < nb; k++) {
        const int x = W - (int) (at / kBirdMs) + 8 * k;
        px_sprite(p, kSprSky[((at / 2000 + k) & 1) ? SPR_SKY_UP : SPR_SKY_DOWN], x, 1 + (k & 1) * 2, pal, 1, false);
      }
  }
  if (s.fall != 0)
    for (int k = 0, n = 3 + r(9, 4); k < n; k++) {
      const int x = k * W / n + r(80 + k, W / n > 0 ? W / n : 1);
      if (s.fall == 1)
        p.vline(x, (int) ((r(90 + k, H + 2) + t / 150) % (H + 2)) - 2, 2, rgb565(0x203048));
      else
        p.put(x + (int) ((t / 1200 + k) & 1), (int) ((r(90 + k, H) + t / 400) % H), rgb565(0x404040));
    }
  if (sp != nullptr) {
    int px, py;
    sky_plane_at(s, *sp, t, px, py);
    const uint32_t frame = (t / 100) % 4;
    // A dashed contrail sliding away behind a jet
    if (m == PlaneMotion::JET)
      for (int d = 0, ty = py + tail_row(*sp), n = 6 + r(11, 5); d < n; d++)
        if (((d + 4 - (int) ((t / 250) % 4)) & 3) != 3)
          p.put(px - 1 - d, ty, rgb565(0x3A3A3A));
    // A black edge keeps the plane clear of the sky
    for (int y = 0; y < sp->h; y++)
      for (int x = 0; x < sp->w; x++)
        if (flights::sprite_px(*sp, x, y) != 0)
          p.fill(px + x - 1, py + y - 1, 3, 3, 0);
    const uint16_t ink[4] = {0, rgb565(flights::kColBody), rgb565(s.tail), rgb565(flights::kColDetail)};
    const flights::PropSpot *ps = flights::prop_spot(kind);
    const bool rotor = m == PlaneMotion::ROTOR;
    for (int y = 0; y < sp->h; y++)
      for (int x = 0; x < sp->w; x++) {
        const uint8_t v = flights::sprite_px(*sp, x, y);
        if (v == 0 || (rotor && y == 0))
          continue;
        if (ps != nullptr && x == ps->x && y >= ps->y0 && y <= ps->y1 && v == flights::kArtDetail &&
            !prop_on(*sp, *ps, y, frame))
          continue;
        // The tail rotor turns: up a pixel every other step
        const bool up = rotor && x == 0 && y == 3 && (frame & 1);
        p.put(px + x, py + y - up, ink[v]);
      }
    // Blades seen edge on, shortening and growing
    if (rotor) {
      const int half = kRotorHalfs[frame] * flights::kRotorHalf / 9;
      p.hline(px + flights::kRotorMid - half, py, 2 * half + 1, ink[flights::kArtDetail]);
    }
  }
  if (sc > 1)
    for (int y = H * sc - 1; y >= 0; y--)
      for (int x = W * sc - 1; x >= 0; x--)
        out.px[y * out.stride + x] = out.px[(y / sc) * out.stride + x / sc];
}

}  // namespace espn
