// Map landmarks: the set's check, the projection, the base layer.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "land_set.h"
#include "landmarks.h"
#include "map_canvas.h"
#include "scene_render.h"
#include "weather.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define LCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

LandError check(const std::vector<uint8_t> &b) {
  // Exactly its size on the heap, for the sanitizers
  std::vector<uint8_t> copy(b);
  LandSet s;
  return land_check(copy.empty() ? nullptr : copy.data(), copy.size(), s);
}

void put16(std::vector<uint8_t> &b, size_t at, int v) {
  b[at] = (uint8_t) (v & 255);
  b[at + 1] = (uint8_t) ((v >> 8) & 255);
}

AppMap weather_map(int w, int h, int32_t lat_e4, int32_t lon_e4) {
  AppMap m;
  m.w = (int16_t) w;
  m.h = (int16_t) h;
  m.lat_e4 = wx::round_e4(lat_e4);
  m.lon_e4 = wx::round_e4(lon_e4);
  m.scale_q16 = (int32_t) lroundf(map_scale_weather(w, h) * 65536.0f);
  m.keep[0] = AppMapKeep{1, 0, 30, 8};
  m.keep[1] = AppMapKeep{(int16_t) (w - 26), (int16_t) (h - 8), 25, 8};
  m.roads = w >= 128;
  m.ring = (uint8_t) (h * 2 / 5);
  return m;
}

struct Canvas {
  std::vector<uint16_t> px;
  Px565 p;
  Canvas(int w, int h) : px((size_t) w * h, 0xFFFF) {
    p.px = px.data();
    p.w = p.stride = (int16_t) w;
    p.h = (int16_t) h;
    p.clip = PxBox{0, 0, (int16_t) w, (int16_t) h};
  }
  int count(uint32_t rgb) const { return (int) std::count(px.begin(), px.end(), rgb565(rgb)); }
};

int top_channel(uint16_t c) { return std::max({(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3}); }

void test_check() {
  const std::vector<uint8_t> good = land_sample();
  LandSet s;
  std::vector<uint8_t> g(good);
  LCHECK(land_check(g.data(), g.size(), s) == LandError::OK, "the sample passes: %s",
         land_error_word(land_check(g.data(), g.size(), s)));
  const auto places = land_sample_places();
  const auto lines = land_sample_lines();
  size_t npt = 0;
  for (const auto &l : lines)
    npt += l.pts.size();
  LCHECK(s.p == g.data() && s.n == g.size() && s.lat_e2 == 4000 && s.lon_e2 == -9000 && s.np == places.size() &&
             s.nl == lines.size() && s.npt == npt && s.reach_km == 250,
         "the header reads back");
  bool same = true;
  for (size_t i = 0; i < places.size(); i++) {
    LandPlace p;
    land_place(s, i, p);
    same = same && p.dlat == places[i].dlat && p.dlon == places[i].dlon && p.cls == places[i].cls &&
           strcmp(p.name, places[i].name) == 0;
  }
  size_t k = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    uint8_t kind, n;
    land_line(s, i, kind, n);
    same = same && kind == lines[i].kind && n == lines[i].pts.size();
    for (size_t j = 0; j < n; j++, k++) {
      int16_t a, b;
      land_point(s, k, a, b);
      same = same && a == lines[i].pts[j].first && b == lines[i].pts[j].second;
    }
  }
  LCHECK(same, "places, lines and points read back");
  LCHECK(check(land_encode(0, 0, 1, {}, {})) == LandError::OK, "an empty set passes");

  // One field wrong at a time
  const size_t place0 = kLandHead, line0 = kLandHead + places.size() * kLandPlaceBytes;
  const size_t pt0 = line0 + lines.size() * 2;
  struct Bad {
    const char *what;
    LandError want;
    void (*make)(std::vector<uint8_t> &, size_t, size_t, size_t);
  };
  const Bad bad[] = {
      {"magic", LandError::FORMAT, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { b[0] = 'X'; }},
      {"version", LandError::FORMAT, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { b[2] = 2; }},
      {"flags", LandError::FORMAT, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { b[3] = 1; }},
      {"lat", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 4, 9001); }},
      {"lon", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 6, -18001); }},
      {"places", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 8, 65); }},
      {"lines", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 10, 201); }},
      {"points", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 12, 1601); }},
      {"reach 0", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 14, 0); }},
      {"reach 301", LandError::COUNTS, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { put16(b, 14, 301); }},
      {"a byte more", LandError::LENGTH, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { b.push_back(0); }},
      {"a byte less", LandError::LENGTH, [](std::vector<uint8_t> &b, size_t, size_t, size_t) { b.pop_back(); }},
      {"class 5", LandError::PLACE, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 4] = 5; }},
      {"class 0", LandError::PLACE, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 4] = 0; }},
      {"place far", LandError::PLACE, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { put16(b, p + 2, 32001); }},
      {"name 0", LandError::NAME, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 5] = 0; }},
      {"name 11", LandError::NAME, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 5] = 11; }},
      {"lowercase", LandError::NAME, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 7] = 'r'; }},
      {"a #", LandError::NAME, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 7] = '#'; }},
      {"trailing space", LandError::NAME,
       [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 6 + b[p + 5] - 1] = ' '; }},
      {"leading space", LandError::NAME, [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 6] = ' '; }},
      {"byte after the name", LandError::NAME,
       [](std::vector<uint8_t> &b, size_t p, size_t, size_t) { b[p + 6 + 9] = 'A'; }},
      {"kind 3", LandError::LINE, [](std::vector<uint8_t> &b, size_t, size_t l, size_t) { b[l] = 3; }},
      {"one point", LandError::LINE,
       [](std::vector<uint8_t> &b, size_t, size_t l, size_t) { b[l + 1] = 1; }},
      {"sum", LandError::LINE, [](std::vector<uint8_t> &b, size_t, size_t l, size_t) { b[l + 1]++; }},
      {"point far", LandError::POINT, [](std::vector<uint8_t> &b, size_t, size_t, size_t t) { put16(b, t, 9400); }},
      {"point far west", LandError::POINT,
       [](std::vector<uint8_t> &b, size_t, size_t, size_t t) { put16(b, t + 6, -12300); }},
  };
  for (const Bad &x : bad) {
    std::vector<uint8_t> b(good);
    x.make(b, place0, line0, pt0);
    const LandError e = check(b);
    LCHECK(e == x.want, "%s: %s, not %s", x.what, land_error_word(e), land_error_word(x.want));
  }
  // Every cut short, and nothing at all
  int cut_ok = 0;
  for (size_t n = 0; n < good.size(); n++)
    cut_ok += check(std::vector<uint8_t>(good.begin(), good.begin() + n)) != LandError::OK;
  LCHECK(cut_ok == (int) good.size(), "every truncation refused: %d of %d", cut_ok, (int) good.size());
  LCHECK(land_check(nullptr, 0, s) == LandError::FORMAT && s.p == nullptr, "no bytes");
  // The largest set is exactly the cap's worth
  std::vector<LandPlaceIn> many(kLandMaxPlaces, LandPlaceIn{0, 0, 1, "ABCDEFGHIJ"});
  std::vector<LandLineIn> ls(kLandMaxLines - 1, LandLineIn{2, std::vector<std::pair<int, int>>(8, {0, 0})});
  ls.push_back(LandLineIn{0, std::vector<std::pair<int, int>>(kLandMaxPoints - 8 * (kLandMaxLines - 1), {0, 0})});
  std::vector<uint8_t> big = land_encode(-9000, 18000, kLandMaxReach, many, ls);
  LCHECK(big.size() == 7840 && check(big) == LandError::OK, "the largest set: %d bytes", (int) big.size());
  big.push_back(0);
  LCHECK(check(big) == LandError::LENGTH, "a byte past the largest: length");
  // The reach box's edge, and a unit past it
  for (int lat_e2 : {0, 3805, -6000}) {
    int la = 0, lo = 0;
    land_reach_box(lat_e2, 100, la, lo);
    auto at = [lat_e2](int a, int b) {
      return check(land_encode(lat_e2, 0, 100, {{a, b, 2, "EDGE"}}, {{0, {{a, b}, {-a, -b}}}}));
    };
    LCHECK(la == (int) ceil(110 / 111.32 * 4000) && lo >= la && at(la, lo) == LandError::OK && at(-la, -lo) == LandError::OK,
           "lat %d: the edge %d,%d passes", lat_e2, la, lo);
    LCHECK(at(la + 1, 0) == LandError::PLACE && at(0, -lo - 1) == LandError::PLACE, "lat %d: a unit past fails", lat_e2);
    LCHECK(check(land_encode(lat_e2, 0, 100, {}, {{0, {{0, 0}, {0, lo + 1}}}})) == LandError::POINT,
           "lat %d: a point past fails", lat_e2);
  }
  int la = 0, lo = 0;
  land_reach_box(8900, 300, la, lo);
  LCHECK(la < kLandMaxOff && lo == kLandMaxOff, "near the pole the cap holds: %d", lo);
  LCHECK(land_e2(380499) == 3805 && land_e2(-847349) == -8473 && land_e2(-50) == -1 && land_e2(-49) == 0 &&
             land_e2(50) == 1,
         "rounding to hundredths");
  LCHECK(land_pack(-1, -1) != kLandNoHome && land_pack(3805, -8473) == land_pack(3805, -8473), "packed homes");
}

void test_projection() {
  LCHECK(map_scale_weather(64, 56) == 0.25f && map_scale_weather(128, 56) == 0.5f &&
             map_scale_weather(128, 120) == 0.5f,
         "the tile's scale: %g %g %g", (double) map_scale_weather(64, 56), (double) map_scale_weather(128, 56),
         (double) map_scale_weather(128, 120));
  // As MapCanvas cuts the tile for each box
  for (const auto &sz : {std::make_pair(64, 56), std::make_pair(128, 56), std::make_pair(128, 120)}) {
    MapCanvas c;
    c.setup(256, 256, sz.first, sz.second);
    LCHECK(fabsf(map_scale_weather(sz.first, sz.second) - (float) c.bw / c.cw) < 1e-6f &&
               fabsf(map_scale_weather(sz.first, sz.second) - (float) c.bh / c.ch) < 0.01f,
           "%dx%d agrees with the image", sz.first, sz.second);
  }
  const AppMap m = weather_map(128, 56, 380512, -847349);
  const MapProj p = map_proj(m.lat_e4, m.lon_e4, 0.5f, 128, 56);
  int x = 0, y = 0;
  LCHECK(map_xy(p, 38.05f, -84.73f, x, y) && x == 64 && y == 28, "the rounded home at the middle: %d,%d", x, y);
  const float lon_px = 360.0f / 32768.0f;
  LCHECK(map_xy(p, 38.05f, -84.73f + 2 * lon_px, x, y) && x == 65 && y == 28, "two tile pixels east, one here: %d", x);
  LCHECK(map_xy(p, 38.05f, -84.73f - 20 * lon_px, x, y) && x == 54, "twenty west, ten here: %d", x);
  // Nine tile pixels north, by the tile's math
  const float wy = wx::merc_px_y(38.05f, 7);
  float lat = 38.05f;
  while (wx::merc_px_y(lat, 7) > wy - 9.0f)
    lat += 0.0001f;
  LCHECK(map_xy(p, lat, -84.73f, x, y) && y == 23, "nine tile pixels north, four and a half here: %d", y);
  LCHECK(!map_xy(p, 38.05f, 60.0f, x, y) && x == 4096, "far away is clamped: %d", x);
  const float rs = map_scale_ring(380500, 23, 16093.44f);
  LCHECK(fabsf(rs * 16093.44f / wx::ground_m_per_px(38.05f, 7) - 23.0f) < 0.01f, "ten miles is 23 px: %g", (double) rs);
  AppMap q = m;
  LCHECK(map_min_class(q) == 2, "128 wide at 38 N: class 2 up");
  q = weather_map(64, 56, 380512, -847349);
  LCHECK(map_min_class(q) == 3, "64 wide: class 3 up");
  q.scale_q16 = 65536 * 3;
  LCHECK(map_min_class(q) == 1, "close in: every place");
  LCHECK(map_label_cap(weather_map(64, 56, 0, 0)) == 4 && map_label_cap(weather_map(128, 56, 0, 0)) == 8 &&
             map_label_cap(weather_map(128, 120, 0, 0)) == 12,
         "label caps");
  // R meters north lands on a 26 px ring
  for (int32_t lat_e4 : {0, 380000, 600000})
    for (float r_m : {16093.44f, 160934.4f}) {
      const float s = map_scale_ring(lat_e4, 26, r_m);
      const MapProj rp = map_proj(lat_e4, 0, s, 128, 56);
      const float north = lat_e4 / 10000.0f + r_m / 6378137.0f * 57.29578f;
      int rx = 0, ry = 0;
      map_xy(rp, north, 0.0f, rx, ry);
      LCHECK(rx == 64 && std::abs((28 - ry) - 26) <= 1, "lat %d, %g m north: %d px up", (int) lat_e4, (double) r_m,
             28 - ry);
    }
}

void labels_sound(const AppMap &m, const LandSet &s, const char *what) {
  MapLabel lab[kMapLabelMax];
  const int n = map_labels(m, s, lab, map_label_cap(m));
  LCHECK((n > 0 || m.w < 128) && n <= map_label_cap(m), "%s: %d labels", what, n);
  const MapProj p = map_proj(m.lat_e4, m.lon_e4, m.scale_q16 / 65536.0f, m.w, m.h);
  const PxBox home{(int16_t) (p.ox - 3), (int16_t) (p.oy - 3), 6, 6};
  const int min_cls = map_min_class(m);
  std::vector<PxBox> boxes;
  for (int i = 0; i < n; i++) {
    LandPlace pl;
    land_place(s, lab[i].place, pl);
    LCHECK(pl.cls >= min_cls, "%s: %s's class %d under %d", what, pl.name, pl.cls, min_cls);
    const PxBox b{lab[i].lx, lab[i].ly, (int16_t) (4 * pl.len - 1), 5};
    LCHECK(b.x >= 0 && b.y >= 0 && b.x + b.w <= m.w && b.y + b.h <= m.h, "%s: %s inside", what, pl.name);
    LCHECK(!box_overlap(b, home), "%s: %s off the home", what, pl.name);
    for (const AppMapKeep &k : m.keep)
      LCHECK(k.w == 0 || !box_overlap(b, PxBox{k.x, k.y, k.w, k.h}), "%s: %s off a kept box", what, pl.name);
    for (const PxBox &o : boxes)
      LCHECK(!box_overlap(PxBox{(int16_t) (b.x - 1), (int16_t) (b.y - 1), (int16_t) (b.w + 2), (int16_t) (b.h + 2)}, o),
             "%s: %s touches another", what, pl.name);
    // A town's label stays on one side of the ring
    if (m.ring > 0 && pl.cls < 3) {
      int in = 0, out = 0, on = 0;
      for (int y = b.y; y < b.y + b.h; y++)
        for (int x = b.x; x < b.x + b.w; x++) {
          const float d = hypotf((float) (x - p.ox), (float) (y - p.oy));
          in += d < m.ring;
          out += d > m.ring;
          on += fabsf(d - m.ring) < 1.0f;
        }
      LCHECK(on == 0 && (in == 0 || out == 0), "%s: %s off the ring", what, pl.name);
    }
    boxes.push_back(b);
  }
  MapLabel again[kMapLabelMax];
  bool same = map_labels(m, s, again, map_label_cap(m)) == n;
  for (int i = 0; i < n && same; i++)
    same = again[i].x == lab[i].x && again[i].y == lab[i].y && again[i].lx == lab[i].lx && again[i].ly == lab[i].ly &&
           again[i].place == lab[i].place;
  LCHECK(same, "%s: the same labels again", what);
}

void test_paint() {
  std::vector<uint8_t> bytes = land_sample();
  LandSet s;
  land_check(bytes.data(), bytes.size(), s);
  const int32_t lat = 400012, lon = -899988;
  for (const auto &sz : {std::make_pair(64, 56), std::make_pair(128, 56), std::make_pair(128, 120)}) {
    const AppMap m = weather_map(sz.first, sz.second, lat, lon);
    Canvas c(sz.first, sz.second);
    map_paint(c.p, m, &s);
    int top = 0, lit = 0;
    for (uint16_t v : c.px) {
      top = std::max(top, top_channel(v));
      lit += v != 0;
    }
    char what[32];
    snprintf(what, sizeof what, "%dx%d", sz.first, sz.second);
    LCHECK(top <= 0xA0 && lit > 20, "%s: dim, and drawn: top %02x, %d lit", what, top, lit);
    LCHECK((c.count(0x4A3A24) > 0) == (sz.first >= 128), "%s: roads only 128 wide", what);
    LCHECK(c.count(0x505050) > 0, "%s: borders", what);
    for (uint32_t line : {0x4A3A24u, 0x505050u, 0x3A4048u})
      LCHECK(top_channel(rgb565(line)) <= 0x60, "lines stay dim");
    labels_sound(m, s, what);
    // Another home's set draws nothing
    AppMap o = weather_map(sz.first, sz.second, lat + 100, lon);
    Canvas d(sz.first, sz.second);
    map_paint(d.p, o, &s);
    LCHECK(std::all_of(d.px.begin(), d.px.end(), [](uint16_t v) { return v == 0; }), "%s: another home: black", what);
    map_paint(d.p, m, nullptr);
    LCHECK(std::all_of(d.px.begin(), d.px.end(), [](uint16_t v) { return v == 0; }), "%s: none: black", what);
  }
  // Two places on one spot: the bigger one
  std::vector<uint8_t> two = land_encode(4000, -9000, 100, {{400, 800, 2, "SMALL"}, {400, 800, 3, "BIG"}}, {});
  LandSet t;
  land_check(two.data(), two.size(), t);
  const AppMap m = weather_map(128, 56, 400000, -900000);
  MapLabel lab[kMapLabelMax];
  const int n = map_labels(m, t, lab, 8);
  LCHECK(n == 1 && lab[0].place == 1, "the bigger class wins the spot: %d", n);
  // Just inside the ring: the label goes left
  AppMap rm = weather_map(128, 56, 400000, -900000);
  rm.ring = 30;
  int ex = 0, ey = 0;
  const MapProj rp = map_proj(rm.lat_e4, rm.lon_e4, 0.5f, 128, 56);
  int16_t dlon = 0;
  for (; dlon < 6000; dlon += 20) {
    map_xy(rp, 40.0f, -90.0f + dlon / 4000.0f, ex, ey);
    if (ex >= rp.ox + 27)
      break;
  }
  std::vector<uint8_t> one = land_encode(4000, -9000, 100, {{0, dlon, 3, "EDGE"}}, {});
  LandSet os;
  land_check(one.data(), one.size(), os);
  LCHECK(map_labels(rm, os, lab, 8) == 1 && lab[0].lx + 15 < lab[0].x, "left of its dot, off the ring: %d", lab[0].lx);
  rm.ring = 0;
  LCHECK(map_labels(rm, os, lab, 8) == 1 && lab[0].lx > lab[0].x, "no ring: right of it");
  // Crowded: never past the cap
  std::vector<LandPlaceIn> crowd;
  static const char *const kNames[] = {"AA", "BB", "CC", "DD", "EE", "FF", "GG", "HH", "II", "JJ", "KK", "LL",
                                       "MM", "NN", "OO", "PP", "QQ", "RR", "SS", "TT", "UU", "VV", "WW", "XX"};
  for (int i = 0; i < 24; i++)
    crowd.push_back(LandPlaceIn{-1000 + (i / 6) * 600, -4400 + (i % 6) * 1800, 4, kNames[i]});
  std::vector<uint8_t> cb = land_encode(4000, -9000, 100, crowd, {});
  LandSet cs;
  land_check(cb.data(), cb.size(), cs);
  for (const auto &sz : {std::make_pair(64, 56), std::make_pair(128, 56), std::make_pair(128, 120)}) {
    const AppMap cm = weather_map(sz.first, sz.second, 400000, -900000);
    const int k = map_labels(cm, cs, lab, map_label_cap(cm));
    LCHECK(k == map_label_cap(cm), "%dx%d crowded: the cap, %d", sz.first, sz.second, k);
    labels_sound(cm, cs, "crowded");
  }
  // A line far outside costs a clamped walk
  std::vector<uint8_t> far = land_encode(4000, -9000, 250, {}, {{2, {{-9000, -12000}, {9000, 12000}}}});
  LandSet fs;
  LCHECK(land_check(far.data(), far.size(), fs) == LandError::OK, "a long road");
  Canvas c(128, 56);
  map_paint(c.p, weather_map(128, 56, 400000, -900000), &fs);
  LCHECK(c.count(0x4A3A24) > 20, "it crosses the canvas");
  // Nothing written past the canvas
  for (const auto &sz : {std::make_pair(64, 56), std::make_pair(128, 56), std::make_pair(128, 120)}) {
    const int w = sz.first, h = sz.second, guard = 64;
    std::vector<uint16_t> buf((size_t) w * h + 2 * guard, 0xA5A5);
    Px565 g;
    g.px = buf.data() + guard;
    g.w = g.stride = (int16_t) w;
    g.h = (int16_t) h;
    g.clip = PxBox{0, 0, (int16_t) w, (int16_t) h};
    for (const LandSet *set : {&s, &fs, &cs})
      for (int32_t mlat : {400000, 400012, 401500}) {
        AppMap gm = weather_map(w, h, mlat, -900000);
        gm.scale_q16 = 65536 * 4;
        map_paint(g, gm, set);
        map_paint(g, weather_map(w, h, mlat, -900000), set);
      }
    bool intact = true;
    for (int i = 0; i < guard; i++)
      intact = intact && buf[i] == 0xA5A5 && buf[buf.size() - 1 - i] == 0xA5A5;
    LCHECK(intact, "%dx%d: the guard bytes stay", w, h);
  }
  // The sample paints the same picture every time
  static const uint32_t kHash[3] = {0x79115bf7u, 0x9e792a4au, 0xe450fa0bu};
  int k = 0;
  for (const auto &sz : {std::make_pair(64, 56), std::make_pair(128, 56), std::make_pair(128, 120)}) {
    Canvas hc(sz.first, sz.second);
    map_paint(hc.p, weather_map(sz.first, sz.second, lat, lon), &s);
    const uint32_t got = fnv1a(2166136261u, hc.px);
    LCHECK(got == kHash[k], "%dx%d paints 0x%08x", sz.first, sz.second, (unsigned) got);
    k++;
  }
}

AppMap radar_of(int32_t lat_e4, int32_t lon_e4) {
  AppMap m;
  m.w = 128;
  m.h = 56;
  m.lat_e4 = lat_e4;
  m.lon_e4 = lon_e4;
  m.roads = 1;
  m.radar = 1;
  m.ring = 26;
  m.inner = 13;
  m.scale_q16 = (int32_t) lroundf(map_scale_ring(lat_e4, 26, 16093.4f) * 65536.0f);
  snprintf(m.ring_text, sizeof m.ring_text, "10MI");
  return m;
}

void add_blip(AppMap &m, int x, int y, int track, uint8_t ink, uint8_t flags, uint8_t number) {
  AppBlip &b = m.blips[m.n_blips++];
  b.x = (int16_t) x;
  b.y = (int16_t) y;
  b.track = (int16_t) track;
  b.ink = ink;
  b.flags = flags;
  b.number = number;
}

PxBox grow(const PxBox &b) { return PxBox{(int16_t) (b.x - 1), (int16_t) (b.y - 1), (int16_t) (b.w + 2), (int16_t) (b.h + 2)}; }

// Every mark's box, as labels must avoid them.
std::vector<PxBox> radar_taken(const AppMap &m) {
  std::vector<PxBox> out;
  const int lw = px_text_w(m.ring_text, 1);
  out.push_back(PxBox{(int16_t) (m.w / 2 - 1), 0, 3, 5});
  out.push_back(PxBox{(int16_t) (m.w - 1 - lw), (int16_t) (m.h - 6), (int16_t) lw, 5});
  out.push_back(PxBox{(int16_t) (m.w / 2 - 3), (int16_t) (m.h / 2 - 3), 6, 6});
  MapMark mk[kAppBlips];
  map_marks(m, mk);
  for (int i = 0; i < m.n_blips; i++) {
    const int d = m.blips[i].flags & kBlipIsCard ? 3 : 2;
    out.push_back(PxBox{(int16_t) (m.blips[i].x - d), (int16_t) (m.blips[i].y - d), (int16_t) (2 * d + 1),
                        (int16_t) (2 * d + 1)});
    if (mk[i].w > 0)
      out.push_back(PxBox{mk[i].x, mk[i].y, mk[i].w, 5});
  }
  return out;
}

void test_radar() {
  std::vector<uint8_t> bytes = land_sample();
  LandSet s;
  land_check(bytes.data(), bytes.size(), s);
  AppMap m = radar_of(400012, -899988);
  add_blip(m, 84, 30, 90, kBlipCard, kBlipTrack | kBlipIsCard, 0);
  add_blip(m, 44, 18, 200, kBlipPlane, kBlipTrack, 1);
  add_blip(m, 76, 44, 0, kBlipPlane, 0, 2);
  add_blip(m, 52, 42, 0, kBlipGround, kBlipOnGround, 3);
  add_blip(m, 72, 12, 315, kBlipAlert, kBlipTrack, 4);
  snprintf(m.tag, sizeof m.tag, "UAL1234");
  m.tag_of = 1;
  // Numbers and the tag: inside, apart, off every blip
  MapMark mk[kAppBlips];
  map_marks(m, mk);
  LCHECK(mk[1].w == px_text_w("UAL1234", 1) && mk[0].w == 0, "the tag in place of a number; the card has none");
  int placed = 0;
  const std::vector<PxBox> taken = radar_taken(m);
  for (int i = 0; i < m.n_blips; i++) {
    if (mk[i].w == 0)
      continue;
    placed++;
    const PxBox b{mk[i].x, mk[i].y, mk[i].w, 5};
    LCHECK(b.x >= 1 && b.y >= 1 && b.x + b.w <= 127 && b.y + 5 <= 55, "mark %d inside", i);
    for (const PxBox &t : taken)
      LCHECK((t.x == b.x && t.y == b.y && t.w == b.w) || !box_overlap(grow(b), t), "mark %d clear of %d,%d", i, t.x, t.y);
  }
  LCHECK(placed == 4, "the tag and every number fit: %d", placed);
  // Place labels keep off the marks
  MapLabel lab[kMapLabelMax];
  const int n = map_labels(m, s, lab, map_label_cap(m));
  LCHECK(n > 0, "towns on the radar: %d", n);
  for (int i = 0; i < n; i++) {
    LandPlace pl;
    land_place(s, lab[i].place, pl);
    const PxBox b{lab[i].lx, lab[i].ly, (int16_t) (4 * pl.len - 1), 5};
    for (const PxBox &t : taken)
      LCHECK(!box_overlap(b, t), "%s clear of the mark at %d,%d", pl.name, t.x, t.y);
  }
  labels_sound(m, s, "radar");
  // Crowded blips: numbers left off, never overlapping
  AppMap c = radar_of(400012, -899988);
  for (int i = 0; i < kAppBlips; i++)
    add_blip(c, 64 + (i % 2), 28 + (i / 4), 45 * i, kBlipPlane, kBlipTrack, (uint8_t) (i + 1));
  map_marks(c, mk);
  int shown = 0;
  for (int i = 0; i < kAppBlips; i++) {
    shown += mk[i].w > 0;
    for (int j = 0; j < i; j++)
      LCHECK(mk[i].w == 0 || mk[j].w == 0 ||
                 !box_overlap(grow(PxBox{mk[i].x, mk[i].y, mk[i].w, 5}), PxBox{mk[j].x, mk[j].y, mk[j].w, 5}),
             "crowded marks %d and %d apart", i, j);
  }
  LCHECK(shown < kAppBlips, "crowded: some numbers left off: %d", shown);
  // Painted: rings and home without a set; nothing past the canvas
  Canvas bare(128, 56);
  map_paint(bare.p, m, nullptr);
  LCHECK(bare.count(0x484848) > 100 && bare.count(0xFFFFFF) == 4, "rings and home, no landmarks");
  LCHECK(bare.count(0x4A3A24) == 0 && bare.count(0x505050) == 0, "no lines without a set");
  LCHECK(bare.count(0xFFD000) > 0 && bare.count(0x806800) == 12 && bare.count(0xFF4040) > 0,
         "the card's plane, its brackets and the emergency");
  Canvas land(128, 56);
  map_paint(land.p, m, &s);
  LCHECK(land.count(0x4A3A24) > 0 && land.count(0x484848) > 100, "roads and rings");
  // The emergency on top of another plane
  AppMap e = radar_of(400012, -899988);
  add_blip(e, 40, 28, 0, kBlipAlert, kBlipTrack, 1);
  add_blip(e, 40, 28, 0, kBlipCard, kBlipTrack | kBlipIsCard, 0);
  Canvas ec(128, 56);
  map_paint(ec.p, e, &s);
  LCHECK(ec.px[28 * 128 + 40] == rgb565(0xFF4040), "the emergency drawn last");
  // Edges, every heading, long tags: the guard bytes stay
  const int guard = 64;
  std::vector<uint16_t> buf(128 * 56 + 2 * guard, 0xA5A5);
  Px565 g;
  g.px = buf.data() + guard;
  g.w = g.stride = 128;
  g.h = 56;
  g.clip = PxBox{0, 0, 128, 56};
  for (int k = 0; k < 16; k++) {
    AppMap em = radar_of(400012, -899988);
    const int xs[4] = {0, 127, 1, 126}, ys[4] = {0, 55, 54, 1};
    for (int i = 0; i < kAppBlips; i++)
      add_blip(em, xs[(i + k) % 4], ys[(i + k / 4) % 4], 45 * i + k, (uint8_t) (i % 4),
               (uint8_t) ((i % 2 ? kBlipTrack : 0) | (i == 3 ? kBlipIsCard : 0) | (i == 5 ? kBlipOnGround : 0)),
               (uint8_t) i);
    snprintf(em.tag, sizeof em.tag, "WWWWWWWW");
    em.tag_of = (int8_t) (k % kAppBlips);
    snprintf(em.ring_text, sizeof em.ring_text, "250MI");
    map_paint(g, em, k % 2 ? &s : nullptr);
  }
  bool intact = true;
  for (int i = 0; i < guard; i++)
    intact = intact && buf[i] == 0xA5A5 && buf[buf.size() - 1 - i] == 0xA5A5;
  LCHECK(intact, "radar: the guard bytes stay");
}

void test_store() {
  // What NVS keeps is the bytes as sent
  std::vector<uint8_t> sent = land_sample();
  std::vector<uint8_t> store(sent);
  std::vector<uint8_t> back(store);
  LandSet s;
  LCHECK(land_check(back.data(), back.size(), s) == LandError::OK && back == sent, "it reloads the same");
  back[kLandHead + 7] = 'x';
  LCHECK(land_check(back.data(), back.size(), s) != LandError::OK && s.p == nullptr, "a bad byte drops it");
}

}  // namespace

int run_landmark_tests(int &total_checks) {
  test_check();
  test_projection();
  test_paint();
  test_radar();
  test_store();
  total_checks += checks;
  return failures;
}
