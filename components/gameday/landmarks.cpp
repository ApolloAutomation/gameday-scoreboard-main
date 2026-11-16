// Map landmarks: checked once, then drawn under the radars.
#include "landmarks.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#include <cmath>
#include <cstdlib>
#include <cstring>

#if __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define LAND_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define LAND_ALLOC(n) malloc(n)
#endif

#include "map_canvas.h"
#include "weather.h"

namespace espn {

namespace {

int16_t rd16(const uint8_t *p) { return (int16_t) (uint16_t) (p[0] | p[1] << 8); }
uint16_t ru16(const uint8_t *p) { return (uint16_t) (p[0] | p[1] << 8); }
bool off_ok(const uint8_t *p, int la, int lo) {
  const int a = rd16(p), b = rd16(p + 2);
  return a >= -la && a <= la && b >= -lo && b <= lo;
}

const uint8_t *places_at(const LandSet &s) { return s.p + kLandHead; }
const uint8_t *lines_at(const LandSet &s) { return places_at(s) + (size_t) s.np * kLandPlaceBytes; }
const uint8_t *points_at(const LandSet &s) { return lines_at(s) + (size_t) s.nl * 2; }

constexpr uint32_t kRoad = 0x4A3A24, kBorder = 0x505050, kCoast = 0x3A4048;
constexpr uint32_t kDotSmall = 0x808080, kDotBig = 0xA0A0A0, kTextSmall = 0x707070, kTextBig = 0x909090;
constexpr int kFar = 4096;

struct Box {
  int16_t x, y, w, h;
};
struct Cand {
  int16_t x, y;
  int32_t key;
  uint8_t idx;
};
// Label placement's scratch, in PSRAM once
struct Scratch {
  Cand c[kLandMaxPlaces];
  Box taken[6 + 2 * kMapLabelMax];
};

Scratch *scratch() {
  static Scratch *s = nullptr;
  if (s == nullptr)
    s = static_cast<Scratch *>(LAND_ALLOC(sizeof(Scratch)));
  return s;
}

// gap 1: a pixel apart counts as touching.
__attribute__((noinline)) bool meet(const Box &a, const Box &b, int gap) {
  return a.x - gap < b.x + b.w && b.x < a.x + a.w + gap && a.y - gap < b.y + b.h && b.y < a.y + a.h + gap;
}

// The box, a pixel out, meets the circle.
__attribute__((noinline)) bool on_ring(const Box &b, int cx, int cy, int r) {
  if (r <= 0)
    return false;
  const int x0 = b.x - 1 - cx, x1 = b.x + b.w - cx, y0 = b.y - 1 - cy, y1 = b.y + b.h - cy;
  const int nx = x0 > 0 ? x0 : x1 < 0 ? x1 : 0, ny = y0 > 0 ? y0 : y1 < 0 ? y1 : 0;
  const int fx = -x0 > x1 ? x0 : x1, fy = -y0 > y1 ? y0 : y1;
  return nx * nx + ny * ny <= (r + 1) * (r + 1) && fx * fx + fy * fy >= (r - 1) * (r - 1);
}

__attribute__((noinline)) void world(float lat, float lon, float &x, float &y) {
  x = wx::merc_px_x(lon, wx::kRadarZoom);
  y = wx::merc_px_y(lat, wx::kRadarZoom);
}

void walk(Px565 &p, int x0, int y0, int x1, int y1, uint16_t c, bool dotted, int &step) {
  if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) || (x0 >= p.w && x1 >= p.w) || (y0 >= p.h && y1 >= p.h))
    return;
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if (!dotted || (step & 1) == 0)
      p.put(x0, y0, c);
    step++;
    if (x0 == x1 && y0 == y1)
      return;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

__attribute__((noinline)) void set_xy(const MapProj &pr, const LandSet &s, int16_t dlat, int16_t dlon, int &x, int &y) {
  map_xy(pr, s.lat_e2 / 100.0f + dlat / 4000.0f, s.lon_e2 / 100.0f + dlon / 4000.0f, x, y);
}

MapProj proj_of(const AppMap &m) { return map_proj(m.lat_e4, m.lon_e4, m.scale_q16 / 65536.0f, m.w, m.h); }

}  // namespace

const char *land_error_word(LandError e) {
  static const char *const kWords[] = {"ok", "format", "counts", "length", "place", "name", "line", "point"};
  return kWords[(int) e < 8 ? (int) e : 1];
}

void land_reach_box(int lat_e2, int reach_km, int &dlat, int &dlon) {
  const float la = (reach_km + 10) / 111.32f * 4000.0f;
  const float c = cosf(lat_e2 / 100.0f * 0.017453293f);
  dlat = la >= kLandMaxOff ? kLandMaxOff : (int) ceilf(la);
  dlon = c * kLandMaxOff <= la ? kLandMaxOff : (int) ceilf(la / c);
}

LandError land_check(const uint8_t *p, size_t n, LandSet &out) {
  out = LandSet();
  if (p == nullptr || n < kLandHead || n > kLandMaxBytes || p[0] != 'G' || p[1] != 'L' || p[2] != 1 || p[3] != 0)
    return LandError::FORMAT;
  const int lat = rd16(p + 4), lon = rd16(p + 6);
  const size_t np = ru16(p + 8), nl = ru16(p + 10), npt = ru16(p + 12), reach = ru16(p + 14);
  if (lat < -9000 || lat > 9000 || lon < -18000 || lon > 18000 || np > kLandMaxPlaces || nl > kLandMaxLines ||
      npt > kLandMaxPoints || reach < 1 || reach > (size_t) kLandMaxReach)
    return LandError::COUNTS;
  if (n != kLandHead + np * kLandPlaceBytes + nl * 2 + npt * 4)
    return LandError::LENGTH;
  int la, lo;
  land_reach_box(lat, (int) reach, la, lo);
  const uint8_t *q = p + kLandHead;
  for (size_t i = 0; i < np; i++, q += kLandPlaceBytes) {
    const size_t len = q[5];
    if (!off_ok(q, la, lo) || q[4] < 1 || q[4] > 4)
      return LandError::PLACE;
    if (len < 1 || len > (size_t) kLandName || q[6] == ' ' || q[5 + len] == ' ')
      return LandError::NAME;
    for (size_t k = 0; k < (size_t) kLandName; k++) {
      const char c = (char) q[6 + k];
      if (k < len ? (c >= 'a' && c <= 'z') || !font3x5_has(c) : c != 0)
        return LandError::NAME;
    }
  }
  size_t sum = 0;
  for (size_t i = 0; i < nl; i++, q += 2) {
    if (q[0] > 2 || q[1] < 2)
      return LandError::LINE;
    sum += q[1];
  }
  if (sum != npt)
    return LandError::LINE;
  for (size_t i = 0; i < npt; i++, q += 4)
    if (!off_ok(q, la, lo))
      return LandError::POINT;
  out.p = p;
  out.n = n;
  out.lat_e2 = (int16_t) lat;
  out.lon_e2 = (int16_t) lon;
  out.np = (uint16_t) np;
  out.nl = (uint16_t) nl;
  out.npt = (uint16_t) npt;
  out.reach_km = (uint16_t) reach;
  return LandError::OK;
}

int16_t land_e2(int32_t e4) { return (int16_t) ((e4 >= 0 ? e4 + 50 : e4 - 50) / 100); }

void land_place(const LandSet &s, size_t i, LandPlace &out) {
  const uint8_t *q = places_at(s) + i * kLandPlaceBytes;
  out.dlat = rd16(q);
  out.dlon = rd16(q + 2);
  out.cls = q[4];
  out.len = q[5];
  memcpy(out.name, q + 6, kLandName);
  out.name[out.len] = '\0';
}

void land_line(const LandSet &s, size_t i, uint8_t &kind, uint8_t &n) {
  const uint8_t *q = lines_at(s) + i * 2;
  kind = q[0];
  n = q[1];
}

void land_point(const LandSet &s, size_t k, int16_t &dlat, int16_t &dlon) {
  const uint8_t *q = points_at(s) + k * 4;
  dlat = rd16(q);
  dlon = rd16(q + 2);
}

MapProj map_proj(int32_t lat_e4, int32_t lon_e4, float s, int w, int h) {
  MapProj p;
  world(lat_e4 / 10000.0f, lon_e4 / 10000.0f, p.wx, p.wy);
  p.s = s;
  p.ox = (int16_t) (w / 2);
  p.oy = (int16_t) (h / 2);
  return p;
}

bool map_xy(const MapProj &p, float lat, float lon, int &x, int &y) {
  float fx, fy;
  world(lat, lon, fx, fy);
  fx = p.ox + (fx - p.wx) * p.s;
  fy = p.oy + (fy - p.wy) * p.s;
  auto cut = [](float v) { return v < -kFar ? -kFar : v > kFar ? kFar : (int) floorf(v); };
  x = cut(fx);
  y = cut(fy);
  return fabsf(fx) < kFar && fabsf(fy) < kFar;
}

float map_scale_weather(int w, int h) { return (float) (w > h ? w : h) / (float) wx::kTilePx; }

float map_scale_ring(int32_t lat_e4, int ring_px, float radius_m) {
  return radius_m > 0 ? ring_px * wx::ground_m_per_px(lat_e4 / 10000.0f, wx::kRadarZoom) / radius_m : 0.0f;
}

int map_min_class(const AppMap &m) {
  const float s = m.scale_q16 / 65536.0f;
  const float kpp = s > 0 ? wx::ground_m_per_px(m.lat_e4 / 10000.0f, wx::kRadarZoom) / s / 1000.0f : 99.0f;
  return kpp < 1.0f ? 1 : kpp < 3.0f ? 2 : 3;
}

int map_label_cap(const AppMap &m) { return m.w < 128 ? 4 : m.h >= 100 ? 12 : 8; }

bool map_has(const AppMap &m, const LandSet *s) {
  return s != nullptr && s->p != nullptr && m.w > 0 && s->lat_e2 == land_e2(m.lat_e4) &&
         s->lon_e2 == land_e2(m.lon_e4);
}

int map_labels(const AppMap &m, const LandSet &s, MapLabel *out, int cap) {
  Scratch *sc = scratch();
  if (sc == nullptr || !map_has(m, &s) || cap <= 0)
    return 0;
  if (cap > kMapLabelMax)
    cap = kMapLabelMax;
  const MapProj pr = proj_of(m);
  const int min_cls = map_min_class(m);
  Cand *cd = sc->c;
  Box *taken = sc->taken;
  // Candidates: projected, inside, biggest then nearest
  const Box home{(int16_t) (pr.ox - 3), (int16_t) (pr.oy - 3), 6, 6};
  int nc = 0;
  LandPlace pl;
  for (size_t i = 0; i < s.np; i++) {
    land_place(s, i, pl);
    int x, y;
    set_xy(pr, s, pl.dlat, pl.dlon, x, y);
    const int d = pl.cls >= 3 ? 2 : 1;
    if (pl.cls < min_cls || x < 1 || y < 1 || x + d > m.w - 1 || y + d > m.h - 1)
      continue;
    const int32_t k = (int32_t) (4 - pl.cls) << 24 | ((x - pr.ox) * (x - pr.ox) + (y - pr.oy) * (y - pr.oy));
    int at = nc++;
    for (; at > 0 && cd[at - 1].key > k; at--)
      cd[at] = cd[at - 1];
    cd[at] = Cand{(int16_t) x, (int16_t) y, k, (uint8_t) i};
  }
  // Taken: the keeps, the home, then dots and labels
  int nt = 0;
  for (const AppMapKeep &k : m.keep)
    if (k.w > 0 && k.h > 0)
      taken[nt++] = Box{k.x, k.y, k.w, k.h};
  taken[nt++] = home;
  int n = 0;
  // Off the ring first; then a city may cross it
  for (int pass = 0; pass < 2; pass++)
    for (int c = 0; c < nc && n < cap; c++) {
      if (cd[c].idx == 0xFF)
        continue;
      land_place(s, cd[c].idx, pl);
      if (pass == 1 && pl.cls < 3)
        continue;
      const int d = pl.cls >= 3 ? 2 : 1, x = cd[c].x, y = cd[c].y, lw = 4 * pl.len - 1;
      const Box dot{(int16_t) x, (int16_t) y, (int16_t) d, (int16_t) d};
      bool free = true;
      for (int t = 0; t < nt && free; t++)
        free = !meet(dot, taken[t], 0);
      // Right, left, above, below, then the corners
      const int r = x + d + 1, l = x - 2 - lw, mid = x + d / 2 - lw / 2;
      const int tx[8] = {r, l, mid, mid, r, r, l, l};
      const int ty[8] = {y - 2, y - 2, y - 7, y + d + 1, y - 6, y + d, y - 6, y + d};
      for (int t = 0; t < 8 && free; t++) {
        const Box b{(int16_t) tx[t], (int16_t) ty[t], (int16_t) lw, 5};
        if (b.x < 0 || b.y < 0 || b.x + b.w > m.w || b.y + b.h > m.h || (pass == 0 && on_ring(b, pr.ox, pr.oy, m.ring)))
          continue;
        bool ok = true;
        for (int k = 0; k < nt && ok; k++)
          ok = !meet(b, taken[k], 1);
        if (!ok)
          continue;
        taken[nt++] = dot;
        taken[nt++] = b;
        out[n++] = MapLabel{(int16_t) x, (int16_t) y, b.x, b.y, cd[c].idx};
        cd[c].idx = 0xFF;
        break;
      }
    }
  return n;
}

void map_paint(Px565 &p, const AppMap &m, const LandSet *set) {
  p.fill(0, 0, p.w, p.h, 0);
  if (!map_has(m, set))
    return;
  const LandSet &s = *set;
  const MapProj pr = proj_of(m);
  // Roads, then borders, then the coast
  static const uint8_t kOrder[3] = {(uint8_t) LandKind::ROAD, (uint8_t) LandKind::BORDER, (uint8_t) LandKind::COAST};
  for (uint8_t kind : kOrder) {
    if (kind == (uint8_t) LandKind::ROAD && !m.roads)
      continue;
    const uint16_t c = rgb565(kind == (uint8_t) LandKind::ROAD ? kRoad : kind == (uint8_t) LandKind::BORDER ? kBorder : kCoast);
    size_t k = 0;
    for (size_t i = 0; i < s.nl; i++) {
      uint8_t lk, n;
      land_line(s, i, lk, n);
      if (lk == kind) {
        int step = 0, px = 0, py = 0;
        for (uint8_t j = 0; j < n; j++) {
          int16_t a, b;
          land_point(s, k + j, a, b);
          int x, y;
          set_xy(pr, s, a, b, x, y);
          if (j > 0)
            walk(p, px, py, x, y, c, kind == (uint8_t) LandKind::BORDER, step);
          px = x;
          py = y;
        }
      }
      k += n;
    }
  }
  // Lines stay clear of the kept labels
  for (const AppMapKeep &k : m.keep)
    p.fill(k.x, k.y, k.w, k.h, 0);
  MapLabel lab[kMapLabelMax];
  const int n = map_labels(m, s, lab, map_label_cap(m));
  LandPlace pl;
  for (int i = 0; i < n; i++) {
    land_place(s, lab[i].place, pl);
    const bool big = pl.cls >= 3;
    const int d = big ? 2 : 1;
    p.fill(lab[i].x, lab[i].y, d, d, rgb565(big ? kDotBig : kDotSmall));
    // A dark edge parts it from the lines
    p.fill(lab[i].lx - 1, lab[i].ly - 1, 4 * pl.len + 1, 7, 0);
    px_text(p, lab[i].lx, lab[i].ly, pl.name, rgb565(big ? kTextBig : kTextSmall), 1);
  }
}

}  // namespace espn
