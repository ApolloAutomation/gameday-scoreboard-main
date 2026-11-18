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

constexpr uint32_t kRoad = 0x4A3A24, kBorder = 0x505050, kCoast = 0x464646;
constexpr uint32_t kDotSmall = 0x808080, kDotBig = 0xA0A0A0, kTextSmall = 0x707070, kTextBig = 0x909090;
constexpr int kFar = 4096;

// What a taken box is, for the space kept from it
enum : uint8_t { kMark = 0, kWord, kDot, kHome };
struct Box {
  int16_t x, y, w, h;
  uint8_t k{kMark};
};
struct Cand {
  int16_t x, y;
  int32_t key;
  uint8_t idx;
};
constexpr int kReserved = 8 + 2 * kAppBlips;
// Label placement's scratch, in PSRAM once
struct Scratch {
  Cand c[kLandMaxPlaces];
  Box taken[kReserved + 2 * kMapLabelMax];
};

Scratch *scratch() {
  static Scratch *s = nullptr;
  if (s == nullptr)
    s = static_cast<Scratch *>(LAND_ALLOC(sizeof(Scratch)));
  return s;
}

// gap 1: a pixel apart counts as touching.
__attribute__((noinline)) bool meet(const Box &a, const Box &b, int gx, int gy) {
  return a.x - gx < b.x + b.w && b.x < a.x + a.w + gx && a.y - gy < b.y + b.h && b.y < a.y + a.h + gy;
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

__attribute__((noinline)) bool set_xyf(const MapProj &pr, const LandSet &s, int16_t dlat, int16_t dlon, float &x,
                                       float &y) {
  return map_xyf(pr, s.lat_e2 / 100.0f + dlat / 4000.0f, s.lon_e2 / 100.0f + dlon / 4000.0f, x, y);
}

bool set_xy(const MapProj &pr, const LandSet &s, int16_t dlat, int16_t dlon, int &x, int &y) {
  float fx, fy;
  if (!set_xyf(pr, s, dlat, dlon, fx, fy))
    return false;
  x = (int) floorf(fx);
  y = (int) floorf(fy);
  return true;
}

// Liang-Barsky: the part within a pixel of the canvas
__attribute__((noinline)) bool clip(float &x0, float &y0, float &x1, float &y1, int w, int h) {
  const float dx = x1 - x0, dy = y1 - y0;
  const float pp[4] = {-dx, dx, -dy, dy}, qq[4] = {x0 + 1, w - x0, y0 + 1, h - y0};
  float t0 = 0, t1 = 1;
  for (int k = 0; k < 4; k++) {
    if (pp[k] == 0) {
      if (qq[k] < 0)
        return false;
      continue;
    }
    const float r = qq[k] / pp[k];
    if (pp[k] < 0) {
      if (r > t1)
        return false;
      if (r > t0)
        t0 = r;
    } else {
      if (r < t0)
        return false;
      if (r < t1)
        t1 = r;
    }
  }
  x1 = x0 + t1 * dx;
  y1 = y0 + t1 * dy;
  x0 += t0 * dx;
  y0 += t0 * dy;
  return true;
}

MapProj proj_of(const AppMap &m) { return map_proj(m.lat_e4, m.lon_e4, m.scale_q16 / 65536.0f, m.w, m.h); }

constexpr uint32_t kRingInk = 0x484848, kInnerInk = 0x303030, kNorthInk = 0x808080, kRingText = 0x707070;
constexpr uint32_t kNumInk = 0x909090, kCardMark = 0x806800, kWideText = 0xFFD000;
constexpr uint32_t kBlipInk[4] = {0xE0E0E0, 0xFFD000, 0xFF4040, 0x808080};

Box box(int x, int y, int w, int h, uint8_t k = kMark) {
  return Box{(int16_t) x, (int16_t) y, (int16_t) w, (int16_t) h, k};
}

// The radar's N on the ring's top, and the ring label
void radar_boxes(const AppMap &m, Box &north, Box &label) {
  north = box(m.w / 2 - 1, 0, 3, 5, kWord);
  const int lw = px_text_w(m.ring_text, 1);
  label = box(m.w - 1 - lw, m.h - 6, lw, 5, kWord);
}

// The owner's distance, right of the inner ring; false none.
bool inner_box(const AppMap &m, Box &b) {
  if (m.inner_text[0] == '\0' || m.inner == 0)
    return false;
  b = box(m.w / 2 + m.inner + 2, m.h / 2 - 6, px_text_w(m.inner_text, 1), 5, kWord);
  return true;
}

int blip_half(const AppBlip &b) { return b.flags & kBlipIsCard ? 3 : 2; }
Box blip_box(const AppBlip &b) {
  const int d = blip_half(b);
  return box(b.x - d, b.y - d, 2 * d + 1, 2 * d + 1);
}
int blip_count(const AppMap &m) { return m.radar ? (m.n_blips < kAppBlips ? m.n_blips : kAppBlips) : 0; }

// What labels avoid: keeps, home, the radar's marks
int reserved(const AppMap &m, Box *out, int &home) {
  int n = 0;
  for (const AppMapKeep &k : m.keep)
    if (k.w > 0 && k.h > 0)
      out[n++] = box(k.x, k.y, k.w, k.h, kWord);
  home = n;
  out[n++] = box(m.w / 2 - 3, m.h / 2 - 3, 6, 6, kHome);
  if (!m.radar)
    return n;
  radar_boxes(m, out[n], out[n + 1]);
  n += 2;
  n += inner_box(m, out[n]);
  MapMark mk[kAppBlips];
  map_marks(m, mk);
  for (int i = 0; i < blip_count(m); i++) {
    out[n++] = blip_box(m.blips[i]);
    if (mk[i].w > 0)
      out[n++] = box(mk[i].x, mk[i].y, mk[i].w, 5, kWord);
  }
  return n;
}

void circle(Px565 &p, int cx, int cy, int r, uint16_t c, bool dotted) {
  int x = r, y = 0, err = 1 - r, step = 0;
  while (x >= y) {
    if (!dotted || (step++ & 1) == 0)
      for (int k = 0; k < 8; k++) {
        const int a = k & 4 ? y : x, b = k & 4 ? x : y;
        p.put(cx + (k & 1 ? -a : a), cy + (k & 2 ? -b : b), c);
      }
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void blip(Px565 &p, const AppBlip &b) {
  const uint16_t c = rgb565(kBlipInk[b.ink & 3]);
  if (b.flags & kBlipIsCard) {
    const uint16_t k = rgb565(kCardMark);
    for (int sx = -1; sx <= 1; sx += 2)
      for (int sy = -1; sy <= 1; sy += 2) {
        const int x = b.x + 3 * sx, y = b.y + 3 * sy;
        p.put(x, y, k);
        p.put(x - sx, y, k);
        p.put(x, y - sy, k);
      }
  }
  if (b.flags & kBlipOnGround) {
    p.fill(b.x - 1, b.y, 2, 1, c);
    return;
  }
  if (!(b.flags & kBlipTrack)) {
    p.fill(b.x - 1, b.y - 1, 2, 2, c);
    return;
  }
  // 5x5 arrows up and up-right, turned in quarters
  static const uint32_t kArrow[2] = {0x4255C4u, 0x11531Cu};
  const int dir = ((b.track % 360 + 360) % 360 + 22) / 45 % 8;
  const uint32_t bits = kArrow[dir & 1];
  for (int i = 0; i < 25; i++) {
    if (!(bits >> i & 1))
      continue;
    int x = i % 5, y = i / 5;
    for (int r = 0; r < dir / 2; r++) {
      const int t = x;
      x = 4 - y;
      y = t;
    }
    p.put(b.x - 2 + x, b.y - 2 + y, c);
  }
}

// Rings, then the N and ring label on black
void radar_under(Px565 &p, const AppMap &m) {
  const int cx = m.w / 2, cy = m.h / 2;
  if (m.inner > 0)
    circle(p, cx, cy, m.inner, rgb565(m.wide ? kRingInk : kInnerInk), true);
  circle(p, cx, cy, m.ring, rgb565(kRingInk), false);
  Box north, label;
  radar_boxes(m, north, label);
  const uint16_t n = rgb565(kNorthInk);
  p.fill(north.x - 1, north.y, north.w + 2, north.h + 1, 0);
  p.fill(label.x - 1, label.y - 1, label.w + 2, label.h + 2, 0);
  px_text(p, north.x, north.y, "N", n, 1);
  px_text(p, label.x, label.y, m.ring_text, rgb565(m.wide ? kWideText : kRingText), 1);
  Box in;
  if (inner_box(m, in)) {
    p.fill(in.x - 1, in.y - 1, in.w + 2, in.h + 2, 0);
    px_text(p, in.x, in.y, m.inner_text, rgb565(kRingText), 1);
  }
}

// Planes, home, then their numbers and the tag
void radar_over(Px565 &p, const AppMap &m) {
  const int n = blip_count(m);
  // Black under each plane, clear of the lines
  for (int i = 0; i < n; i++) {
    const Box b = blip_box(m.blips[i]);
    p.fill(b.x, b.y, b.w, b.h, 0);
  }
  // Others, then the card's, then emergencies on top
  for (uint8_t ink : {kBlipGround, kBlipPlane, kBlipCard, kBlipAlert})
    for (int i = 0; i < n; i++)
      if (m.blips[i].ink == ink)
        blip(p, m.blips[i]);
  // Home stays in sight under a plane
  p.fill(m.w / 2 - 1, m.h / 2 - 1, 2, 2, 0xFFFF);
  MapMark mk[kAppBlips];
  map_marks(m, mk);
  for (int i = 0; i < n; i++) {
    if (mk[i].w == 0)
      continue;
    const AppBlip &b = m.blips[i];
    char num[2] = {(char) ('0' + b.number), 0};
    const bool tag = i == m.tag_of;
    p.fill(mk[i].x - 1, mk[i].y - 1, mk[i].w + 2, 7, 0);
    const uint32_t ink = tag || b.ink == kBlipAlert ? kBlipInk[b.ink & 3] : kNumInk;
    px_text(p, mk[i].x, mk[i].y, tag ? m.tag : num, rgb565(ink), 1);
  }
}

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
  if (p == nullptr || n < kLandHead || n > kLandMaxBytes || p[0] != 'G' || p[1] != 'L' || p[2] != kLandVersion || p[3] != 0)
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

bool map_xyf(const MapProj &p, float lat, float lon, float &x, float &y) {
  world(lat, lon, x, y);
  // The shorter way round, across the 180th meridian
  constexpr float kWorld = (float) (wx::kTilePx << wx::kRadarZoom);
  float dx = x - p.wx;
  if (dx > kWorld / 2)
    dx -= kWorld;
  else if (dx < -kWorld / 2)
    dx += kWorld;
  x = p.ox + dx * p.s;
  y = p.oy + (y - p.wy) * p.s;
  return fabsf(x) < kFar && fabsf(y) < kFar;
}

bool map_xy(const MapProj &p, float lat, float lon, int &x, int &y) {
  float fx, fy;
  x = y = 0;
  if (!map_xyf(p, lat, lon, fx, fy))
    return false;
  x = (int) floorf(fx);
  y = (int) floorf(fy);
  return true;
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
  int nc = 0;
  LandPlace pl;
  for (size_t i = 0; i < s.np; i++) {
    land_place(s, i, pl);
    int x, y;
    const bool on = set_xy(pr, s, pl.dlat, pl.dlon, x, y);
    const int d = pl.cls >= 3 ? 2 : 1;
    if (!on || pl.cls < min_cls || x < 1 || y < 1 || x + d > m.w - 1 || y + d > m.h - 1)
      continue;
    const int32_t k = (int32_t) (4 - pl.cls) << 24 | ((x - pr.ox) * (x - pr.ox) + (y - pr.oy) * (y - pr.oy));
    int at = nc++;
    for (; at > 0 && cd[at - 1].key > k; at--)
      cd[at] = cd[at - 1];
    cd[at] = Cand{(int16_t) x, (int16_t) y, k, (uint8_t) i};
  }
  // Taken: the keeps, the home, then dots and labels
  int home_at = 0;
  int nt = reserved(m, taken, home_at);
  const Box home = taken[home_at];
  bool home_named = false;
  // Home's town: the nearest place close enough on the ground
  const float kpp = wx::ground_m_per_px(m.lat_e4 / 10000.0f, wx::kRadarZoom) / (m.scale_q16 / 65536.0f) / 1000.0f;
  int home_c = -1;
  float best = 1e9f;
  for (int c = 0; c < nc; c++) {
    land_place(s, cd[c].idx, pl);
    static const float kNearKm[5] = {0, 3, 4, 8, 15};
    const float dx = cd[c].x - pr.ox, dy = cd[c].y - pr.oy, km = sqrtf(dx * dx + dy * dy) * kpp;
    if (km <= kNearKm[pl.cls] && km < best) {
      best = km;
      home_c = c;
    }
  }
  // The biggest cities and the nearest city lead
  int lead_c3 = -1;
  for (int c = 0; c < nc && lead_c3 < 0; c++)
    if (cd[c].key >> 24 == 1)
      lead_c3 = c;
  int n = 0;
  // Leads off the ring, leads on it, then the rest
  for (int pass = 0; pass < 3; pass++)
    for (int c = 0; c < nc && n < cap; c++) {
      if (cd[c].idx == 0xFF)
        continue;
      const bool lead = cd[c].key >> 24 == 0 || c == lead_c3;
      if (lead != (pass < 2))
        continue;
      land_place(s, cd[c].idx, pl);
      int d = pl.cls >= 3 ? 2 : 1, x = cd[c].x, y = cd[c].y;
      const int lw = 4 * pl.len - 1;
      Box dot = box(x, y, d, d, kDot);
      // The place home sits in names the white dot
      const bool at_home = !home_named && c == home_c;
      if (at_home) {
        dot = home;
        x = home.x;
        y = home.y + 2;
        d = home.w;
      }
      bool free = true;
      // A dot clearly nearer its own name than any other
      for (int t = 0; t < nt && free && !at_home; t++) {
        const uint8_t k = taken[t].k;
        const int g = k == kWord || k == kHome ? 4 : k == kDot ? 3 : 1;
        // Wider along a name's own rows
        free = !meet(dot, taken[t], g, g) && (k != kWord || !meet(dot, taken[t], 6, 0));
      }
      // Beside it, the corners; home's over, under, past a small ring
      const int r = x + d + 1, l = x - 2 - lw, mid = x + d / 2 - lw / 2;
      const int tx[10] = {r, l, r, r, l, l, mid, mid, mid, mid};
      const int ty[10] = {y - 2, y - 2, y - 6, y + d + 1, y - 6, y + d + 1, y - 8, y + 5,
                          pr.oy - m.ring - 8, pr.oy + m.ring + 3};
      for (int t = 0; t < (at_home ? (m.ring > 0 && m.ring <= 16 ? 10 : 8) : 6) && free; t++) {
        const Box b = box(tx[t], ty[t], lw, 5, kWord);
        // A pixel in from the edges, for the dark edge
        if (b.x < 1 || b.y < 1 || b.x + b.w > m.w - 1 || b.y + b.h > m.h - 1 ||
            ((pass != 1 || !m.radar) && on_ring(b, pr.ox, pr.oy, m.ring)))
          continue;
        bool ok = true;
        // Two names on a row read as two places
        for (int k = 0; k < nt && ok; k++) {
          const uint8_t kk = taken[k].k;
          const int gx = kk == kWord ? 8 : kk == kDot ? 4 : kk == kHome && !at_home ? 3 : 1;
          const int gy = kk == kWord ? 2 : gx;
          ok = (at_home && k == home_at) || (!meet(b, taken[k], gx, gy) && (kk != kDot || !meet(b, taken[k], 6, 0)));
        }
        if (!ok)
          continue;
        if (!at_home)
          taken[nt++] = dot;
        home_named = home_named || at_home;
        taken[nt++] = b;
        out[n++] = MapLabel{(int16_t) (at_home ? -1 : x), (int16_t) (at_home ? -1 : y), b.x, b.y, cd[c].idx};
        cd[c].idx = 0xFF;
        break;
      }
    }
  return n;
}

void map_marks(const AppMap &m, MapMark out[kAppBlips]) {
  for (int i = 0; i < kAppBlips; i++)
    out[i] = MapMark();
  const int n = blip_count(m);
  if (n == 0)
    return;
  Box taken[4 + 2 * kAppBlips];
  int nt = 0;
  taken[nt++] = box(m.w / 2 - 3, m.h / 2 - 3, 6, 6);
  radar_boxes(m, taken[nt], taken[nt + 1]);
  nt += 2;
  nt += inner_box(m, taken[nt]);
  for (int i = 0; i < n; i++)
    taken[nt++] = blip_box(m.blips[i]);
  // The tag first, then numbers in list order
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < n; i++) {
      const AppBlip &b = m.blips[i];
      const bool tag = i == m.tag_of && m.tag[0] != '\0';
      if (tag != (pass == 0) || (!tag && (b.number < 1 || b.number > 9)))
        continue;
      const int lw = tag ? px_text_w(m.tag, 1) : 3, d = blip_half(b);
      // Beside, above, below, the corners; a tag farther out too
      const int tx[8] = {b.x + d + 2, b.x - d - 2 - lw, b.x - lw / 2, b.x - lw / 2,
                         b.x + d + 1, b.x + d + 1, b.x - d - 1 - lw, b.x - d - 1 - lw};
      const int ty[8] = {b.y - 2, b.y - 2, b.y - d - 6, b.y + d + 2, b.y - d - 5, b.y + d + 1, b.y - d - 5, b.y + d + 1};
      const int far = tag ? 3 : 1;
      for (int t = 0; t < 8 * far; t++) {
        const int k = t % 8, s = t / 8 * 4, sx = k == 1 || k > 5 ? -s : k < 2 || k > 3 ? s : 0;
        const int sy = k == 2 || k == 4 || k == 6 ? -s : k == 3 || k == 5 || k == 7 ? s : 0;
        const Box l = box(tx[k] + sx, ty[k] + sy, lw, 5);
        if (l.x < 1 || l.y < 1 || l.x + l.w > m.w - 1 || l.y + l.h > m.h - 1)
          continue;
        bool ok = true;
        for (int k = 0; k < nt && ok; k++)
          ok = !meet(l, taken[k], 1, 1);
        if (!ok)
          continue;
        out[i] = MapMark{l.x, l.y, l.w};
        taken[nt++] = l;
        break;
      }
    }
}

void map_paint(Px565 &p, const AppMap &m, const LandSet *set) {
  p.fill(0, 0, p.w, p.h, 0);
  if (!map_has(m, set)) {
    if (m.radar) {
      radar_under(p, m);
      radar_over(p, m);
    }
    return;
  }
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
        int step = 0;
        float px = 0, py = 0;
        bool had = false;
        for (uint8_t j = 0; j < n; j++) {
          int16_t a, b;
          land_point(s, k + j, a, b);
          float x, y;
          // A point far off breaks the line
          const bool ok = set_xyf(pr, s, a, b, x, y);
          float x0 = px, y0 = py, x1 = x, y1 = y;
          if (ok && had && clip(x0, y0, x1, y1, p.w, p.h))
            walk(p, (int) floorf(x0), (int) floorf(y0), (int) floorf(x1), (int) floorf(y1), c,
                 kind == (uint8_t) LandKind::BORDER, step);
          px = x;
          py = y;
          had = ok;
        }
      }
      k += n;
    }
  }
  // Lines stay clear of the kept labels
  for (const AppMapKeep &k : m.keep)
    p.fill(k.x, k.y, k.w, k.h, 0);
  if (m.radar)
    radar_under(p, m);
  MapLabel lab[kMapLabelMax];
  const int n = map_labels(m, s, lab, map_label_cap(m));
  LandPlace pl;
  for (int i = 0; i < n; i++) {
    land_place(s, lab[i].place, pl);
    const bool big = pl.cls >= 3;
    const int d = big ? 2 : 1;
    if (lab[i].x >= 0)
      p.fill(lab[i].x, lab[i].y, d, d, rgb565(big ? kDotBig : kDotSmall));
    // A dark edge parts it from the lines
    p.fill(lab[i].lx - 1, lab[i].ly - 1, 4 * pl.len + 1, 7, 0);
    px_text(p, lab[i].lx, lab[i].ly, pl.name, rgb565(big ? kTextBig : kTextSmall), 1);
  }
  if (m.radar)
    radar_over(p, m);
}

}  // namespace espn
