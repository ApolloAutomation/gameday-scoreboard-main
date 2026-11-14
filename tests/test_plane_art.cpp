// Plane pictures and airline logos.
#include <cstdio>
#include <fstream>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "apps.h"
#include "flight_airlines.h"
#include "flights.h"
#include "logo_icon.h"
#include "logo_lru.h"
#include "plane_art.h"

using namespace flights;

namespace {

int checks = 0;
int failures = 0;

#define PCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

const char *kind_name(PlaneKind k) {
  static const char *const n[] = {"NONE",     "GENERIC", "LIGHT", "TWIN_PROP", "TURBOPROP", "BIZJET",   "REGIONAL",
                                  "NARROW",   "WIDE",    "JUMBO", "HELI",      "FIGHTER",   "MIL_CARGO"};
  return (int) k < kPlaneKinds ? n[(int) k] : "?";
}

void test_kinds() {
  // Every type the firmware names has a real picture.
  const std::string src = slurp("../components/gameday/flights.cpp");
  const size_t at = src.find("const char kTypes[]"), end = src.find("const char *type_for");
  PCHECK(at != std::string::npos && end != std::string::npos, "kTypes in flights.cpp");
  const std::string block = src.substr(at, end - at);
  const std::regex code("\"([A-Z0-9]{2,4})\\\\0\" \"");
  int n = 0;
  for (std::sregex_iterator it(block.begin(), block.end(), code), stop; it != stop; ++it, n++) {
    const std::string c = (*it)[1];
    const PlaneKind k = plane_kind(c.c_str(), "");
    PCHECK(k != PlaneKind::GENERIC && k != PlaneKind::NONE, "%s has a kind", c.c_str());
  }
  PCHECK(n >= 150, "the named types: %d", n);
  const struct {
    const char *type;
    PlaneKind kind;
  } traps[] = {
      {"A310", PlaneKind::WIDE},      {"A319", PlaneKind::NARROW},     {"A124", PlaneKind::MIL_CARGO},
      {"A139", PlaneKind::HELI},      {"B461", PlaneKind::REGIONAL},   {"B407", PlaneKind::HELI},
      {"B350", PlaneKind::TWIN_PROP}, {"B38M", PlaneKind::NARROW},     {"H25B", PlaneKind::BIZJET},
      {"H60", PlaneKind::HELI},       {"C130", PlaneKind::MIL_CARGO},  {"C17", PlaneKind::MIL_CARGO},
      {"C152", PlaneKind::LIGHT},     {"C208", PlaneKind::LIGHT},      {"C25A", PlaneKind::BIZJET},
      {"C340", PlaneKind::TWIN_PROP}, {"PA32", PlaneKind::LIGHT},      {"PA34", PlaneKind::TWIN_PROP},
      {"PA44", PlaneKind::TWIN_PROP}, {"PA46", PlaneKind::LIGHT},      {"BE36", PlaneKind::LIGHT},
      {"BE58", PlaneKind::TWIN_PROP}, {"F100", PlaneKind::REGIONAL},   {"F50", PlaneKind::TURBOPROP},
      {"F2TH", PlaneKind::BIZJET},    {"F16", PlaneKind::FIGHTER},     {"E35L", PlaneKind::BIZJET},
      {"E135", PlaneKind::REGIONAL},  {"E75L", PlaneKind::REGIONAL},   {"K35R", PlaneKind::NARROW},
      {"B52", PlaneKind::MIL_CARGO},  {"GA8", PlaneKind::LIGHT},       {"GLF6", PlaneKind::BIZJET},
      {"B744", PlaneKind::JUMBO},     {"A388", PlaneKind::JUMBO},      {"B789", PlaneKind::WIDE},
      {"A359", PlaneKind::WIDE},      {"CRJ9", PlaneKind::REGIONAL},   {"DH8D", PlaneKind::TURBOPROP},
      {"AT76", PlaneKind::TURBOPROP}, {"C40", PlaneKind::NARROW},      {"C402", PlaneKind::TWIN_PROP},
      {"EC35", PlaneKind::HELI},      {"SR22", PlaneKind::LIGHT},      {"P28A", PlaneKind::LIGHT},
      {"GLID", PlaneKind::LIGHT},     {"ULAC", PlaneKind::LIGHT},      {"UHEL", PlaneKind::HELI},
      {"GYRO", PlaneKind::HELI},      {"BALL", PlaneKind::NONE},       {"SHIP", PlaneKind::NONE},
      {"UAV", PlaneKind::NONE},       {"PARA", PlaneKind::NONE},       {"GLEX", PlaneKind::BIZJET},
      {"CL35", PlaneKind::BIZJET},    {"CL2T", PlaneKind::TURBOPROP},  {"GA7", PlaneKind::TWIN_PROP},
      {"GA6C", PlaneKind::BIZJET},    {"K35E", PlaneKind::NARROW},     {"E3TF", PlaneKind::NARROW},
      {"B703", PlaneKind::NARROW},    {"E6", PlaneKind::NARROW},       {"A346", PlaneKind::WIDE},
      {"IL96", PlaneKind::WIDE},      {"B748", PlaneKind::JUMBO},      {"H269", PlaneKind::HELI},
      {"EN48", PlaneKind::HELI},      {"G2CA", PlaneKind::HELI},       {"B205", PlaneKind::HELI},
      {"MI8", PlaneKind::HELI},       {"DHC2", PlaneKind::LIGHT},      {"DHC3", PlaneKind::LIGHT},
      {"PA38", PlaneKind::LIGHT},     {"BE99", PlaneKind::TURBOPROP},  {"B36T", PlaneKind::LIGHT},
      {"B37M", PlaneKind::NARROW},    {"B39M", PlaneKind::NARROW},     {"B3XM", PlaneKind::NARROW},
  };
  for (const auto &t : traps)
    PCHECK(plane_kind(t.type, "") == t.kind, "%s: %s, want %s", t.type, kind_name(plane_kind(t.type, "")),
           kind_name(t.kind));
  const struct {
    const char *cat;
    PlaneKind kind;
  } cats[] = {{"A1", PlaneKind::LIGHT},   {"A2", PlaneKind::BIZJET},  {"A3", PlaneKind::NARROW},
              {"A4", PlaneKind::NARROW},  {"A5", PlaneKind::WIDE},    {"A6", PlaneKind::FIGHTER},
              {"A7", PlaneKind::HELI},    {"B1", PlaneKind::LIGHT},   {"B2", PlaneKind::NONE},
              {"B3", PlaneKind::NONE},    {"B4", PlaneKind::LIGHT},   {"B6", PlaneKind::NONE},
              {"B7", PlaneKind::NONE},    {"A0", PlaneKind::GENERIC}, {"C1", PlaneKind::GENERIC},
              {"A", PlaneKind::GENERIC},  {"", PlaneKind::GENERIC}};
  for (const auto &c : cats)
    PCHECK(plane_kind("", c.cat) == c.kind, "category %s: %s", c.cat, kind_name(plane_kind("", c.cat)));
  PCHECK(plane_kind("ZZZZ", "A7") == PlaneKind::HELI, "an unknown type: its category");
  PCHECK(plane_kind("B738", "A7") == PlaneKind::NARROW, "a known type beats its category");
  PCHECK(plane_kind("GLID", "B1") == PlaneKind::LIGHT && plane_kind("BALL", "A1") == PlaneKind::NONE,
         "special designators beat prefixes and categories");
  PCHECK(plane_kind("ZZZZ", "") == PlaneKind::GENERIC && plane_kind(nullptr, nullptr) == PlaneKind::GENERIC,
         "nothing known: a plain plane");
  PCHECK(plane_kind("B73", "") == PlaneKind::GENERIC && plane_kind("C", "") == PlaneKind::GENERIC,
         "a prefix alone isn't a type");
}

void test_sprites() {
  PCHECK(plane_sprite(PlaneKind::NONE) == nullptr && plane_sprite((PlaneKind) kPlaneKinds) == nullptr, "none");
  for (int k = 1; k < kPlaneKinds; k++) {
    const PlaneSprite *s = plane_sprite((PlaneKind) k);
    const char *name = kind_name((PlaneKind) k);
    PCHECK(s != nullptr && s->bits != nullptr, "%s drawn", name);
    if (s == nullptr)
      continue;
    PCHECK(s->w <= kPlaneArtMaxW && s->h <= kPlaneArtMaxH && s->w <= espn::kAppArtMaxW &&
               s->h <= espn::kAppArtMaxH,
           "%s %dx%d fits", name, s->w, s->h);
    int body = 0, tail = 0, top = 0, bottom = 0, left = 0, right = 0;
    for (int y = 0; y < s->h; y++)
      for (int x = 0; x < s->w; x++) {
        const uint8_t v = sprite_px(*s, x, y);
        body += v == kArtBody;
        tail += v == kArtTail;
        top += y == 0 && v != kArtClear;
        bottom += y == s->h - 1 && v != kArtClear;
        left += x == 0 && v != kArtClear;
        right += x == s->w - 1 && v != kArtClear;
      }
    PCHECK(body > 0 && tail > 0, "%s: body %d, tail %d", name, body, tail);
    PCHECK(top && bottom && left && right, "%s: no clear edge", name);
  }
  const PlaneKind order[] = {PlaneKind::JUMBO,    PlaneKind::WIDE,      PlaneKind::MIL_CARGO,
                             PlaneKind::NARROW,   PlaneKind::REGIONAL,  PlaneKind::TURBOPROP,
                             PlaneKind::BIZJET,   PlaneKind::TWIN_PROP, PlaneKind::LIGHT};
  for (size_t i = 1; i < sizeof(order) / sizeof(order[0]); i++)
    PCHECK(plane_sprite(order[i - 1])->w > plane_sprite(order[i])->w, "%s wider than %s", kind_name(order[i - 1]),
           kind_name(order[i]));
  // Hand-checked pixels of the art
  const PlaneSprite &j = *plane_sprite(PlaneKind::JUMBO);
  PCHECK(j.w == 56 && j.h == 15, "the jumbo's size");
  PCHECK(sprite_px(j, 0, 0) == kArtTail && sprite_px(j, 55, 0) == kArtClear && sprite_px(j, 55, 9) == kArtBody &&
             sprite_px(j, 55, 10) == kArtClear && sprite_px(j, 8, 9) == kArtDetail,
         "the jumbo's tail, nose and windows");
  const PlaneSprite &h = *plane_sprite(PlaneKind::HELI);
  PCHECK(sprite_px(h, 0, 0) == kArtDetail && sprite_px(h, 18, 0) == kArtDetail, "the rotor's ends");

  // Painted as runtime_image lays logos out
  const uint8_t bits[] = {(uint8_t) (kArtBody | kArtTail << 2 | kArtDetail << 4)};
  espn::AppArt a;
  a.w = 3;
  a.h = 1;
  a.bits = bits;
  a.ink[0] = 0xFF0000;
  a.ink[1] = 0x00FF00;
  a.ink[2] = 0x0000FF;
  uint8_t out[9];
  espn::app_art_paint(a, out);
  PCHECK(out[0] == 0x00 && out[1] == 0xF8 && out[2] == 0xE0 && out[3] == 0x07 && out[4] == 0x1F && out[5] == 0x00,
         "RGB565 little endian: %02x%02x %02x%02x %02x%02x", out[0], out[1], out[2], out[3], out[4], out[5]);
  PCHECK(out[6] == 0xFF && out[7] == 0xFF && out[8] == 0xFF, "opaque ink");
  a.flip = true;
  espn::app_art_paint(a, out);
  PCHECK(out[0] == 0x1F && out[1] == 0x00 && out[4] == 0x00 && out[5] == 0xF8, "flipped");
  const uint8_t clear[] = {0};
  a.bits = clear;
  a.w = 2;
  uint8_t two[6];
  espn::app_art_paint(a, two);
  PCHECK(two[4] == 0 && two[5] == 0, "clear stays clear");

  // Doubled: each bit two by two, flipped whole
  a.bits = bits;
  a.w = 3;
  a.flip = false;
  a.scale = 2;
  uint8_t big[6 * 2 * 3];
  espn::app_art_paint(a, big);
  const int n = 12;
  PCHECK(big[0] == 0x00 && big[1] == 0xF8 && big[2] == 0x00 && big[3] == 0xF8 && big[4] == 0xE0 && big[5] == 0x07,
         "doubled across");
  PCHECK(big[12] == 0x00 && big[13] == 0xF8 && big[22] == 0x1F && big[23] == 0x00 && big[2 * n + 11] == 0xFF,
         "doubled down");
  a.flip = true;
  espn::app_art_paint(a, big);
  PCHECK(big[0] == 0x1F && big[2] == 0x1F && big[10] == 0x00 && big[11] == 0xF8, "doubled and flipped");
  a.scale = 1;
  a.flip = false;
  a.bits = clear;
  a.w = 2;

  espn::AppFrame f;
  for (size_t i = 0; i <= espn::kAppArt; i++)
    f.art.push_back(a);
  espn::app_frame_trim(f);
  PCHECK(f.art.size() == espn::kAppArt && f.art[0].w == 2, "trimmed to the pool");
  PCHECK(espn::app_frame_board(f), "a picture alone is a board");
  f.art.resize(2);
  f.art[0].w = espn::kAppArtMaxW + 1;
  espn::app_frame_trim(f);
  PCHECK(f.art.size() == 1 && f.art[0].w == 2, "too big to draw: dropped, the rest kept");
  f.art.clear();
  f.art.push_back(a);
  // The drawn size counts, scale included
  a.w = 56;
  a.h = 15;
  a.scale = 2;
  f.art = {a};
  espn::app_frame_trim(f);
  PCHECK(f.art.size() == 1, "a jumbo doubled fits");
  f.art[0].scale = 3;
  espn::app_frame_trim(f);
  PCHECK(f.art.empty(), "tripled it doesn't");
}

// One RGB565 + alpha image, filled per pixel.
struct Img {
  int w, h;
  std::vector<uint8_t> px;
  Img(int w_, int h_) : w(w_), h(h_), px(espn::logo_icon_bytes(w_, h_), 0) {}
  void set(int x, int y, int r, int g, int b, int a) { espn::logo_px_put(px.data(), w * h, y * w + x, r, g, b, a); }
  void get(int x, int y, int &r, int &g, int &b, int &a) const {
    espn::logo_px_get(px.data(), w * h, y * w + x, r, g, b, a);
  }
  int alpha(int x, int y) const { return px[2 * w * h + y * w + x]; }
};

void test_logo_pipeline() {
  // A white tile with a red square, white inside it
  Img t(32, 32);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      const bool red = x >= 10 && x < 22 && y >= 10 && y < 22;
      t.set(x, y, 255, red ? 20 : 255, red ? 40 : 255, 255);
    }
  t.set(16, 16, 255, 255, 255, 255);
  std::vector<uint16_t> stack;
  const int cleared = espn::logo_icon_knockout(t.px.data(), 32, 32, stack);
  PCHECK(cleared == 32 * 32 - 144, "the white joined to the edge: %d", cleared);
  PCHECK(t.alpha(0, 0) == 0 && t.alpha(10, 10) == 255 && t.alpha(16, 16) == 255, "the inner white stays");
  std::vector<uint8_t> small = espn::logo_icon_shrink(t.px.data(), 32, 32, 16);
  PCHECK(espn::logo_icon_usable(small.data(), 16), "a 6x6 red square reads");

  // A navy mark on clear is turned light
  Img m(16, 16);
  for (int y = 4; y < 12; y++)
    for (int x = 4; x < 12; x++)
      m.set(x, y, 0, 0, 50, 255);
  PCHECK(!espn::logo_icon_usable(m.px.data(), 16), "navy on black doesn't read");
  PCHECK(espn::logo_icon_lift(m.px.data(), 16, 16, true), "lifted");
  int r, g, b, a;
  m.get(6, 6, r, g, b, a);
  PCHECK(std::max(r, std::max(g, b)) >= 0x80 && a == 255, "made light: %d %d %d", r, g, b);
  PCHECK(espn::logo_icon_usable(m.px.data(), 16), "and then reads");

  // A navy tile lifted despite a white accent
  Img nt(16, 16);
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      const bool accent = x >= 7 && x < 9 && y >= 7 && y < 9;
      nt.set(x, y, accent ? 255 : 0, accent ? 255 : 0, accent ? 255 : 90, 255);
    }
  std::vector<uint8_t> kept = nt.px;
  PCHECK(!espn::logo_icon_lift(kept.data(), 16, 16), "by its brightest: the accent keeps it");
  PCHECK(espn::logo_icon_lift(nt.px.data(), 16, 16, true), "a navy tile lifted");
  nt.get(0, 0, r, g, b, a);
  PCHECK(b >= 0xC0 && r == 0 && g == 0, "its navy made bright: %d %d %d", r, g, b);
  nt.get(7, 7, r, g, b, a);
  PCHECK(r >= 0xF0 && g >= 0xF0 && b >= 0xF0, "its white kept");
  PCHECK(espn::logo_icon_usable(nt.px.data(), 16), "and it reads");

  // Mostly dim with a few bright pixels: not lifted, unreadable
  Img dim(16, 16);
  for (int i = 0; i < 40; i++)
    dim.set(2 + i % 12, 6 + i / 12, i < 12 ? 170 : 100, i < 12 ? 170 : 100, i < 12 ? 170 : 100, 255);
  PCHECK(!espn::logo_icon_lift(dim.px.data(), 16, 16, true), "a quarter bright: left alone");
  PCHECK(!espn::logo_icon_usable(dim.px.data(), 16), "too dim to read");
  for (int i = 0; i < 40; i++)
    dim.set(2 + i % 12, 6 + i / 12, 180, 180, 180, 255);
  PCHECK(espn::logo_icon_usable(dim.px.data(), 16), "the same shape, bright, reads");

  // Too thin, too small, or nothing at all
  Img bar(16, 16), speck(16, 16), none(16, 16), ba(16, 16);
  for (int x = 0; x < 16; x++)
    for (int y = 7; y < 9; y++)
      bar.set(x, y, 255, 255, 255, 255);
  for (int i = 0; i < 3; i++)
    speck.set(5 + i, 5, 255, 255, 255, 255);
  PCHECK(!espn::logo_icon_usable(bar.px.data(), 16), "a 16x2 bar is a wordmark");
  PCHECK(!espn::logo_icon_usable(speck.px.data(), 16), "a speck");
  PCHECK(!espn::logo_icon_usable(none.px.data(), 16), "clear");
  for (int x = 1; x < 15; x++) {
    ba.set(x, 6, 220, 30, 50, 255);
    ba.set(x, 9, 220, 30, 50, 255);
  }
  ba.set(1, 7, 220, 30, 50, 255);
  ba.set(14, 8, 220, 30, 50, 255);
  PCHECK(espn::logo_icon_usable(ba.px.data(), 16), "a 14x4 mark of 30 pixels reads");

  // Shrinking a half-clear edge leaves no dark fringe
  Img edge(32, 32);
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++)
      if (x >= 15)
        edge.set(x, y, 255, 255, 255, 255);
  small = espn::logo_icon_shrink(edge.px.data(), 32, 32, 16);
  espn::logo_px_get(small.data(), 256, 7, r, g, b, a);
  PCHECK(r >= 240 && g >= 240 && b >= 240 && a > 0 && a < 255, "white, half clear: %d %d %d %d", r, g, b, a);

  // Squared: a wide logo centered
  Img wide(4, 2);
  wide.set(0, 0, 255, 0, 0, 255);
  std::vector<uint8_t> sq(espn::logo_icon_bytes(4, 4));
  espn::logo_icon_square(wide.px.data(), 4, 2, sq.data());
  PCHECK(sq[2 * 16 + 4] == 255 && sq[2 * 16 + 0] == 0 && sq[2 * 16 + 15] == 0, "the row moved down one");

  // Google's gray tail
  const uint8_t png4[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                          0,    0,   0,   32,  0,    0,    0,    32,   8, 4, 0, 0, 0};
  uint8_t png6[sizeof(png4)];
  memcpy(png6, png4, sizeof(png4));
  png6[25] = 6;
  const std::string gs = std::string(kGoogleLogoUrl) + "OO.png", fa = std::string(kFlightAwareLogoUrl) + "SKW.png";
  PCHECK(espn::logo_png_color_type(png4, sizeof(png4)) == 4 && espn::logo_png_color_type(png4, 20) == -1,
         "the color type");
  PCHECK(espn::logo_placeholder(gs.c_str(), png4, sizeof(png4)), "gray and alpha from Google");
  PCHECK(!espn::logo_placeholder(gs.c_str(), png6, sizeof(png6)), "a real logo");
  PCHECK(!espn::logo_placeholder(fa.c_str(), png4, sizeof(png4)), "only Google's");
  PCHECK(!espn::logo_placeholder("https://www.gstatic.com/other/OO.png", png4, sizeof(png4)), "only its logos");
  PCHECK(espn::logo_key(nullptr, 0) == 2166136261u && espn::kGstaticBlankKey == 0x8cf55aa4u, "the constant");

  // Random images never break the steps
  std::mt19937 rng(7);
  for (int round = 0; round < 300; round++) {
    const int w = 1 + (int) (rng() % 32), h = 1 + (int) (rng() % 32), s = std::max(w, h);
    std::vector<uint8_t> img(espn::logo_icon_bytes(w, h));
    for (uint8_t &v : img)
      v = (uint8_t) (rng() % 3 == 0 ? 255 : rng());
    std::vector<uint8_t> sqr(espn::logo_icon_bytes(s, s));
    espn::logo_icon_square(img.data(), w, h, sqr.data());
    espn::logo_icon_knockout(sqr.data(), s, s, stack);
    std::vector<uint8_t> icon = espn::logo_icon_shrink(sqr.data(), s, s, 16);
    espn::logo_icon_lift(icon.data(), 16, 16, true);
    espn::logo_icon_usable(icon.data(), 16);
    PCHECK(icon.size() == 768 && stack.empty(), "random %dx%d", w, h);
  }
}

AirlineName airline(const char *icao, const char *iata, uint8_t logo) {
  AirlineName a{};
  strncpy(a.icao, icao, sizeof(a.icao) - 1);
  strncpy(a.iata, iata, sizeof(a.iata) - 1);
  strncpy(a.name, "Test", sizeof(a.name) - 1);
  a.logo = logo;
  return a;
}

std::vector<std::string> airline_logo_urls(const AirlineName &a) {
  std::vector<std::string> out;
  char url[160];
  for (uint8_t k = 0; airline_logo_url(a, k, url, sizeof url) > 0; k++)
    out.emplace_back(url);
  return out;
}

void test_logo_urls() {
  const std::string g = kGoogleLogoUrl, fa = kFlightAwareLogoUrl;
  std::vector<std::string> u = airline_logo_urls(airline("DAL", "DL", kLogoGoogle));
  PCHECK(u.size() == 2 && u[0] == g + "DL.png" && u[1] == fa + "DAL.png", "Google, then FlightAware");
  PCHECK(u[0] == "https://www.gstatic.com/flights/airline_logos/32px/DL.png" &&
             u[1] == "https://www.flightaware.com/images/airline_logos/90p/DAL.png",
         "the addresses");
  u = airline_logo_urls(airline("ASA", "AS", kLogoFlightAware));
  PCHECK(u.size() == 2 && u[0] == fa + "ASA.png" && u[1] == g + "AS.png", "FlightAware first when hinted");
  PCHECK(airline_logo_urls(airline("SKW", "OO", kLogoNone)).empty(), "none usable: never asked");
  u = airline_logo_urls(airline("XSR", "", kLogoUnknown));
  PCHECK(u.size() == 1 && u[0] == fa + "XSR.png", "no IATA code: FlightAware only");
  u = airline_logo_urls(airline("ZZZ", "Z9", kLogoUnknown));
  PCHECK(u.size() == 2 && u[0] == g + "Z9.png", "unknown: Google first");
  u = airline_logo_urls(airline("ZZY", "", kLogoFlightAware));
  PCHECK(u.size() == 1, "FlightAware alone");

  for (const AirlineName &a : kAirlines)
    PCHECK(a.logo <= kLogoNone, "%s: a known hint", a.icao);
  PCHECK(airline_by_icao("DAL") != nullptr && airline_by_icao("DA") == nullptr && airline_by_icao(nullptr) == nullptr,
         "by ICAO code");

  // The airline on the plane beats the one flying it
  Plane p;
  strcpy(p.callsign, "RPA4505");
  strcpy(p.brand, "AAL");
  PCHECK(logo_airline(p) != nullptr && strcmp(logo_airline(p)->icao, "AAL") == 0, "Republic in American's paint");
  strcpy(p.brand, "QQQ");
  PCHECK(logo_airline(p) != nullptr && strcmp(logo_airline(p)->icao, "RPA") == 0, "an unknown brand: the operator");
  p.brand[0] = '\0';
  PCHECK(logo_airline(p) != nullptr && strcmp(logo_airline(p)->icao, "RPA") == 0, "no brand: the operator");
  strcpy(p.callsign, "N7QD");
  PCHECK(logo_airline(p) == nullptr, "a private plane: none");

  const AirlineName *skw = airline_by_icao("SKW"), *eja = airline_by_icao("EJA");
  PCHECK(skw != nullptr && eja != nullptr, "SkyWest and NetJets listed");
  if (skw != nullptr && eja != nullptr) {
    PCHECK(logo_chip(*skw, flight_number("SKW5678")) == "OO", "SkyWest's code: %s",
           logo_chip(*skw, flight_number("SKW5678")).c_str());
    PCHECK(logo_chip(*eja, "EJA974") == "EJA", "NetJets by its ICAO code");
    PCHECK(logo_chip(*skw, "") == "SKW", "no flight number: ICAO");
  }
}

Home nyc() {
  Home h;
  h.lat = 40.71;
  h.lon = -74.01;
  h.radius_nm = 25;
  return h;
}

const int64_t kNow = 1794457500;

// Each recorded plane, one at a time.
std::vector<Plane> fr24_planes() {
  std::vector<Plane> out;
  JsonDocument doc;
  deserializeJson(doc, slurp("fixtures/flights_fr24_nyc.json"));
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    if (!kv.value().is<JsonArrayConst>())
      continue;
    Plane p;
    if (plane_from_fr24(kv.value().as<JsonArrayConst>(), nyc(), kNow, p))
      out.push_back(p);
  }
  return out;
}

const Plane *by_label(const std::vector<Plane> &v, const char *label) {
  for (const Plane &p : v)
    if (strcmp(plane_label(p), label) == 0)
      return &p;
  return nullptr;
}

const Text *text_of(const Frame &f, const std::string &s) {
  for (const Text &t : f.items)
    if (t.text == s)
      return &t;
  return nullptr;
}

Frame card(const Plane &p, int w) {
  Nearby n;
  n.planes[0] = p;
  n.n = 1;
  n.air = 1;
  FrameOpts o;
  o.w = w;
  return flight_frame(n, nullptr, o, (uint32_t) kNow);
}

void test_brand_and_cards() {
  const std::vector<Plane> v = fr24_planes();
  PCHECK(v.size() >= 25, "the recording: %zu planes", v.size());
  const struct {
    const char *label, *brand;
  } brands[] = {{"RPA4505", "AAL"}, {"EDV5371", "DAL"}, {"MTN7303", "FDX"}, {"EJA974", "EJA"}, {"N7QD", ""}};
  for (const auto &b : brands) {
    const Plane *p = by_label(v, b.label);
    PCHECK(p != nullptr && strcmp(p->brand, b.brand) == 0, "%s painted %s: %s", b.label, b.brand,
           p != nullptr ? p->brand : "?");
  }

  // Every recorded plane as the card, at every width.
  for (const Plane &p : v)
    for (int w : {64, 128, 192, 256}) {
      Nearby n;
      n.planes[0] = p;
      n.n = 1;
      for (const Plane &q : v)
        if (n.n < kNear && strcmp(q.hex, p.hex) != 0)
          n.planes[n.n++] = q;
      for (bool ticker : {true, false}) {
        FrameOpts o;
        o.w = w;
        o.ticker = ticker;
        const Frame f = flight_frame(n, nullptr, o, (uint32_t) kNow);
        const bool brand = logo_airline(p) != nullptr;
        const bool art = plane_sprite(plane_kind(p.type, p.category)) != nullptr;
        PCHECK(f.art.size() == (art ? 1u : 0u), "%s %d: a picture", plane_label(p), w);
        if (!f.art.empty())
          PCHECK(f.art[0].flip == (p.track > 180), "%s %d: faces its way", plane_label(p), w);
        for (const Logo &l : f.logos)
          PCHECK(l.alt >= 0 && f.items[(size_t) l.alt].text == logo_chip(*logo_airline(p), p.flight[0] != '\0'
                                                                                             ? p.flight
                                                                                             : flight_number(
                                                                                                   p.callsign)),
                 "%s %d: its code is the alt", plane_label(p), w);
        PCHECK(brand || f.logos.empty(), "%s %d: no airline, no logo", plane_label(p), w);
      }
    }

  // 128: logo top right, picture beside the route
  const Plane *swa = by_label(v, "SWA1678");
  PCHECK(swa != nullptr, "SWA1678");
  if (swa != nullptr) {
    Frame f = card(*swa, 128);
    PCHECK(f.logos.size() == 1 && f.logos[0].x == 111 && f.logos[0].y == 0, "the logo at 111,0");
    PCHECK(f.art.size() == 1 && f.art[0].kind == PlaneKind::NARROW, "a 737");
    const PlaneSprite *s = plane_sprite(PlaneKind::NARROW);
    PCHECK(f.art[0].x == 72 + (56 - s->w) / 2 && f.art[0].y == 17 + (21 - s->h) / 2, "centered in 72,17,56x21");
    PCHECK(text_of(f, "737 MAX 8") != nullptr, "the type without its maker");
    PCHECK(text_of(f, "WN") != nullptr && text_of(f, "WN")->font == BoardFont::S12, "the code while it loads");
    PCHECK(text_of(f, "Southwest") != nullptr && text_of(f, "Southwest")->x + text_of(f, "Southwest")->w <= 109,
           "the name before the logo");
    PCHECK(f.art[0].tail != kColTail, "Southwest's tail in its color");
    // 64: logo and a small callsign
    f = card(*swa, 64);
    const Text *cs = text_of(f, "SWA1678");
    PCHECK(cs != nullptr && cs->x == 18 && cs->y == 0 && cs->font == BoardFont::S8, "the callsign beside the logo");
    PCHECK(f.logos.size() == 1 && f.logos[0].x == 0 && f.logos[0].y == 0, "the logo at 0,0");
    PCHECK(text_of(f, "DEN-LGA") != nullptr && text_of(f, "DEN-LGA")->y == 8, "the route under it");
    PCHECK(f.art.size() == 1 && f.art[0].y >= 16 && f.art[0].y + s->h <= 32, "the picture in rows 16-31");
    PCHECK(text_of(f, "Southwest") == nullptr, "no name line: the logo stands in");
  }
  const Plane *rpa = by_label(v, "RPA4505");
  if (rpa != nullptr) {
    const Frame f = card(*rpa, 128);
    PCHECK(text_of(f, "Republic") != nullptr && text_of(f, "AA") != nullptr, "Republic's name, American's code");
    PCHECK(!f.logos.empty() && strcmp(f.logos[0].icao, "AAL") == 0 &&
               airline_logo_urls(*airline_by_icao(f.logos[0].icao))[0].find("/AA.png") != std::string::npos,
           "American's logo");
  }
  const Plane *eja = by_label(v, "EJA974");
  if (eja != nullptr) {
    const Frame f = card(*eja, 64);
    const Text *chip = text_of(f, "EJA");
    PCHECK(chip != nullptr && chip->font == BoardFont::S8 && chip->x == 0 && chip->w == 16, "a three-letter code");
  }
  const Plane *heli = by_label(v, "N7QD");
  if (heli != nullptr) {
    Frame f = card(*heli, 64);
    PCHECK(f.logos.empty() && text_of(f, "N7QD") != nullptr && text_of(f, "N7QD")->font == BoardFont::S16,
           "a private plane keeps its big callsign");
    PCHECK(f.art.size() == 1 && f.art[0].kind == PlaneKind::HELI && f.art[0].tail == kColTail, "a gray helicopter");
    f = card(*heli, 128);
    PCHECK(text_of(f, "Bell 206") != nullptr && text_of(f, "Bell 206")->x == 66, "no logo: the name as before");
  }
  // Westbound faces left; no track faces right
  Plane west = v[0], none = v[0];
  west.track = 270;
  none.track = -1;
  PCHECK(card(west, 128).art[0].flip && !card(none, 128).art[0].flip, "flip by track");
  Plane balloon = v[0];
  strcpy(balloon.type, "");
  strcpy(balloon.category, "B2");
  PCHECK(card(balloon, 64).art.empty(), "a balloon: no picture");
}

}  // namespace

int run_plane_art_tests(int &total_checks) {
  test_kinds();
  test_sprites();
  test_logo_pipeline();
  test_logo_urls();
  test_brand_and_cards();
  total_checks += checks;
  return failures;
}
