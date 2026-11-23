// The Flight Tracker's scene behind its drawn planes.
#include <cstdio>
#include <cstdlib>
#include <algorithm>
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

AppSky sky_of(PlaneKind k, int w, int h, bool flip, int elev, uint8_t cover = 1, uint8_t fall = 0, int land = 0,
              uint8_t flags = 0) {
  AppSky s;
  s.w = (uint8_t) w;
  s.h = (uint8_t) h;
  s.kind = (uint8_t) k;
  s.flip = flip;
  s.elev = (int8_t) elev;
  s.flags = (uint8_t) (flags | (elev > 0 ? kSkyDay : 0) | (land << kSkyLandShift));
  s.cover = cover;
  s.fall = fall;
  s.moon = 4;
  s.tail = 0xD02020;
  s.seed = kSeed;
  s.home = 0xBEEF01u + (uint32_t) land * 7919u;
  return s;
}

// A pixel's channels, 0 to 255
struct Rgb {
  int r, g, b;
};
Rgb rgb_of(uint16_t c) { return Rgb{(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31}; }

// A color as the scene lights it, rounded
uint16_t lit565(uint32_t c) {
  auto q = [](int v, int max, int min) {
    const int n = v < 8 ? 0 : ((v * (max == 31 ? 498 : 1012) >> 8) + 8) >> 4;
    return v >= 8 && n < min ? min : n;
  };
  return (uint16_t) (q((c >> 16) & 255, 31, 1) << 11 | q((c >> 8) & 255, 63, 2) << 5 | q(c & 255, 31, 1));
}

// Rows that are sky whatever the land
int sky_rows(int h) { return h - (h / 8 < 2 ? 2 : h / 8) - 2 * ((3 * h + 8) / 16) - 2; }

const int kBoxes[][2] = {{64, 16}, {56, 21}, {45, 21}, {128, 28}, {128, 48}};
const int kElevs[] = {-30, -18, -13, -9, -6, -3, -1, 0, 1, 3, 6, 10, 15, 30, 45, 60};

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

int count16(const std::vector<uint16_t> &px, uint16_t c565) {
  int n = 0;
  for (uint16_t c : px)
    n += c == c565;
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
  const int sc = s.scale < 1 ? 1 : s.scale;
  auto add = [&](int x, int y) {
    for (int j = 0; j < sc; j++)
      for (int i = 0; i < sc; i++)
        out.insert((y * sc + j) * s.w + (s.flip ? s.w - 1 - (x * sc + i) : x * sc + i));
  };
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
  // Steps where the plane moved a row
  std::vector<char> moved(steps, 0);
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
    // The plane's own swing is motion, not change
    std::set<int> band;
    if (const PlaneSprite *sp = flights::plane_sprite((PlaneKind) s.kind)) {
      int x0, y0, x1, y1;
      sky_plane_at(s, *sp, t, x0, y0);
      sky_plane_at(s, *sp, t > kStep ? t - kStep : 0, x1, y1);
      SCHECK(std::abs(y0 - y1) <= 1, "%s: the plane moves a row at most a step", what);
      moved[k] = k > 0 && y0 != y1;
      const int sc = s.scale < 1 ? 1 : s.scale;
      for (int y = std::min(y0, y1) * sc - 1; y <= (std::max(y0, y1) + sp->h) * sc; y++)
        for (int x = x0 * sc - 1; x <= (x0 + sp->w) * sc; x++)
          if (x >= 0 && y >= 0 && x < s.w && y < s.h)
            band.insert(y * s.w + (s.flip ? s.w - 1 - x : x));
    }
    if (!prev.empty()) {
      int changed = 0;
      for (int i = 0; i < n; i++)
        changed += px[i] != prev[i] && band.count(i) == 0;
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
  int flicker = 0, at = -1, areas = 0, area_at = -1, spin = 0, spin_at = -1;
  std::vector<int> series(steps);
  for (int i = 0; i < n; i++) {
    for (int k = 0; k < steps; k++)
      series[k] = lum[k][i];
    if (crosses(series) <= 6)
      continue;
    // Rotors and propellers too, the wave's steps held
    if (spot.count(i) != 0) {
      for (int k = 1; k < steps; k++)
        series[k] = moved[k] ? series[k - 1] : series[k];
      if (crosses(series) <= 6)
        continue;
      spin++;
      spin_at = i;
    } else {
      flicker++;
      at = i;
    }
  }
  SCHECK(spin == 0, "%s: %d rotor or propeller pixels flash more than 3 times a second (one at %d,%d)", what, spin,
         spin_at < 0 ? 0 : spin_at % s.w, spin_at < 0 ? 0 : spin_at / s.w);
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

// The old drawing's plane: each lit cell and its ink
std::vector<std::pair<int, uint8_t>> plane_cells(const AppSky &s, uint32_t t) {
  const PlaneKind kind = (PlaneKind) s.kind;
  const PlaneSprite *sp = flights::plane_sprite(kind);
  const flights::PropSpot *ps = flights::prop_spot(kind);
  const bool rotor = flights::plane_motion(kind) == PlaneMotion::ROTOR;
  const uint32_t frame = (t + kSkySpinMs * 5 / 2) / kSkySpinMs % 4;
  std::vector<uint8_t> grid((size_t) sp->w * sp->h, 0);
  for (int y = 0; y < sp->h; y++)
    for (int x = 0; x < sp->w; x++) {
      const uint8_t v = flights::sprite_px(*sp, x, y);
      if (v == 0 || (rotor && y == 0))
        continue;
      if (ps != nullptr && x == ps->x && y >= ps->y0 && y <= ps->y1 && v == flights::kArtDetail) {
        int n = 0, j = 0;
        for (int r = ps->y0; r <= ps->y1; r++)
          if (flights::sprite_px(*sp, ps->x, r) == flights::kArtDetail) {
            j += r < y;
            n++;
          }
        const bool on = frame == 0   ? true
                        : frame == 2 ? j == 0 || j == n - 1
                                     : n % 2 ? j == n / 2 : j == n / 2 - 1 || j == n / 2;
        if (!on)
          continue;
      }
      const bool up = rotor && x == 0 && y == 3 && (frame & 1);
      grid[(size_t) (y - up) * sp->w + x] = v;
    }
  if (rotor) {
    static const int kHalfs[4] = {9, 6, 3, 6};
    const int half = kHalfs[frame] * flights::kRotorHalf / 9;
    for (int x = flights::kRotorMid - half; x <= flights::kRotorMid + half; x++)
      grid[(size_t) x] = flights::kArtDetail;
  }
  int px, py;
  sky_plane_at(s, *sp, t, px, py);
  const int sc = s.scale;
  std::vector<std::pair<int, uint8_t>> out;
  for (int y = 0; y < sp->h; y++)
    for (int x = 0; x < sp->w; x++)
      if (const uint8_t v = grid[(size_t) y * sp->w + x])
        for (int j = 0; j < sc; j++)
          for (int i = 0; i < sc; i++) {
            const int lx = (px + x) * sc + i, ly = (py + y) * sc + j;
            out.push_back({ly * s.w + (s.flip ? s.w - 1 - lx : lx), v});
          }
  return out;
}

// The plane drawn whole, a dark halo round it.
void test_plane() {
  const uint16_t ink[4] = {0, rgb565(flights::kColBody), rgb565(0xD02020), rgb565(flights::kColDetail)};
  for (PlaneKind k : kKinds) {
    const PlaneSprite *sp = flights::plane_sprite(k);
    for (int sc = 1; sc <= 3; sc++)
      for (int box = 0; box < 2; box++)
        for (int flip = 0; flip < 2; flip++)
          for (int land = 0; land < 5; land++) {
            const int w = sc > 1 ? 128 : box ? 64 : 56, h = sc > 1 ? 48 : box ? 16 : 21;
            if (sp->w * sc > w || sp->h * sc > h || (sc > 1 && box))
              continue;
            const int elev = land == 0 ? -25 : land == 1 ? 0 : 40;
            AppSky s = sky_of(k, w, h, flip, elev, (uint8_t) (land % 4), land == 4, land);
            s.scale = (uint8_t) sc;
            s.wind = 1;
            s.fog = land == 3;
            int bad = 0, out = 0, halo = 0;
            size_t cells = 0;
            for (uint32_t t = 0; t < 16000; t += 300) {
              const std::vector<uint16_t> px = paint(s, t);
              const std::vector<std::pair<int, uint8_t>> ref = plane_cells(s, t);
              std::set<int> on;
              for (const auto &c : ref) {
                out += c.first < 0 || c.first >= w * h;
                if (c.first < 0 || c.first >= w * h)
                  continue;
                bad += px[(size_t) c.first] != ink[c.second];
                on.insert(c.first);
              }
              cells = std::max(cells, on.size());
              for (int i : on)
                for (int d = 0; d < 9; d++) {
                  const int x = i % w + d % 3 - 1, y = i / w + d / 3 - 1;
                  if (x >= 0 && y >= 0 && x < w && y < h && on.count(y * w + x) == 0)
                    halo += luma(px[(size_t) (y * w + x)]) > 58;
                }
            }
            SCHECK(bad == 0 && out == 0, "kind %d x%d %dx%d flip %d land %d: the plane's pixels as before (%d off, %d out)",
                   (int) k, sc, w, h, flip, land, bad, out);
            SCHECK(halo == 0, "kind %d x%d land %d: a dark halo round the plane (%d bright)", (int) k, sc, land, halo);
            SCHECK(cells >= (size_t) sc * sc * 20, "kind %d x%d: a plane of %zu pixels", (int) k, sc, cells);
          }
  }
  // A blue tail at midday: black beside it
  AppSky b = sky_of(PlaneKind::NARROW, 56, 21, false, 40, 0);
  b.tail = 0x2040D0;
  const std::vector<uint16_t> px = paint(b, 0);
  const uint16_t tail = rgb565(b.tail);
  int dark = 0, beside = 0;
  for (int y = 0; y < b.h; y++)
    for (int x = 0; x < b.w; x++)
      if (px[(size_t) y * b.w + x] == tail)
        for (int d = 0; d < 9; d++) {
          const int nx = x + d % 3 - 1, ny = y + d / 3 - 1;
          const uint16_t c = px[(size_t) ny * b.w + nx];
          if (c != tail && c != rgb565(flights::kColBody) && c != rgb565(flights::kColDetail)) {
            beside++;
            dark += c == 0;
          }
        }
  SCHECK(beside > 0 && dark == beside, "a blue tail's halo is black by day: %d of %d", dark, beside);
}

// Lit channels 8 up, blue sky, plane brightest.
void test_lit() {
  const int body = luma(rgb565(flights::kColBody));
  for (const auto &b : kBoxes)
    for (int elev : kElevs)
      for (int am = 0; am < 2; am++)
        for (int wx = 0; wx < 7; wx++)
          for (int land = 0; land < 5; land++) {
            AppSky s = sky_of(PlaneKind::NONE, b[0], b[1], false, elev, (uint8_t) (wx < 4 ? wx : 3),
                              (uint8_t) (wx == 4 ? 1 : wx == 5 ? 2 : 0), land, am ? kSkyAm : 0);
            s.fog = wx == 6;
            s.wind = wx == 2;
            s.seed = kSeed + (uint32_t) (elev * 31 + land);
            for (uint32_t t : {0u, 5300u}) {
              const std::vector<uint16_t> px = paint(s, t);
              int dim = 0, pale = 0, bright = 0;
              for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++) {
                  const Rgb c = rgb_of(px[(size_t) y * s.w + x]);
                  dim += (c.r > 0 && c.r < 8) || (c.g > 0 && c.g < 8) || (c.b > 0 && c.b < 8);
                  if (y >= sky_rows(s.h))
                    continue;
                  pale += c.b < 24;
                  // Stars, the moon and the sun's disc aside
                  bright += elev > 0 && s.h < 40 && luma(px[(size_t) y * s.w + x]) > body;
                }
              SCHECK(dim == 0, "%dx%d elev %d %s wx %d land %d: %d half-lit pixels", b[0], b[1], elev, am ? "am" : "pm", wx,
                     land, dim);
              SCHECK(pale == 0, "%dx%d elev %d wx %d land %d: %d sky pixels under blue 24", b[0], b[1], elev, wx, land,
                     pale);
              SCHECK(bright == 0, "%dx%d elev %d wx %d: %d sky pixels past the plane's white", b[0], b[1], elev, wx, bright);
            }
          }
}

// A clear sky brightens from the top down.
void test_palette() {
  for (const auto &b : kBoxes)
    for (int elev : kElevs)
      for (int am = 0; am < 2; am++) {
        AppSky s = sky_of(PlaneKind::NONE, b[0], b[1], false, elev, 0, 0, 0, (uint8_t) (kSkyStill | (am ? kSkyAm : 0)));
        s.moon = 0;
        const std::vector<uint16_t> px = paint(s, 0);
        int last = -1, falls = 0;
        for (int y = 0; y < sky_rows(s.h); y++) {
          std::vector<int> row;
          for (int x = 0; x < s.w; x++)
            row.push_back(luma(px[(size_t) y * s.w + x]));
          std::sort(row.begin(), row.end());
          // The median: a star is not the sky
          const int m = row[row.size() / 2];
          falls += m + 6 < last;
          last = std::max(last, m);
        }
        SCHECK(falls == 0, "%dx%d elev %d %s: %d rows darker than one above", b[0], b[1], elev, am ? "am" : "pm", falls);
        SCHECK(last >= 2, "%dx%d elev %d: the sky is lit (%d)", b[0], b[1], elev, last);
      }
}

// The glow sits by the sun; morning is not evening.
void test_times() {
  for (const auto &b : kBoxes) {
    AppSky pm = sky_of(PlaneKind::NONE, b[0], b[1], false, 0, 0, 0, 0, kSkyStill), am = pm;
    am.flags |= kSkyAm;
    const std::vector<uint16_t> e = paint(pm, 0), m = paint(am, 0);
    SCHECK(e != m, "%dx%d: sunrise and sunset differ", b[0], b[1]);
    long el = 0, er = 0, ml = 0, mr = 0;
    for (int y = 0; y < pm.h; y++)
      for (int x = 0; x < pm.w / 4; x++) {
        el += luma(e[(size_t) y * pm.w + x]);
        er += luma(e[(size_t) y * pm.w + pm.w - 1 - x]);
        ml += luma(m[(size_t) y * pm.w + x]);
        mr += luma(m[(size_t) y * pm.w + pm.w - 1 - x]);
      }
    SCHECK(er > el && ml > mr, "%dx%d: the glow right at sunset, left at sunrise", b[0], b[1]);
    // Noon is the same sky either side of it
    AppSky n1 = sky_of(PlaneKind::NONE, b[0], b[1], false, 45, 0, 0, 0, kSkyStill), n2 = n1;
    n2.flags |= kSkyAm;
    int off = 0;
    const std::vector<uint16_t> a = paint(n1, 0), c = paint(n2, 0);
    for (int i = 0; i < sky_rows(n1.h) * n1.w; i++)
      off += a[(size_t) i] != c[(size_t) i];
    SCHECK(off <= (b[1] >= 40 ? 130 : 0), "%dx%d: noon the same from either side (%d off)", b[0], b[1], off);
  }
  // Night: stars and a moon; none under cloud
  for (uint32_t seed = 1; seed <= 6; seed++)
    for (const auto &b : kBoxes) {
      AppSky n = sky_of(PlaneKind::NONE, b[0], b[1], false, -25, 0, 0, 0, kSkyStill), o = n, d = n;
      n.seed = o.seed = d.seed = seed;
      o.cover = 3;
      d.elev = 40;
      auto lights = [&](const AppSky &s) {
        const std::vector<uint16_t> px = paint(s, 0);
        int k = 0;
        for (int i = 0; i < sky_rows(s.h) * s.w; i++)
          k += luma(px[(size_t) i]) >= 40 && rgb_of(px[(size_t) i]).r >= 40;
        return k;
      };
      const int clear = lights(n);
      SCHECK(clear >= 10 && lights(o) == 0, "%dx%d seed %u: stars and a moon on a clear night (%d), none overcast (%d)",
             b[0], b[1], (unsigned) seed, clear, lights(o));
      n.moon = 0;
      const int stars = lights(n);
      SCHECK(stars >= 1 && stars < clear && stars <= 40, "%dx%d seed %u: %d stars, no new moon", b[0], b[1], (unsigned) seed,
             stars);
      n.moon = 1;
      SCHECK(lights(n) > stars && lights(n) < clear, "a crescent, then a full moon");
    }
  // The sun's disc on tall boxes only, never under cloud
  auto suns = [](int w, int h, int elev, uint8_t cover) {
    const std::vector<uint16_t> px = paint(sky_of(PlaneKind::NONE, w, h, false, elev, cover, 0, 0, kSkyStill), 0);
    int k = 0;
    for (uint16_t c : px)
      k += rgb_of(c).r == 255 && rgb_of(c).g > 200;
    return k;
  };
  SCHECK(suns(128, 48, 6, 0) >= 30 && suns(128, 48, 40, 0) >= 30 && suns(128, 48, 40, 3) == 0 && suns(128, 48, -10, 0) == 0,
         "a sun by day on the tall box");
  SCHECK(suns(56, 21, 6, 0) == 0 && suns(64, 16, 40, 0) == 0 && suns(128, 28, 40, 0) == 0, "none on small boxes");
  // Birds now and then by day only
  for (uint32_t seed = 1; seed <= 4; seed++) {
    AppSky d = sky_of(PlaneKind::LIGHT, 56, 21, false, 40, 0), n = d;
    n.elev = -25;
    d.seed = n.seed = seed;
    const uint16_t bird = lit565(0x1C1C28);
    int by_day = 0, by_night = 0;
    for (uint32_t t = 0; t < kEnd; t += 500) {
      by_day += count16(paint(d, t), bird);
      by_night += count16(paint(n, t), bird);
    }
    SCHECK(by_day > 0 && by_night == 0, "seed %u: birds by day only (%d, %d)", (unsigned) seed, by_day, by_night);
  }
}

// A home has one view, for every plane.
void test_home() {
  const int32_t lat = 407100, lon = -740100;
  SCHECK(sky_home(lat, lon) == sky_home(lat + 300, lon - 300) && sky_home(lat, lon) != sky_home(lat + 2000, lon) &&
             sky_home(lat, lon) != sky_home(lat, lon + 2000) && sky_home(INT32_MIN, 0) == 0,
         "a home is a tenth of a degree");
  std::set<int> lands;
  for (int k = 0; k < 40; k++)
    lands.insert((int) (sky_home(lat + 3000 * k, lon + 7000 * k) % 5));
  SCHECK(lands.size() == 5, "five sceneries across homes: %zu", lands.size());
  for (const auto &b : kBoxes)
    for (int land = 0; land < 5; land++)
      for (int elev : {-25, 0, 40}) {
        AppSky a = sky_of(PlaneKind::NONE, b[0], b[1], false, elev, 0, 0, land, kSkyStill), c = a, o = a;
        a.moon = c.moon = o.moon = 0;
        c.seed = 0xC0FFEEu;
        o.home ^= 0x5A5A5A5Au;
        const std::vector<uint16_t> pa = paint(a, 0), pc = paint(c, 0);
        // A short shore can match; three homes never all do
        bool other = false;
        for (uint32_t k = 1; k <= 3; k++) {
          o.home = a.home ^ (0x5A5A5A5Au * k);
          other = other || paint(o, 0) != pa;
        }
        const int base = b[1] / 8 < 2 ? 2 : b[1] / 8, from = (b[1] - base) * b[0];
        SCHECK(std::equal(pa.begin() + from, pa.end(), pc.begin() + from), "%dx%d land %d elev %d: one land for every plane",
               b[0], b[1], land, elev);
        SCHECK(other, "%dx%d land %d elev %d: another home, another view", b[0], b[1], land, elev);
      }
  // Each scenery is its own
  for (int land = 1; land < 5; land++) {
    AppSky a = sky_of(PlaneKind::NONE, 128, 48, false, 40, 0, 0, 0, kSkyStill), c = a;
    c.flags = (uint8_t) (kSkyStill | kSkyDay | (land << kSkyLandShift));
    SCHECK(paint(a, 0) != paint(c, 0), "scenery %d differs from hills", land);
  }
}

// Lamps and windows at night, few on small boxes.
void test_lights() {
  const uint16_t lamp = lit565(0xFF9628), window = lit565(0xFFAA28), mast = lit565(0xDC1E14);
  for (const auto &b : kBoxes)
    for (int land = 0; land < 5; land++) {
      int most = 0, any = 0, by_day = 0, masts = 0;
      for (uint32_t t = 0; t < 60000; t += 700) {
        const std::vector<uint16_t> n = paint(sky_of(PlaneKind::NONE, b[0], b[1], false, -25, 0, 0, land), t);
        const std::vector<uint16_t> d = paint(sky_of(PlaneKind::NONE, b[0], b[1], false, 30, 0, 0, land), t);
        const int k = count16(n, lamp) + count16(n, window);
        most = std::max(most, k);
        any += k;
        masts = std::max(masts, count16(n, mast));
        by_day += count16(d, lamp) + count16(d, window) + count16(d, mast);
        // None in the sky
        for (int i = 0; i < sky_rows(b[1]) * b[0]; i++)
          by_day += n[(size_t) i] == lamp || n[(size_t) i] == window;
        // Tall boxes: windows above the street only
        const int hy = b[1] - (b[1] / 8 < 2 ? 2 : b[1] / 8);
        for (int i = hy * b[0]; land == 2 && b[1] >= 40 && i < b[0] * b[1]; i++)
          by_day += n[(size_t) i] == window;
      }
      const int cap = land == 2 ? (b[1] >= 40 ? 4000 : b[1] < 21 ? 4 : 6) : (b[0] + 6) / 24 + 1;
      SCHECK(by_day == 0 && (land == 3 || any > 0) && most <= cap && masts <= 1,
             "%dx%d land %d: lights at night only, %d at most (cap %d), %d masts", b[0], b[1], land, most, cap, masts);
    }
}

void test_still() {
  for (PlaneKind k : kKinds)
    for (int land = 0; land < 5; land++) {
      AppSky s = sky_of(k, 56, 21, false, land % 2 ? 30 : -25, 2, land == 4, land, kSkyStill);
      SCHECK(paint(s, 0) == paint(s, 5000) && paint(s, 0) == paint(s, 63300), "kind %d land %d: still in night hours or dim",
             (int) k, land);
    }
  // An unknown type: a still picture in a still scene
  AppSky g = sky_of(PlaneKind::GENERIC, 56, 21, false, 30, 2, 1);
  SCHECK(paint(g, 0) == paint(g, 7700), "an unknown type holds still");
  AppSky j = sky_of(PlaneKind::NARROW, 56, 21, false, 30, 2);
  SCHECK(paint(j, 0) != paint(j, 1500), "a jet's scene moves");
  AppSky band = sky_of(PlaneKind::NONE, 128, 28, false, 30, 2);
  SCHECK(paint(band, 0) != paint(band, 4000), "a note's scene drifts");
  // The same inputs, the same picture
  SCHECK(paint(j, 2500) == paint(j, 2500), "repeatable");
}

// Flip turns the plane and the drift, never the world.
void test_flip() {
  for (const auto &b : kBoxes)
    for (int land = 0; land < 5; land++) {
      AppSky a = sky_of(PlaneKind::NONE, b[0], b[1], false, 3, 2, 0, land, kSkyStill), f = a;
      f.flip = 1;
      SCHECK(paint(a, 0) == paint(f, 0), "%dx%d land %d: the world as it was, flipped or not", b[0], b[1], land);
      a.flags &= (uint8_t) ~kSkyStill;
      f.flags &= (uint8_t) ~kSkyStill;
      SCHECK(paint(a, 8000) != paint(f, 8000), "%dx%d land %d: the land slides the other way", b[0], b[1], land);
    }
  AppSky a = sky_of(PlaneKind::NARROW, 56, 21, false, 3, 0, 0, 0, kSkyStill), f = a;
  f.flip = 1;
  const std::vector<uint16_t> pa = paint(a, 0), pf = paint(f, 0);
  const uint16_t body = rgb565(flights::kColBody);
  int mirrored = 0, same_sky = 0, n = 0;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) {
      const uint16_t c = pa[(size_t) y * a.w + x], m = pf[(size_t) y * a.w + a.w - 1 - x];
      mirrored += (c == body) != (m == body);
      // The top row: clear of the plane and its trail
      if (y == 0) {
        n++;
        same_sky += c == pf[(size_t) y * a.w + x];
      }
    }
  SCHECK(mirrored == 0 && same_sky == n, "the plane mirrors, the sky does not (%d, %d of %d)", mirrored, same_sky, n);
}

void test_variety() {
  const int64_t now = 1794457500;
  SCHECK(sky_seed("a1b2c3", now) != sky_seed("a1b2c4", now), "planes differ");
  SCHECK(sky_seed("a1b2c3", now) != sky_seed("a1b2c3", now + 600), "new clouds every 10 minutes");
  SCHECK(sky_seed("a1b2c3", now) == sky_seed("a1b2c3", now + 1), "the same sky meanwhile");
  int differ = 0;
  for (uint32_t k = 0; k < 8; k++) {
    AppSky a = sky_of(PlaneKind::NARROW, 56, 21, false, 30, 2), b = a;
    a.seed = sky_seed("a1b2c3", now + 600 * k);
    b.seed = sky_seed("a00001", now + 600 * k);
    differ += paint(a, 0) != paint(b, 0);
  }
  SCHECK(differ >= 7, "different planes, different clouds: %d of 8", differ);
}

void test_calm() {
  char what[96];
  for (PlaneKind k : kKinds)
    for (int elev : {-25, 2, 40})
      for (uint8_t fall : {0, 1, 2}) {
        for (int w : {56, 64}) {
          AppSky s = sky_of(k, w, w == 64 ? 16 : 21, k == PlaneKind::NARROW, elev, fall ? 3 : 2, fall, (int) k % 5);
          s.wind = 1;
          s.fog = fall == 2;
          snprintf(what, sizeof what, "kind %d %dx%d elev %d fall %d", (int) k, s.w, s.h, elev, fall);
          check_calm(s, what);
        }
      }
  // The full-screen card's scene, doubled and tripled
  for (PlaneKind k : kKinds)
    for (int sc : {2, 3})
      for (int elev : {-25, 2, 40}) {
        const PlaneSprite *sp = flights::plane_sprite(k);
        if (sp->w * sc > 128 || sp->h * sc > 48)
          continue;
        AppSky s = sky_of(k, 128, 48, k == PlaneKind::NARROW, elev, 1, 0, (int) k % 5);
        s.scale = (uint8_t) sc;
        s.wind = 1;
        snprintf(what, sizeof what, "kind %d x%d 128x48 elev %d", (int) k, sc, elev);
        check_calm(s, what);
      }
  // A clear or partly cloudy coast, low sun and high
  for (int h : {28, 48})
    for (uint8_t cover : {0, 1})
      for (int elev : {2, 40}) {
        AppSky s = sky_of(PlaneKind::NONE, 128, h, false, elev, cover, 0, 3);
        snprintf(what, sizeof what, "coast 128x%d cover %d elev %d", h, cover, elev);
        check_calm(s, what);
      }
  // The scene alone, over a note, at each band
  for (const auto &b : {std::make_pair(64, 24), std::make_pair(128, 28), std::make_pair(128, 48)})
    for (int elev : {-25, 2, 40})
      for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
        AppSky s = sky_of(PlaneKind::NONE, b.first, b.second, false, elev, seed % 4, seed == 3, (int) seed % 5);
        s.seed = seed;
        s.wind = seed == 2;
        snprintf(what, sizeof what, "band %dx%d elev %d seed %u", s.w, s.h, elev, (unsigned) seed);
        check_calm(s, what);
      }
}

void test_weather() {
  AppSky c = sky_of(PlaneKind::NONE, 128, 28, false, 40, 0, 0, 0, kSkyStill), o = c, p = c;
  o.cover = 3;
  p.cover = 2;
  const std::vector<uint16_t> pc = paint(c, 0), po = paint(o, 0), pp = paint(p, 0);
  long blue_c = 0, blue_o = 0;
  int streaks = 0;
  for (int i = 0; i < sky_rows(c.h) * c.w; i++) {
    blue_c += rgb_of(pc[(size_t) i]).b - rgb_of(pc[(size_t) i]).r;
    blue_o += rgb_of(po[(size_t) i]).b - rgb_of(po[(size_t) i]).r;
    streaks += pp[(size_t) i] != pc[(size_t) i];
  }
  SCHECK(blue_o < blue_c * 3 / 4, "overcast greys the sky: %ld against %ld", blue_o, blue_c);
  SCHECK(streaks >= 10 && streaks <= 4 * 60, "a few thin clouds when partly cloudy: %d pixels", streaks);
  for (int y = c.h * 2 / 5; y < sky_rows(c.h); y++)
    for (int x = 0; x < c.w; x++)
      streaks -= pp[(size_t) y * c.w + x] != pc[(size_t) y * c.w + x] ? 100000 : 0;
  SCHECK(streaks > 0, "clouds in the top of the sky only");
  AppSky r = sky_of(PlaneKind::NONE, 128, 28, false, 40, 3, 1);
  SCHECK(count16(paint(r, 0), lit565(0x466096)) >= 3, "rain falls");
  r.fall = 2;
  SCHECK(count16(paint(r, 0), lit565(0xB8C0D0)) >= 2, "snow falls");
  // Fog lies in the lower half
  AppSky f = c;
  f.fog = 1;
  const std::vector<uint16_t> pf = paint(f, 0);
  int top = 0, low = 0;
  for (int i = 0; i < c.w * c.h; i++)
    (i / c.w < c.h / 2 ? top : low) += pf[(size_t) i] != pc[(size_t) i];
  SCHECK(top == 0 && low > c.w * 4, "fog in the lower half: %d above, %d below", top, low);
  // The sun at Versailles, sunrise and sunset in June and December
  const int32_t lat = 488049, lon = 21204;
  const int64_t times[][2] = {{1782013740, 1782071940}, {1797838920, 1797868680}};
  for (const auto &t : times) {
    bool up;
    SCHECK(!sun_up(lat, lon, t[0] - 600) && sun_up(lat, lon, t[0] + 600), "sunrise at %lld", (long long) t[0]);
    SCHECK(sun_up(lat, lon, t[1] - 600) && !sun_up(lat, lon, t[1] + 600), "sunset at %lld", (long long) t[1]);
    SCHECK(std::abs(sun_elev(lat, lon, t[0], up)) <= 1 && up, "on the horizon and rising at sunrise");
    SCHECK(std::abs(sun_elev(lat, lon, t[1], up)) <= 1 && !up, "on the horizon and setting at sunset");
  }
  bool up;
  SCHECK(!sun_up(lat, lon, 1782042000 + 43200) && sun_up(lat, lon, 1782042000), "noon and midnight");
  const int noon = sun_elev(lat, lon, 1782042000, up), night = sun_elev(lat, lon, 1782042000 + 43200, up);
  SCHECK(noon >= 62 && noon <= 66 && night <= -15 && night >= -20, "June's noon and midnight heights: %d %d", noon, night);
  // The 2024 eclipse's new moon, the March 2025 eclipse's full moon
  SCHECK(moon_phase(1712600220) == 0 && moon_phase(1741935300) == 4, "the moon's phases: %d %d",
         moon_phase(1712600220), moon_phase(1741935300));
  // Weather's sky when it knows; the sun's place always
  App *const kept = app_slots().app[(int) Display::WEATHER];
  app_slots().app[(int) Display::WEATHER] = nullptr;
  AppSky s;
  app_env().still = true;
  sky_fill(s, "a1b2c3", lat, lon, 1782042000);
  SCHECK((s.flags & kSkyDay) && (s.flags & kSkyStill) && !(s.flags & kSkyWx) && s.cover <= 2 && s.fall == 0 &&
             s.elev == 60,
         "no Weather: by day from the sun, seeded clouds, elev %d", s.elev);
  SCHECK(s.home == sky_home(lat, lon) && (s.flags >> kSkyLandShift) == s.home % 5, "the home's scenery");
  app_env().still = false;
  sky_fill(s, "a1b2c3", lat, lon, 1782042000 + 43200);
  SCHECK(!(s.flags & kSkyDay) && !(s.flags & kSkyStill) && s.seed == sky_seed("a1b2c3", 1782042000 + 43200) &&
             s.elev == night,
         "at midnight, moving");
  sky_fill(s, "a1b2c3", lat, lon, times[0][0] + 1200);
  const uint8_t am = s.flags & kSkyAm;
  sky_fill(s, "a1b2c3", lat, lon, times[0][1] - 1200);
  SCHECK(am != 0 && !(s.flags & kSkyAm) && s.elev >= 0 && s.elev <= 6, "morning, then evening: elev %d", s.elev);
  WeatherSky wx;
  app_slots().app[(int) Display::WEATHER] = &wx;
  sky_fill(s, "a1b2c3", lat, lon, 1782042000 + 43200);
  SCHECK(!(s.flags & kSkyDay) && (s.flags & kSkyWx) && s.cover == 3 && s.fall == 2 && s.fog == 1 && s.wind == 1,
         "Weather's clouds, the sun's own night");
  app_slots().app[(int) Display::WEATHER] = kept;
  // No home: the clock's hours stand in
  sky_fill(s, "a1b2c3", INT32_MIN, 0, 1782042000);
  SCHECK(s.home == 0 && (s.flags >> kSkyLandShift) == 0 && s.elev >= -30 && s.elev <= 60, "no home: hills by the clock");
  // Level at 06:00 and 18:00, highest at noon
  const auto local = app_env().local;
  app_env().local = nullptr;
  const int64_t day = 1782000000;
  const int want[3] = {0, 50, 0};
  for (int k = 0; k < 3; k++) {
    sky_fill(s, "a1b2c3", INT32_MIN, 0, day + 21600 * (k + 1));
    SCHECK(s.elev == want[k], "no home at %02d:00: elev %d, wants %d", 6 * (k + 1), s.elev, want[k]);
  }
  sky_fill(s, "a1b2c3", INT32_MIN, 0, day);
  SCHECK(s.elev == -30, "no home at midnight: elev %d", s.elev);
  app_env().local = local;
}

// The sun's disc stays clear of the plane.
void test_sun_clear() {
  const uint16_t sun[2] = {lit565(0xFFD66E), lit565(0xFFECAA)};
  int seen = 0;
  for (PlaneKind k : kKinds) {
    const PlaneSprite *sp = flights::plane_sprite(k);
    for (int sc : {2, 3})
      for (int flip = 0; flip < 2; flip++)
        for (int am = 0; am < 2; am++)
          for (int elev = 0; elev <= 60; elev++) {
            if (sp->w * sc > 128 || sp->h * sc > 48)
              continue;
            AppSky s = sky_of(k, 128, 48, flip, elev, 0, 0, 0, am ? kSkyAm : 0);
            s.scale = (uint8_t) sc;
            // The box over the wave, 2 px round
            int bx, lo = 1 << 20, hi = -(1 << 20);
            for (uint32_t t = 0; t < 8000; t += 100) {
              int y;
              sky_plane_at(s, *sp, t, bx, y);
              lo = std::min(lo, y);
              hi = std::max(hi, y);
            }
            const int x0 = bx * sc - 2, x1 = (bx + sp->w) * sc + 1, y0 = lo * sc - 2, y1 = (hi + sp->h) * sc + 1;
            int in = 0;
            for (uint32_t t : {0u, 1800u, 5400u}) {
              const std::vector<uint16_t> px = paint(s, t);
              for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++) {
                  const uint16_t c = px[(size_t) y * s.w + (flip ? s.w - 1 - x : x)];
                  if (c != sun[0] && c != sun[1])
                    continue;
                  seen++;
                  in += x >= x0 && x <= x1 && y >= y0 && y <= y1;
                }
            }
            SCHECK(in == 0, "kind %d x%d flip %d %s elev %d: %d sun pixels by the plane", (int) k, sc, flip,
                   am ? "am" : "pm", elev, in);
          }
  }
  SCHECK(seen > 0, "the sun shows beside the plane");
}

// The scene draws at full size whatever the plane's scale.
void test_scale() {
  AppSky one = sky_of(PlaneKind::NONE, 128, 48, false, 30, 2), two = one;
  two.scale = 2;
  SCHECK(paint(one, 3000) == paint(two, 3000), "a band's scene is not doubled");
  // Too small a box paints dark, in bounds
  AppSky tiny = sky_of(PlaneKind::NONE, 6, 4, false, 30, 2);
  SCHECK(count16(paint(tiny, 0), 0) == 24, "a box too small stays dark");
}

// Moving craft ride a slow wave inside the box
void test_wave() {
  for (PlaneKind k : kKinds) {
    const PlaneSprite *sp = flights::plane_sprite(k);
    if (sp == nullptr)
      continue;
    const bool moves = flights::plane_motion(k) != PlaneMotion::STILL;
    for (int h : {sp->h + 2, 16, 21, 40}) {
      if (h < sp->h)
        continue;
      AppSky s = sky_of(k, 56, h, false, 30, 1);
      int x0, y0, lo = 1 << 20, hi = -(1 << 20), changes = 0, last = 0;
      sky_plane_at(s, *sp, 0, x0, y0);
      for (uint32_t t = 0; t < 12000; t += kSkyFrameMs) {
        int x, y;
        sky_plane_at(s, *sp, t, x, y);
        SCHECK(x == x0, "kind %d h %d: the wave is up and down only", (int) k, h);
        SCHECK(y >= 0 && y + sp->h <= h, "kind %d h %d: stays inside at %u ms", (int) k, h, (unsigned) t);
        lo = std::min(lo, y);
        hi = std::max(hi, y);
        changes += t > 0 && y != last;
        last = y;
      }
      const int room = h - sp->h - 2, amp = !moves || room < 2 ? 0 : room < 6 ? 1 : 2;
      SCHECK(hi - lo == 2 * amp, "kind %d h %d: swings %d, wants %d", (int) k, h, hi - lo, 2 * amp);
      SCHECK(changes <= 4 * amp * 2, "kind %d h %d: %d steps in 12 s, slow enough", (int) k, h, changes);
      // Above the middle, the land under its belly
      SCHECK(lo + hi <= h - sp->h, "kind %d h %d: at or above the middle", (int) k, h);
    }
  }
  AppSky s = sky_of(PlaneKind::NARROW, 56, 40, false, 30, 1, 0, 0, kSkyStill);
  SCHECK(paint(s, 0) == paint(s, 1500), "holds level when still");
}

}  // namespace

int run_sky_tests(int &total_checks) {
  test_wave();
  test_motion();
  test_plane();
  test_lit();
  test_palette();
  test_times();
  test_home();
  test_lights();
  test_calm();
  test_still();
  test_flip();
  test_variety();
  test_weather();
  test_scale();
  test_sun_clear();
  total_checks += checks;
  printf("sky: %d checks, %d failures\n", checks, failures);
  return failures;
}
