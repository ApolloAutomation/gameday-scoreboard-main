// Weather's radar with landmarks, as the panel layers it
#pragma once

#include <cmath>
#include <functional>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ArduinoJson.h"
#include "app_flights.h"
#include "app_weather.h"
#include "field_render.h"
#include "land_set.h"
#include "landmarks.h"
#include "map_canvas.h"
#include "gif_write.h"
#include "png_write.h"
#include "sky.h"

namespace espn {

namespace review_map {

inline std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

inline uint32_t g_ms = 0;
inline uint32_t ms_now() { return g_ms; }
inline struct tm local_of(int64_t t) {
  const time_t x = (time_t) (t - 8 * 3600);
  struct tm out {};
  gmtime_r(&x, &out);
  return out;
}

// Every read due now, from the Tahoe fixtures.
inline void read_all(wx::WeatherApp &a, const std::string &fx, int64_t now) {
  for (int k = 0; k < 8; k++) {
    AppRequest req;
    if (!a.request(now, req))
      break;
    const std::string &u = req.url;
    const char *file = u.find("air-quality") != std::string::npos   ? "weather_aq_tahoe.json"
                       : u.find("api.open-meteo.com") != std::string::npos ? "weather_om_tahoe.json"
                       : u.find("weather.gov") != std::string::npos   ? "weather_nws_tahoe.json"
                                                                      : "weather_rainviewer.json";
    JsonDocument filter, doc;
    const bool ok = a.filter(filter) &&
                    !deserializeJson(doc, slurp(fx + "/" + file), DeserializationOption::Filter(filter),
                                     DeserializationOption::NestingLimit(40)) &&
                    a.parse_doc(doc.as<JsonObjectConst>(), now);
    AppResult r;
    r.status = 200;
    a.ended(r);
    a.apply(ok, now);
  }
}

// A made-up storm west of home, as RainViewer's colors
inline void rain_rgba(int x, int y, uint8_t out[4]) {
  const float dx = (x - 96) / 34.0f, dy = (y - 120) / 22.0f;
  const float d = sqrtf(dx * dx + dy * dy) + 0.25f * sinf(x * 0.21f) * cosf(y * 0.17f);
  out[0] = out[1] = out[2] = out[3] = 0;
  if (d > 1.0f)
    return;
  if (d < 0.25f) {
    out[0] = 230, out[1] = 40, out[2] = 30;
  } else if (d < 0.55f) {
    out[0] = 240, out[1] = 210, out[2] = 0;
  } else {
    out[0] = 20, out[1] = 170, out[2] = 70;
  }
  out[3] = 220;
}

inline void line(Canvas &cv, int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    cv.put(x0, y0, c);
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

// Canvas, rain, home, ring, art, labels, ticker
inline Canvas compose(const AppFrame &f, int w, int h, const LandSet *set, bool rain, const ReviewFonts &fonts) {
  Canvas cv;
  cv.w = w;
  cv.h = h;
  cv.px.assign((size_t) w * h, 0);
  const AppMap &m = f.map;
  if (m.w > 0) {
    std::vector<uint16_t> buf((size_t) m.w * m.h);
    Px565 p;
    p.px = buf.data();
    p.w = p.stride = m.w;
    p.h = m.h;
    p.clip = PxBox{0, 0, m.w, m.h};
    map_paint(p, m, set);
    for (int y = 0; y < m.h; y++)
      for (int x = 0; x < m.w; x++)
        cv.put(m.x + x, m.y + y, buf[(size_t) y * m.w + x]);
  }
  // A drawn plane's sky at the review clock
  const AppSky &sk = f.sky;
  if (sk.w > 0) {
    std::vector<uint16_t> buf((size_t) sk.w * sk.h);
    Px565 p;
    p.px = buf.data();
    p.w = p.stride = sk.w;
    p.h = sk.h;
    p.clip = PxBox{0, 0, sk.w, sk.h};
    sky_paint(p, sk, g_ms);
    for (int y = 0; y < sk.h; y++)
      for (int x = 0; x < sk.w; x++)
        cv.put(sk.x + x, sk.y + y, buf[(size_t) y * sk.w + x]);
  }
  const AppPhoto &ph = f.photo;
  if (rain && ph.w > 0) {
    MapCanvas mc;
    std::vector<MapCell> cells((size_t) ph.w * ph.h, MapCell{});
    mc.setup(256, 256, ph.w, ph.h);
    mc.cells = cells.data();
    uint8_t rgba[4];
    for (int y = 0; y < 256; y++)
      for (int x = 0; x < 256; x++) {
        rain_rgba(x, y, rgba);
        mc.paint(x, y, 1, 1, rgba);
      }
    std::vector<uint8_t> img((size_t) ph.w * ph.h * 3);
    mc.finish(img.data());
    const int n = ph.w * ph.h;
    for (int i = 0; i < n; i++) {
      const int x = ph.x + i % ph.w, y = ph.y + i / ph.w;
      const uint16_t s = (uint16_t) (img[2 * i] | img[2 * i + 1] << 8);
      const uint32_t a = img[2 * n + i];
      const uint16_t d = cv.px[(size_t) y * w + x];
      auto mix = [a](int sv, int dv) { return (int) ((sv * a + dv * (255 - a)) / 255); };
      cv.put(x, y,
             (uint16_t) (mix(s >> 11, d >> 11) << 11 | mix((s >> 5) & 63, (d >> 5) & 63) << 5 | mix(s & 31, d & 31)));
    }
  }
  if (f.bar.w > 0)
    for (int y = 0; y < f.bar.h; y++)
      for (int x = 0; x < f.bar.w; x++)
        cv.put(f.bar.x + x, f.bar.y + y, rgb565(f.bar.color));
  for (size_t i = 1; i < f.line.pts.size(); i++)
    line(cv, f.line.pts[i - 1].x, f.line.pts[i - 1].y, f.line.pts[i].x, f.line.pts[i].y, rgb565(f.line.color));
  for (const AppArt &a : f.art) {
    const int s = a.scale < 1 ? 1 : a.scale, aw = a.w * s, ah = a.h * s;
    std::vector<uint8_t> out((size_t) aw * ah * 3);
    app_art_paint(a, out.data());
    for (int i = 0; i < aw * ah; i++)
      if (out[2 * aw * ah + i])
        cv.put(a.x + i % aw, a.y + i / aw, (uint16_t) (out[2 * i] | out[2 * i + 1] << 8));
  }
  for (size_t i = 0; i < f.texts.size(); i++)
    if (!(rain && (int) i == ph.alt))
      review_text(cv, fonts, f.texts[i]);
  AppText tk;
  tk.x = 0;
  tk.w = (int16_t) w;
  tk.y = (int16_t) (h - 8);
  tk.text = f.ticker;
  tk.color = 0xFFFFFF;
  review_text(cv, fonts, tk, false);
  return cv;
}

}  // namespace review_map

// Weather's radar page at each size; returns the files written
inline int render_map_views(const std::string &dir, const std::string &fixtures, const ReviewFonts &fonts) {
  using namespace review_map;
  AppEnv &env = app_env();
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  AppSlots &slots = app_slots();
  auto *fl = new flights::FlightsApp();
  fl->setup();
  app_register(slots, fl);
  fl->set("flyhome", "38.9399,-119.9772");
  auto *a = new wx::WeatherApp();
  const int64_t now = 1794630600;
  a->shown(true);
  read_all(*a, fixtures, now);
  AppFrame first;
  a->frame(64, 64, now, first);
  int64_t t = now;
  while (a->page(t) != wx::Page::RADAR && t < now + 200)
    t++;
  // Invented towns, or REVIEW_LAND's set moved here
  std::vector<uint8_t> bytes = land_encode(3894, -11998, 250, land_sample_places(), land_sample_lines());
  const char *own = getenv("REVIEW_LAND");
  if (own != nullptr && *own) {
    const std::string b = slurp(own);
    if (b.size() > kLandHead) {
      bytes.assign(b.begin(), b.end());
      bytes[4] = (uint8_t) (3894 & 255);
      bytes[5] = (uint8_t) (3894 >> 8);
      bytes[6] = (uint8_t) (-11998 & 255);
      bytes[7] = (uint8_t) ((-11998 >> 8) & 255);
    }
  }
  LandSet set;
  if (land_check(bytes.data(), bytes.size(), set) != LandError::OK)
    printf("the landmark set is refused\n");
  set.id = 1;
  int n = 0;
  static const int kSizes[][2] = {{64, 64}, {128, 64}, {256, 64}, {128, 128}};
  for (const auto &sz : kSizes) {
    for (int k = 0; k < 3; k++) {
      // The frame knows when landmarks draw
      AppFrame f;
      g_ms = 0;
      env.land = k == 0 ? nullptr : &set;
      a->frame(sz[0], sz[1], t, f);
      app_frame_trim(f);
      static const char *const kKind[] = {"before", "base", "rain"};
      const Canvas cv = compose(f, sz[0], sz[1], k == 0 ? nullptr : &set, k != 1, fonts);
      char name[96];
      snprintf(name, sizeof name, "/radar_%dx%d_%s.png", sz[0], sz[1], kKind[k]);
      if (png_save(dir + name, cv.px.data(), cv.w, cv.h, 6)) {
        printf("%s%s\n", dir.c_str(), name);
        n++;
      }
    }
  }
  delete a;
  env = AppEnv{};
  return n;
}

// The Flight Tracker on every size, its radar where there's room
inline int render_plane_views(const std::string &dir, const std::string &fixtures, const ReviewFonts &fonts) {
  using namespace review_map;
  AppEnv &env = app_env();
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  const std::string fr24 = slurp(fixtures + "/flights_fr24_nyc.json");
  // Invented towns around the home's rounded spot
  std::vector<uint8_t> bytes = land_encode(4071, -7401, 250, land_sample_places(), land_sample_lines());
  LandSet set;
  land_check(bytes.data(), bytes.size(), set);
  set.id = 1;
  int n = 0;
  static const int kSizes[][2] = {{64, 64}, {128, 64}, {256, 64}, {128, 128}};
  for (const char *radius : {"28mi", "10mi"})
    for (const auto &sz : kSizes)
      for (int turn = 0; turn < 2; turn++) {
        flights::FlightsApp a;
        a.setup();
        a.set("flyhome", "40.71,-74.01");
        a.set("flyradius", radius);
        a.shown(true);
        const int64_t now = 1794457500;
        AppRequest req;
        if (a.request(now, req)) {
          AppTextStream in(fr24);
          const bool ok = a.parse(in, now);
          a.ended(AppResult{200, false, 0});
          a.apply(ok, now);
        }
        AppFrame f;
        a.frame(sz[0], sz[1], now, f);
        if (turn == 1)
          a.frame(sz[0], sz[1], now + 8, f = AppFrame());
        app_frame_trim(f);
        for (int k = 0; k < 2; k++) {
          if (k == 1 && f.map.w == 0)
            continue;
          const Canvas cv = compose(f, sz[0], sz[1], k == 0 ? nullptr : &set, false, fonts);
          char name[96];
          snprintf(name, sizeof name, "/planes_%s_%dx%d_%s_%s.png", radius, sz[0], sz[1], turn ? "turn2" : "turn1",
                   k ? "land" : "bare");
          if (png_save(dir + name, cv.px.data(), cv.w, cv.h, 6)) {
            printf("%s%s\n", dir.c_str(), name);
            n++;
          }
        }
      }
  env = AppEnv{};
  return n;
}

// The Flight Tracker's drawn planes in their skies: whole screens
// (GIF and a PNG still) at each size, and each kind's box
inline int render_sky_views(const std::string &dir, const std::string &fixtures, const ReviewFonts &fonts) {
  using namespace review_map;
  AppEnv &env = app_env();
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  std::string fr24 = slurp(fixtures + "/flights_fr24_nyc.json");
  int n = 0;
  auto save_gif = [&](const char *name, int w, int h, const std::function<void(std::vector<uint16_t> &)> &paint,
                      uint32_t ms) {
    GifWriter g;
    g.w = w;
    g.h = h;
    g.scale = 4;
    std::vector<uint16_t> buf((size_t) w * h);
    for (g_ms = 0; g_ms < ms; g_ms += kSkyFrameMs) {
      paint(buf);
      g.add(buf.data(), w, (int) (kSkyFrameMs / 10));
    }
    if (g.save(dir + name)) {
      printf("%s%s\n", dir.c_str(), name);
      n++;
    }
  };
  static const int kSizes[][2] = {{64, 64}, {128, 64}, {256, 64}, {128, 128}};
  for (int note = 0; note < 3; note++)
    for (const auto &sz : kSizes) {
      flights::FlightsApp a;
      a.setup();
      a.set("flyhome", "40.71,-74.01");
      a.set("flyradius", "28mi");
      a.shown(true);
      // 2: the card again, its sky turned to night
      const int64_t now = 1794457500;
      AppRequest req;
      if (note != 1 && a.request(now, req)) {
        AppTextStream in(fr24);
        const bool ok = a.parse(in, now);
        a.ended(AppResult{200, false, 0});
        a.apply(ok, now);
      }
      AppFrame f;
      a.frame(sz[0], sz[1], now, f);
      app_frame_trim(f);
      if (note == 2) {
        f.sky.flags &= (uint8_t) ~kSkyDay;
        f.sky.elev = -25;
        f.sky.moon = 2;
      }
      static const char *const kWhat[] = {"card", "note", "night"};
      char name[96];
      snprintf(name, sizeof name, "/sky_%s_%dx%d.gif", kWhat[note], sz[0], sz[1]);
      save_gif(
          name, sz[0], sz[1],
          [&](std::vector<uint16_t> &buf) { buf = compose(f, sz[0], sz[1], nullptr, false, fonts).px; }, 12000);
      g_ms = 3000;
      const Canvas cv = compose(f, sz[0], sz[1], nullptr, false, fonts);
      snprintf(name, sizeof name, "/sky_%s_%dx%d.png", kWhat[note], sz[0], sz[1]);
      if (png_save(dir + name, cv.px.data(), cv.w, cv.h, 6)) {
        printf("%s%s\n", dir.c_str(), name);
        n++;
      }
    }
  // Each kind in the 128 card's box and the 64 card's, day and night
  const flights::PlaneKind kinds[] = {flights::PlaneKind::HELI,   flights::PlaneKind::LIGHT,
                                      flights::PlaneKind::TWIN_PROP, flights::PlaneKind::TURBOPROP,
                                      flights::PlaneKind::NARROW, flights::PlaneKind::JUMBO,
                                      flights::PlaneKind::GENERIC};
  static const char *const kNames[] = {"heli", "light", "twin", "turboprop", "narrow", "jumbo", "generic"};
  for (int i = 0; i < 7; i++)
    for (int box = 0; box < 2; box++)
      for (int sky = 0; sky < 3; sky++) {
        AppSky s;
        s.w = box ? 64 : 56;
        s.h = box ? 16 : 21;
        s.kind = (uint8_t) kinds[i];
        s.flip = i % 2;
        s.tail = 0xD03030;
        s.seed = 0x1234567u * (i + 1);
        s.flags = (uint8_t) ((sky == 1 ? 0 : kSkyDay) | ((i % 5) << kSkyLandShift));
        s.elev = (int8_t) (sky == 1 ? -25 : sky == 2 ? 30 : 4);
        s.home = 0xBEEF01u + (uint32_t) i * 7919u;
        s.cover = sky == 2 ? 3 : 1;
        s.fall = sky == 2 ? 1 : 0;
        s.wind = sky == 2;
        s.moon = 3;
        char name[96];
        static const char *const kSky[] = {"day", "night", "rain"};
        snprintf(name, sizeof name, "/sky_%s_%dx%d_%s.gif", kNames[i], s.w, s.h, kSky[sky]);
        save_gif(name, s.w, s.h, [&](std::vector<uint16_t> &buf) {
          Px565 p;
          p.px = buf.data();
          p.w = p.stride = s.w;
          p.h = s.h;
          p.clip = PxBox{0, 0, s.w, s.h};
          sky_paint(p, s, g_ms);
        }, i == 0 ? 60000 : 20000);
      }
  env = AppEnv{};
  return n;
}

// Wider reads: found, still widening, and the standing screen
inline int render_wide_views(const std::string &dir, const ReviewFonts &fonts) {
  using namespace review_map;
  AppEnv &env = app_env();
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  std::vector<uint8_t> bytes = land_encode(4071, -7401, 250, land_sample_places(), land_sample_lines());
  LandSet set;
  land_check(bytes.data(), bytes.size(), set);
  set.id = 1;
  const int64_t t0 = 1794457500;
  // Three planes 20 to 45 miles out
  auto feed = [](int64_t at) {
    static const double kAt[][3] = {{0.30, 0.10, 45}, {-0.20, -0.45, 300}, {0.55, -0.35, 200}};
    std::string out = "{\"full_count\":3,\"version\":4";
    for (int k = 0; k < 3; k++) {
      char one[200];
      snprintf(one, sizeof one,
               ",\"f%d\":[\"a%05d\",%.4f,%.4f,%d,%d,380,\"1200\",\"\",\"%s\",\"N%dAB\",%lld,\"\",\"\",\"\",0,0,"
               "\"%s\",0,\"\"]",
               k, k, 40.71 + kAt[k][0], -74.01 + kAt[k][1], (int) kAt[k][2], 9000 + 4000 * k,
               k == 1 ? "R44" : "C172", k, (long long) at, k == 0 ? "N123AB" : k == 1 ? "N44HX" : "");
      out += one;
    }
    return out + "}";
  };
  const std::string empty = "{\"full_count\":0,\"version\":4}";
  int n = 0;
  static const int kSizes[][2] = {{64, 64}, {128, 64}, {256, 64}, {128, 128}};
  for (int what = 0; what < 3; what++)
    for (const auto &sz : kSizes)
      for (int turn = 0; turn < 2; turn++) {
        if (what > 0 && turn == 1)
          continue;
        flights::FlightsApp a;
        a.setup();
        a.set("flyhome", "40.71,-74.01");
        a.set("flyradius", what == 2 ? "250mi" : "10mi");
        a.shown(true);
        int64_t now = t0;
        // Found at 50, widening, or none to the cap
        const int reads = what == 0 ? 4 : 1;
        for (int i = 0; i < reads; i++, now += 30) {
          AppRequest req;
          if (!a.request(now, req))
            continue;
          const std::string body = what == 0 && i == reads - 1 ? feed(now) : empty;
          AppTextStream in(body);
          const bool ok = a.parse(in, now);
          a.ended(AppResult{200, false, 0});
          a.apply(ok, now);
        }
        now -= 30;
        AppFrame f;
        a.frame(sz[0], sz[1], now, f);
        if (turn == 1)
          a.frame(sz[0], sz[1], now + 8, f = AppFrame());
        app_frame_trim(f);
        g_ms = 3000;
        const Canvas cv = compose(f, sz[0], sz[1], &set, false, fonts);
        static const char *const kWhat[] = {"found", "widening", "standing"};
        char name[96];
        snprintf(name, sizeof name, "/wide_%s_%dx%d_%s.png", kWhat[what], sz[0], sz[1], turn ? "turn2" : "turn1");
        if (png_save(dir + name, cv.px.data(), cv.w, cv.h, 6)) {
          printf("%s%s\n", dir.c_str(), name);
          n++;
        }
      }
  env = AppEnv{};
  return n;
}

}  // namespace espn
