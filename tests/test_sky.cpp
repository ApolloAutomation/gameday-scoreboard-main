// The Flight Tracker's moving sky behind its drawn planes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "flights.h"
#include "sky.h"

using namespace espn;
using flights::PlaneKind;
using flights::PlaneMotion;
using flights::PlaneSprite;

namespace {

int checks = 0;
int failures = 0;

#define SCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

const uint32_t kSeed = 0x5EEDu;
const uint32_t kEnd = 120000, kStep = 100;

AppSky sky_of(PlaneKind k, int w, int h, bool flip, uint8_t flags, uint8_t cover = 1, uint8_t fall = 0) {
  AppSky s;
  s.w = (uint8_t) w;
  s.h = (uint8_t) h;
  s.kind = (uint8_t) k;
  s.flip = flip;
  s.flags = flags;
  s.cover = cover;
  s.fall = fall;
  s.moon = 4;
  s.tail = 0xD02020;
  s.seed = kSeed;
  return s;
}

std::vector<uint16_t> paint(const AppSky &s, uint32_t t) {
  std::vector<uint16_t> buf((size_t) s.w * s.h, 0xFFFF);
  Px565 p;
  p.px = buf.data();
  p.w = p.stride = s.w;
  p.h = s.h;
  p.clip = PxBox{0, 0, s.w, s.h};
  sky_paint(p, s, t);
  return buf;
}

int luma(uint16_t c) {
  const int r = (c >> 11) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
  return (3 * r + 6 * g + b) / 10;
}

int count(const std::vector<uint16_t> &px, uint32_t rgb) {
  int n = 0;
  for (uint16_t c : px)
    n += c == rgb565(rgb);
  return n;
}

const PlaneKind kKinds[] = {PlaneKind::GENERIC, PlaneKind::LIGHT,  PlaneKind::TWIN_PROP, PlaneKind::TURBOPROP,
                            PlaneKind::BIZJET,  PlaneKind::REGIONAL, PlaneKind::NARROW, PlaneKind::WIDE,
                            PlaneKind::JUMBO,   PlaneKind::HELI,   PlaneKind::FIGHTER,   PlaneKind::MIL_CARGO};

// Weather, as far as the sky goes: snowy, foggy, windy by day.
class WeatherSky final : public App {
 public:
  Display id() const override { return Display::WEATHER; }
  const char *const *hosts() const override { return nullptr; }
  uint32_t poll_ms(int64_t) const override { return 0; }
  bool request(int64_t, AppRequest &) override { return false; }
  void apply(bool, int64_t) override {}
  void frame(int, int, int64_t, AppFrame &) const override {}
  bool sky(int64_t, SkyNow &out) const override {
    out.day = true;
    out.cover = 3;
    out.fall = 2;
    out.fog = out.wind = 1;
    return true;
  }
};

void test_motion() {
  SCHECK(flights::plane_motion(PlaneKind::NONE) == PlaneMotion::STILL &&
             flights::plane_motion(PlaneKind::GENERIC) == PlaneMotion::STILL,
         "unknown kinds hold still");
  SCHECK(flights::plane_motion(PlaneKind::HELI) == PlaneMotion::ROTOR, "a helicopter's rotor turns");
  for (PlaneKind k : {PlaneKind::LIGHT, PlaneKind::TWIN_PROP, PlaneKind::TURBOPROP}) {
    SCHECK(flights::plane_motion(k) == PlaneMotion::PROP, "kind %d: propellers", (int) k);
    // The spot is the sprite's own propeller: detail pixels in a column
    const flights::PropSpot *ps = flights::prop_spot(k);
    const PlaneSprite *sp = flights::plane_sprite(k);
    int n = 0;
    for (int y = ps != nullptr ? ps->y0 : 1; ps != nullptr && y <= ps->y1; y++)
      n += flights::sprite_px(*sp, ps->x, y) == flights::kArtDetail;
    SCHECK(ps != nullptr && ps->x < sp->w && ps->y1 < sp->h && n >= 3, "kind %d: a propeller of %d pixels", (int) k,
           n);
  }
  for (PlaneKind k : {PlaneKind::BIZJET, PlaneKind::REGIONAL, PlaneKind::NARROW, PlaneKind::WIDE, PlaneKind::JUMBO,
                      PlaneKind::FIGHTER, PlaneKind::MIL_CARGO})
    SCHECK(flights::plane_motion(k) == PlaneMotion::JET && flights::prop_spot(k) == nullptr, "kind %d: a jet",
           (int) k);
  const PlaneSprite *heli = flights::plane_sprite(PlaneKind::HELI);
  bool row = true;
  for (int x = 0; x < heli->w; x++)
    row = row && flights::sprite_px(*heli, x, 0) == flights::kArtDetail;
  SCHECK(row && heli->w == 2 * flights::kRotorHalf + 1 && flights::kRotorMid == flights::kRotorHalf,
         "the rotor is the top row, centered");
}

// The pixels the rotor or a propeller may change, at t.
void spot_at(const AppSky &s, uint32_t t, std::set<int> &out) {
  const PlaneSprite *sp = flights::plane_sprite((PlaneKind) s.kind);
  int px, py;
  sky_plane_at(s, *sp, t, px, py);
  auto add = [&](int x, int y) { out.insert(y * s.w + (s.flip ? s.w - 1 - x : x)); };
  if ((PlaneKind) s.kind == PlaneKind::HELI) {
    for (int x = 0; x < sp->w; x++)
      add(px + x, py);
    add(px, py + 2);
    add(px, py + 3);
  }
  if (const flights::PropSpot *ps = flights::prop_spot((PlaneKind) s.kind))
    for (int y = ps->y0; y <= ps->y1; y++)
      add(px + ps->x, py + y);
}

// The plane is the clearest thing: its own inks, a black edge.
void test_clarity() {
  const uint16_t ink[4] = {0, rgb565(flights::kColBody), rgb565(0xD02020), rgb565(flights::kColDetail)};
  const int cap = luma(rgb565(flights::kColBody)) * 45 / 100;
  for (PlaneKind k : kKinds)
    for (int flip = 0; flip < 2; flip++)
      for (uint8_t flags : {(uint8_t) kSkyDay, (uint8_t) 0}) {
        AppSky s = sky_of(k, 56, 21, flip, flags, 3, flags ? 0 : 1);
        s.wind = 1;
        const PlaneSprite *sp = flights::plane_sprite(k);
        int bad = 0, edge = 0, decor = 0, spot_max = 0;
        for (uint32_t t = 0; t < kEnd; t += kStep) {
          const std::vector<uint16_t> px = paint(s, t);
          int x0, y0;
          sky_plane_at(s, *sp, t, x0, y0);
          std::set<int> spot;
          spot_at(s, t, spot);
          spot_max = std::max(spot_max, (int) spot.size());
          std::set<int> plane;
          for (int y = -1; y <= sp->h; y++)
            for (int x = -1; x <= sp->w; x++) {
              const int bx = x0 + x, by = y0 + y;
              if (bx < 0 || by < 0 || bx >= s.w || by >= s.h)
                continue;
              const int i = by * s.w + (flip ? s.w - 1 - bx : bx);
              const bool in = x >= 0 && y >= 0 && x < sp->w && y < sp->h;
              const uint8_t v = in ? flights::sprite_px(*sp, x, y) : 0;
              bool near = false;
              for (int dy = -1; dy <= 1 && !near; dy++)
                for (int dx = -1; dx <= 1 && !near; dx++) {
                  const int nx = x + dx, ny = y + dy;
                  near = nx >= 0 && ny >= 0 && nx < sp->w && ny < sp->h && flights::sprite_px(*sp, nx, ny) != 0;
                }
              if (!near)
                continue;
              plane.insert(i);
              if (spot.count(i) != 0) {
                bad += px[i] != 0 && px[i] != ink[1] && px[i] != ink[3];
              } else if (v != 0) {
                bad += px[i] != ink[v];
              } else {
                edge += px[i] != 0;
              }
            }
          for (int i = 0; i < s.w * s.h; i++)
            if (plane.count(i) == 0 && luma(px[i]) > cap)
              decor++;
        }
        SCHECK(bad == 0 && edge == 0, "kind %d flip %d %s: the plane's inks (%d off) and black edge (%d off)", (int) k,
               flip, flags ? "day" : "night", bad, edge);
        SCHECK(decor == 0, "kind %d: the sky under 45%% of the plane's white (%d brighter)", (int) k, decor);
        SCHECK(spot_max <= 24, "kind %d: the rotor or propeller spot %d px", (int) k, spot_max);
      }
}

// The most swings across a series' range in 1 s: from its
// lowest third to its highest or back. 0 under 10% of full.
int crosses(const std::vector<int> &v) {
  int lo = 255, hi = 0;
  for (int x : v) {
    lo = std::min(lo, x);
    hi = std::max(hi, x);
  }
  if (hi - lo < 26)
    return 0;
  const int low = lo + (hi - lo) / 3, high = hi - (hi - lo) / 3;
  std::vector<int> at;
  int state = v[0] >= high ? 1 : 0;
  for (size_t k = 1; k < v.size(); k++)
    if ((state == 0 && v[k] >= high) || (state == 1 && v[k] <= low)) {
      state ^= 1;
      at.push_back((int) k);
    }
  int most = 0;
  for (size_t a = 0, b = 0; b < at.size(); b++) {
    while (at[b] - at[a] >= 10)
      a++;
    most = std::max(most, (int) (b - a + 1));
  }
  return most;
}

// Small steps only: nothing large changes at once or flickers.
void check_calm(const AppSky &s, const char *what) {
  const int n = s.w * s.h;
  const int steps = (int) (kEnd / kStep);
  std::vector<std::vector<uint8_t>> lum(steps, std::vector<uint8_t>(n));
  std::set<int> spot;
  int worst_changed = 0, worst_total = 0;
  long prev_total = -1;
  std::vector<uint16_t> prev;
  for (int k = 0; k < steps; k++) {
    const uint32_t t = (uint32_t) k * kStep;
    const std::vector<uint16_t> px = paint(s, t);
    long total = 0;
    for (int i = 0; i < n; i++) {
      lum[k][i] = (uint8_t) luma(px[i]);
      total += lum[k][i];
    }
    if (flights::plane_sprite((PlaneKind) s.kind) != nullptr)
      spot_at(s, t, spot);
    if (!prev.empty()) {
      int changed = 0;
      for (int i = 0; i < n; i++)
        changed += px[i] != prev[i];
      worst_changed = std::max(worst_changed, changed);
      // Against the frame, or a whole box at a dim 8
      const long floor = std::max(prev_total, (long) n * 8);
      worst_total = std::max(worst_total, (int) (std::labs(total - prev_total) * 1000 / floor));
    }
    prev = px;
    prev_total = total;
  }
  SCHECK(worst_changed * 100 <= n * 12, "%s: at most %d of %d pixels change a step", what, worst_changed, n);
  SCHECK(worst_total < 100, "%s: the box's brightness moves %d.%d%% a step", what, worst_total / 10,
         worst_total % 10);
  // A pixel flashes (swings there and back) under 3 times a
  // second; an 8x8 area (unscaled) swings under 3 times
  int flicker = 0, at = -1, areas = 0, area_at = -1;
  std::vector<int> series(steps);
  for (int i = 0; i < n; i++) {
    if (spot.count(i) != 0)
      continue;
    for (int k = 0; k < steps; k++)
      series[k] = lum[k][i];
    if (crosses(series) > 6) {
      flicker++;
      at = i;
    }
  }
  const int side = 8 * s.scale;
  for (int by = 0; by + side <= s.h; by += side / 2)
    for (int bx = 0; bx + side <= s.w; bx += side / 2) {
      bool mine = true;
      for (int y = by; y < by + side; y++)
        for (int x = bx; x < bx + side; x++)
          mine = mine && spot.count(y * s.w + x) == 0;
      if (!mine)
        continue;
      for (int k = 0; k < steps; k++) {
        series[k] = 0;
        for (int y = by; y < by + side; y++)
          for (int x = bx; x < bx + side; x++)
            series[k] += lum[k][y * s.w + x];
        series[k] /= side * side;
      }
      if (crosses(series) > 3) {
        areas++;
        area_at = by * s.w + bx;
      }
    }
  SCHECK(flicker == 0, "%s: %d pixels flash more than 3 times a second (one at %d,%d)", what, flicker,
         at < 0 ? 0 : at % s.w, at < 0 ? 0 : at / s.w);
  SCHECK(areas == 0, "%s: %d areas change more than 3 times a second (one at %d,%d)", what, areas,
         area_at < 0 ? 0 : area_at % s.w, area_at < 0 ? 0 : area_at / s.w);
}

void test_calm() {
  char what[96];
  for (PlaneKind k : kKinds)
    for (int day = 0; day < 2; day++)
      for (uint8_t fall : {0, 1, 2}) {
        for (int w : {56, 64}) {
          AppSky s = sky_of(k, w, w == 64 ? 16 : 21, k == PlaneKind::NARROW, day ? kSkyDay : 0, fall ? 3 : 2, fall);
          s.wind = 1;
          s.fog = fall == 2;
          snprintf(what, sizeof what, "kind %d %dx%d %s fall %d", (int) k, s.w, s.h, day ? "day" : "night", fall);
          check_calm(s, what);
        }
      }
  // The sky alone, over a note, at each band
  for (const auto &b : {std::make_pair(64, 24), std::make_pair(128, 28), std::make_pair(128, 48)})
    for (int day = 0; day < 2; day++)
      for (uint32_t seed : {1u, 2u, 3u}) {
        AppSky s = sky_of(PlaneKind::NONE, b.first, b.second, false, day ? kSkyDay : 0, seed % 4, seed == 3);
        s.scale = b.second == 48 ? 2 : 1;
        s.seed = seed;
        s.wind = seed == 2;
        snprintf(what, sizeof what, "band %dx%d %s seed %u", s.w, s.h, day ? "day" : "night", (unsigned) seed);
        check_calm(s, what);
      }
}

void test_still() {
  for (PlaneKind k : kKinds) {
    AppSky s = sky_of(k, 56, 21, false, kSkyDay | kSkyStill, 2, 1);
    SCHECK(paint(s, 0) == paint(s, 5000) && paint(s, 0) == paint(s, 63300), "kind %d: still in night hours or dim",
           (int) k);
  }
  // An unknown type: a still picture in a still sky
  AppSky g = sky_of(PlaneKind::GENERIC, 56, 21, false, kSkyDay, 2, 1);
  SCHECK(paint(g, 0) == paint(g, 7700), "an unknown type holds still");
  AppSky j = sky_of(PlaneKind::NARROW, 56, 21, false, kSkyDay, 2);
  SCHECK(paint(j, 0) != paint(j, 1500), "a jet's sky moves");
  AppSky band = sky_of(PlaneKind::NONE, 128, 28, false, kSkyDay, 2);
  SCHECK(paint(band, 0) != paint(band, 4000), "a note's sky drifts");
}

void test_variety() {
  const int64_t now = 1794457500;
  SCHECK(sky_seed("a1b2c3", now) != sky_seed("a1b2c4", now), "planes differ");
  SCHECK(sky_seed("a1b2c3", now) != sky_seed("a1b2c3", now + 600), "a new sky every 10 minutes");
  SCHECK(sky_seed("a1b2c3", now) == sky_seed("a1b2c3", now + 1), "the same sky meanwhile");
  int differ = 0;
  for (uint32_t k = 0; k < 8; k++) {
    AppSky a = sky_of(PlaneKind::NARROW, 56, 21, false, kSkyDay, 2), b = a;
    a.seed = sky_seed("a1b2c3", now + 600 * k);
    b.seed = sky_seed("a00001", now + 600 * k);
    differ += paint(a, 0) != paint(b, 0);
    SCHECK(paint(a, 2500) == paint(a, 2500), "the same inputs, the same picture");
  }
  SCHECK(differ >= 7, "different planes, different skies: %d of 8", differ);
}

void test_day_night() {
  const uint32_t sun = 0x705408, moon = 0x585858, bird = 0x504838;
  for (uint32_t seed = 1; seed <= 6; seed++) {
    AppSky d = sky_of(PlaneKind::LIGHT, 56, 21, false, kSkyDay, 0), n = d;
    n.flags = 0;
    d.seed = n.seed = seed;
    int birds_day = 0, birds_night = 0, sun_day = 0, sun_night = 0, moon_day = 0, moon_night = 0;
    for (uint32_t t = 0; t < kEnd; t += 500) {
      const std::vector<uint16_t> a = paint(d, t), b = paint(n, t);
      birds_day += count(a, bird);
      birds_night += count(b, bird);
      sun_day += count(a, sun);
      sun_night += count(b, sun);
      moon_day += count(a, moon);
      moon_night += count(b, moon);
    }
    SCHECK(sun_day > 0 && sun_night == 0 && moon_night > 0 && moon_day == 0,
           "seed %u: the sun by day, the moon by night", (unsigned) seed);
    SCHECK(birds_day > 0 && birds_night == 0, "seed %u: birds now and then by day only (%d)", (unsigned) seed,
           birds_day);
    // Stars: what night adds besides the moon, a few dim pixels
    AppSky ds = sky_of(PlaneKind::NONE, 128, 28, false, kSkyDay | kSkyStill, 0), ns = ds;
    ns.flags = kSkyStill;
    ds.seed = ns.seed = seed;
    const std::vector<uint16_t> a = paint(ds, 0), b = paint(ns, 0);
    int stars = 0, lit = 0;
    for (size_t i = 0; i < a.size(); i++)
      if (a[i] != b[i] && b[i] != rgb565(moon) && b[i] != 0) {
        stars++;
        lit += luma(b[i]) >= 0x30 && luma(b[i]) <= 0x50;
      }
    SCHECK(stars >= 1 && stars <= 5 && lit == stars, "seed %u: %d stars at night", (unsigned) seed, stars);
  }
  // The moon's phase picks its picture; a new moon shows none
  AppSky nm = sky_of(PlaneKind::NONE, 64, 24, false, kSkyStill, 0);
  nm.moon = 0;
  SCHECK(count(paint(nm, 0), moon) == 0, "no moon at the new moon");
  nm.moon = 1;
  const int crescent = count(paint(nm, 0), moon);
  nm.moon = 4;
  SCHECK(crescent > 0 && count(paint(nm, 0), moon) > crescent, "a crescent, then a full moon");
}

void test_weather() {
  long clear = 0, overcast = 0;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    AppSky a = sky_of(PlaneKind::NONE, 128, 28, false, kSkyDay | kSkyStill, 0), b = a;
    a.seed = b.seed = seed;
    b.cover = 3;
    const std::vector<uint16_t> pa = paint(a, 0), pb = paint(b, 0);
    clear += count(pa, 0x3C3C3C) + count(pa, 0x2E2E2E);
    overcast += count(pb, 0x2E2E2E) + count(pb, 0x262626);
    SCHECK(count(pb, 0x3C3C3C) == 0 && count(pb, 0x262626) > 0, "seed %u: overcast clouds darker", (unsigned) seed);
  }
  SCHECK(overcast > clear * 3, "more clouds overcast: %ld against %ld", overcast, clear);
  AppSky r = sky_of(PlaneKind::NONE, 128, 28, false, kSkyDay, 3, 1);
  SCHECK(count(paint(r, 0), 0x203048) >= 3, "rain falls");
  r.fall = 2;
  SCHECK(count(paint(r, 0), 0x404040) >= 2, "snow falls");
  r.fall = 0;
  r.fog = 1;
  SCHECK(count(paint(r, 0), 0x181818) > 20, "fog lies in bands");
  // The sun at Versailles, sunrise and sunset in June and December
  const int32_t lat = 488049, lon = 21204;
  const int64_t times[][2] = {{1782013740, 1782071940}, {1797838920, 1797868680}};
  for (const auto &t : times) {
    SCHECK(!sun_up(lat, lon, t[0] - 600) && sun_up(lat, lon, t[0] + 600), "sunrise at %lld", (long long) t[0]);
    SCHECK(sun_up(lat, lon, t[1] - 600) && !sun_up(lat, lon, t[1] + 600), "sunset at %lld", (long long) t[1]);
  }
  SCHECK(!sun_up(lat, lon, 1782042000 + 43200) && sun_up(lat, lon, 1782042000), "noon and midnight");
  // The 2024 eclipse's new moon, the March 2025 eclipse's full moon
  SCHECK(moon_phase(1712600220) == 0 && moon_phase(1741935300) == 4, "the moon's phases: %d %d",
         moon_phase(1712600220), moon_phase(1741935300));
  // Weather's sky when it knows, else the sun's place
  App *const kept = app_slots().app[(int) Display::WEATHER];
  app_slots().app[(int) Display::WEATHER] = nullptr;
  AppSky f;
  app_env().still = true;
  sky_fill(f, "a1b2c3", lat, lon, 1782042000);
  SCHECK((f.flags & kSkyDay) && (f.flags & kSkyStill) && !(f.flags & kSkyWx) && f.cover <= 2 && f.fall == 0,
         "no Weather: by day from the sun, seeded clouds");
  app_env().still = false;
  sky_fill(f, "a1b2c3", lat, lon, 1782042000 + 43200);
  SCHECK(!(f.flags & kSkyDay) && !(f.flags & kSkyStill) && f.seed == sky_seed("a1b2c3", 1782042000 + 43200),
         "at midnight, moving");
  WeatherSky wx;
  app_slots().app[(int) Display::WEATHER] = &wx;
  sky_fill(f, "a1b2c3", lat, lon, 1782042000 + 43200);
  SCHECK((f.flags & kSkyDay) && (f.flags & kSkyWx) && f.cover == 3 && f.fall == 2 && f.fog == 1 && f.wind == 1,
         "Weather's sky wins");
  app_slots().app[(int) Display::WEATHER] = kept;
}

// A doubled band fills its box from the half-size sky.
void test_scale() {
  AppSky one = sky_of(PlaneKind::NONE, 64, 24, false, kSkyDay, 2), two = one;
  two.w = 128;
  two.h = 48;
  two.scale = 2;
  for (uint32_t t : {0u, 3000u}) {
    const std::vector<uint16_t> a = paint(one, t), b = paint(two, t);
    int off = 0;
    for (int y = 0; y < 48; y++)
      for (int x = 0; x < 128; x++)
        off += b[(size_t) y * 128 + x] != a[(size_t) (y / 2) * 64 + x / 2];
    SCHECK(off == 0, "doubled at %u ms: %d pixels off", (unsigned) t, off);
  }
}

}  // namespace

int run_sky_tests(int &total_checks) {
  test_motion();
  test_clarity();
  test_calm();
  test_still();
  test_variety();
  test_day_night();
  test_weather();
  test_scale();
  total_checks += checks;
  printf("sky: %d checks, %d failures\n", checks, failures);
  return failures;
}
