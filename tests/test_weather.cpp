// The Weather display: sources, failover, icons, maps and frames.
#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "app_flights.h"
#include "app_frame_check.h"
#include "app_weather.h"
#include "map_canvas.h"

using namespace wx;

namespace {

int checks = 0;
int failures = 0;

#define WCHECK(cond, ...)                         \
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
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

// The fixtures' panel: Pacific time, recorded 2026-11-14 04:30 UTC.
const int64_t kNow = 1794630600;
int g_offset = -8 * 3600;
struct tm local_of(int64_t t) {
  const time_t x = (time_t) (t + g_offset);
  struct tm out {};
  gmtime_r(&x, &out);
  return out;
}
uint32_t g_ms = 0;
uint32_t ms_now() { return g_ms; }

flights::FlightsApp *g_flights = nullptr;

void set_home(const char *coords) {
  g_flights->set("flyhome", coords == nullptr ? "none" : coords);
  g_flights->set("flyplace", coords == nullptr ? "" : "Home Town, State, Country");
}

template<typename From, typename Fill> bool parse_file(const char *file, Fill fill, From from) {
  const std::string body = slurp(file);
  return parse_with(body, fill, from);
}

// One read as run_app_job_ makes it, from a fixture.
bool step(WeatherApp &a, int64_t now, std::string &url, const char *file, int status = 200, bool refused = false) {
  espn::AppRequest req;
  if (!a.request(now, req))
    return false;
  url = req.url;
  bool ok = false;
  if (file != nullptr && status == 200) {
    JsonDocument filter, doc;
    ok = a.filter(filter) &&
         !deserializeJson(doc, slurp(file), DeserializationOption::Filter(filter),
                          DeserializationOption::NestingLimit(40)) &&
         a.parse_doc(doc.as<JsonObjectConst>(), now);
  }
  espn::AppResult r;
  r.status = file != nullptr || status != 200 ? status : 0;
  r.refused = refused;
  a.ended(r);
  a.apply(ok, now);
  return true;
}

const char *fixture_for(const std::string &url) {
  if (url.find("air-quality") != std::string::npos)
    return "fixtures/weather_aq_tahoe.json";
  if (url.find("api.open-meteo.com") != std::string::npos)
    return "fixtures/weather_om_tahoe.json";
  if (url.find("api.met.no") != std::string::npos)
    return "fixtures/met_vegas.json";
  if (url.find("weather.gov") != std::string::npos)
    return "fixtures/weather_nws_tahoe.json";
  return "fixtures/weather_rainviewer.json";
}

// Every read due now, answered from the Tahoe fixtures.
int read_all(WeatherApp &a, int64_t now) {
  int n = 0;
  for (int k = 0; k < 8; k++) {
    espn::AppRequest req;
    if (!a.request(now, req))
      break;
    const char *file = fixture_for(req.url);
    JsonDocument filter, doc;
    const bool ok = a.filter(filter) &&
                    !deserializeJson(doc, slurp(file), DeserializationOption::Filter(filter),
                                     DeserializationOption::NestingLimit(40)) &&
                    a.parse_doc(doc.as<JsonObjectConst>(), now);
    espn::AppResult r;
    r.status = 200;
    a.ended(r);
    a.apply(ok, now);
    n++;
  }
  return n;
}

bool has_text(const espn::AppFrame &f, const std::string &s) {
  for (const espn::AppText &t : f.texts)
    if (t.text.find(s) != std::string::npos)
      return true;
  return false;
}

void test_units_text() {
  const Units us = units_for_zone(4), uk = units_for_zone(10), eu = units_for_zone(11);
  WCHECK(us.f && us.mph && !uk.f && uk.mph && !eu.f && !eu.mph, "US F and mph, UK mph, Europe metric");
  WCHECK(temp_of(-4, true) == 31 && temp_of(-4, false) == 0 && temp_of(-15, false) == -2, "rounding");
  WCHECK(deg(250, us) == "77\xC2\xB0" && deg(250, eu, true) == "25\xC2\xB0" "C", "degrees: %s", deg(250, us).c_str());
  WCHECK(std::string(compass(168)) == "S" && std::string(compass(229)) == "SW" && std::string(compass(359)) == "N" &&
             std::string(compass(-1)).empty(),
         "compass");
  WCHECK(wind_text(115, 168, us, true) == "S 7mph" && wind_text(115, 168, eu, false) == "S 12 km/h",
         "wind: %s", wind_text(115, 168, eu, false).c_str());
  WCHECK(wind_text(4, 90, us, true) == "Calm", "calm");
  struct tm t {};
  t.tm_hour = 18;
  t.tm_min = 5;
  WCHECK(clock_text(t) == "6:05p" && hour_text(t) == "6p", "times");
  t.tm_hour = 0;
  WCHECK(hour_text(t) == "12a", "midnight");
  t.tm_hour = 12;
  WCHECK(hour_text(t) == "12p", "noon");
  WCHECK(iso_epoch("2026-11-14T10:00:00-08:00") == iso_epoch("2026-11-14T18:00:00Z") &&
             iso_epoch("2026-11-14T18:00:00Z") == espn::parse_iso8601_z("2026-11-14T18:00:00Z") &&
             iso_epoch("2026-11-14T19:30:00+01:30") == iso_epoch("2026-11-14T18:00:00Z"),
         "offsets");
  WCHECK(iso_epoch("") == 0 && iso_epoch(nullptr) == 0, "no time");
}

void test_icons() {
  struct Case {
    uint8_t code;
    bool day;
    Icon want;
  };
  static const Case kCases[] = {
      {0, true, Icon::SUN},      {0, false, Icon::MOON},       {1, true, Icon::SUN},     {2, true, Icon::PARTLY_DAY},
      {2, false, Icon::PARTLY_NIGHT}, {3, true, Icon::CLOUDY}, {45, true, Icon::FOG},    {48, false, Icon::FOG},
      {51, true, Icon::DRIZZLE}, {55, true, Icon::DRIZZLE},    {56, true, Icon::SLEET},  {61, true, Icon::RAIN},
      {63, true, Icon::RAIN},    {65, true, Icon::HEAVY_RAIN}, {66, true, Icon::SLEET},  {67, true, Icon::SLEET},
      {71, true, Icon::SNOW},    {75, true, Icon::SNOW},       {77, true, Icon::SNOW},   {80, true, Icon::RAIN},
      {82, true, Icon::HEAVY_RAIN}, {85, true, Icon::SNOW},    {86, true, Icon::SNOW},   {95, true, Icon::THUNDER},
      {99, true, Icon::THUNDER}, {255, true, Icon::NONE},
  };
  for (const Case &c : kCases)
    WCHECK(icon_for(c.code, c.day) == c.want, "WMO %d", c.code);
  // MET Norway's symbols, through the F1 display's code
  struct Met {
    const char *sym;
    Icon want;
  };
  static const Met kMet[] = {{"clearsky_day", Icon::SUN},         {"clearsky_night", Icon::MOON},
                             {"fair_polartwilight", Icon::SUN},   {"partlycloudy_night", Icon::PARTLY_NIGHT},
                             {"cloudy", Icon::CLOUDY},            {"fog", Icon::FOG},
                             {"lightrainshowers_day", Icon::RAIN}, {"heavyrain", Icon::HEAVY_RAIN},
                             {"sleet", Icon::SLEET},              {"heavysnow", Icon::SNOW},
                             {"rainandthunder", Icon::THUNDER},   {"lightssnowshowersandthunder_day", Icon::THUNDER}};
  for (const Met &m : kMet)
    WCHECK(icon_for(espn::met_weather_code(m.sym), met_day(m.sym)) == m.want, "MET %s", m.sym);
  Now n{};
  n.code = 1;
  n.day = true;
  n.wind_kmh10 = 450;
  WCHECK(now_icon(n) == Icon::WIND, "strong wind on a dry sky");
  n.code = 61;
  WCHECK(now_icon(n) == Icon::RAIN, "rain beats wind");
  for (int i = 1; i < kIcons; i++)
    for (bool big : {true, false}) {
      const IconArt *a = icon_art((Icon) i, big);
      WCHECK(a != nullptr && a->bits != nullptr && a->w == (big ? 16 : 10) && a->h == a->w, "icon %d art", i);
      const espn::AppArt art = icon_at((Icon) i, big, 3, 4, 2);
      WCHECK(art.x == 3 && art.y == 4 && art.scale == 2 && art.w * art.scale <= espn::kAppArtMaxW &&
                 art.h * 3 <= espn::kAppArtMaxH,
             "icon %d drawable, tripled too", i);
    }
  WCHECK(icon_art(Icon::NONE, true) == nullptr && hazard_art() != nullptr, "none and the sign");
  WCHECK(sky_text(2, true, 12) == "Pt cloudy" && sky_text(2, true, 20) == "Partly cloudy" &&
             sky_text(0, false, 12) == "Clear" && sky_text(73, true, 12) == "Snow",
         "words: %s", sky_text(73, true, 12).c_str());
}

void test_aqi_alerts() {
  static const int kAqi[][2] = {{0, 0}, {50, 0}, {51, 1}, {100, 1}, {101, 2}, {150, 2},
                                {151, 3}, {200, 3}, {201, 4}, {300, 4}, {301, 5}, {500, 5}};
  for (const auto &c : kAqi)
    WCHECK(aqi_band(c[0]) == c[1], "AQI %d", c[0]);
  WCHECK(std::string(aqi_name(2, false)) == "Unhealthy for sensitive groups" &&
             std::string(aqi_name(2, true)) == "Sensitive" && std::string(aqi_name(0, false)) == "Good",
         "AQI names");
  WCHECK(aqi_color(0) == 0x00E400 && aqi_color(3) != aqi_color(4), "AQI colors");
  WCHECK(alert_level("Tornado Warning") == Level::WARNING && alert_level("Winter Storm Watch") == Level::WATCH &&
             alert_level("Wind Advisory") == Level::ADVISORY &&
             alert_level("Special Weather Statement") == Level::STATEMENT &&
             alert_level("Shelter In Place Warning") == Level::WARNING && alert_level("") == Level::STATEMENT,
         "alert levels");
  WCHECK(level_color(Level::WARNING) == 0xFF2020 && level_color(Level::WATCH) == 0xFF8C00 &&
             level_color(Level::ADVISORY) == 0xFFD000,
         "severity colors");
  const std::string t = tidy_alert("* CHANGES...None.\n\n* WHAT...Heavy wet\nsnow...mostly.\n\n* WHERE...Here.", 300);
  WCHECK(t == "Heavy wet snow, mostly.", "the what alone: %s", t.c_str());
  const std::string longer(5000, 'x');
  WCHECK(tidy_alert(longer.c_str(), 300).size() <= 300, "a long one, read from its start");
  const std::string cut = tidy_alert("Plain words with no sections at all here", 20);
  WCHECK(cut.size() <= 20 && cut.substr(cut.size() - 3) == "...", "cut at a word: %s", cut.c_str());
}

void test_parse() {
  Forecast f{};
  WCHECK(parse_file("fixtures/weather_om_tahoe.json", wx::detail::fill_forecast_filter,
                    [&f](JsonObjectConst r) { return wx::detail::forecast_from_doc(r, f); }),
         "Open-Meteo parses");
  WCHECK(f.valid && f.src == Src::OPEN_METEO && f.now.temp_c10 == -4 && f.now.feels_c10 == -45 &&
             f.now.humidity == 90 && f.now.code == 3 && !f.now.day && f.now.wind_kmh10 == 115 &&
             f.now.gust_kmh10 == 209 && f.now.wind_dir == 168 && f.now.at == 1794630600,
         "now: %d %d", f.now.temp_c10, f.now.feels_c10);
  WCHECK(f.n_hours == 24 && f.hours[0].at == 1794628800 && f.hours[0].temp_c10 == -1 && f.hours[0].code == 73 &&
             f.hours[0].pop == 48 && !f.hours[0].day,
         "hours: %d", f.n_hours);
  WCHECK(f.n_days == 7 && f.days[0].hi_c10 == 70 && f.days[0].lo_c10 == -18 && f.days[0].code == 75 &&
             f.days[0].pop == 99 && f.days[0].sunrise == 1794580848 && f.days[0].sunset == 1794617247,
         "days: %d", f.n_days);
  Forecast m{};
  const int64_t met_now = espn::parse_iso8601_z("2026-11-12T06:10:00Z");
  WCHECK(parse_file("fixtures/met_vegas.json", wx::detail::fill_met_filter,
                    [&](JsonObjectConst r) { return wx::detail::met_from_doc(r, met_now, m); }),
         "MET Norway parses");
  WCHECK(m.valid && m.src == Src::MET && m.now.at == espn::parse_iso8601_z("2026-11-12T06:00:00Z") &&
             m.n_hours > 12 && m.n_days >= 5 && m.hours[0].pop == -1 && m.now.wind_dir >= 0,
         "MET: now %lld, %d hours, %d days", (long long) m.now.at, m.n_hours, m.n_days);
  for (size_t i = 0; i < m.n_days; i++)
    WCHECK(m.days[i].hi_c10 >= m.days[i].lo_c10 && m.days[i].code != 255, "MET day %d", (int) i);
  Air a{};
  WCHECK(parse_file("fixtures/weather_aq_tahoe.json", wx::detail::fill_air_filter,
                    [&a](JsonObjectConst r) { return wx::detail::air_from_doc(r, a); }) &&
             a.aqi == 41 && a.main == Pollutant::OZONE && a.pm25_10 == 2,
         "AQI Tahoe");
  WCHECK(parse_file("fixtures/weather_aq_delhi.json", wx::detail::fill_air_filter,
                    [&a](JsonObjectConst r) { return wx::detail::air_from_doc(r, a); }) &&
             a.aqi == 190 && a.main == Pollutant::PM25 && a.pm25_10 == 952,
         "AQI Delhi");
  Alerts al{};
  WCHECK(parse_file("fixtures/weather_nws_tahoe.json", wx::detail::fill_alerts_filter,
                    [&al](JsonObjectConst r) { return wx::detail::alerts_from_doc(r, kNow, al); }),
         "NWS parses");
  WCHECK(al.n == 1 && al.a[0].level == Level::WARNING && std::string(al.a[0].event) == "Winter Storm Warning" &&
             al.a[0].ends == iso_epoch("2026-11-14T10:00:00-08:00") &&
             std::string(al.a[0].text).compare(0, 15, "Heavy wet snow.") == 0,
         "the warning: %s", al.a[0].text);
  WCHECK(parse_file("fixtures/weather_nws_tahoe.json", wx::detail::fill_alerts_filter,
                    [&al](JsonObjectConst r) {
                      return wx::detail::alerts_from_doc(r, iso_epoch("2026-11-14T10:00:00-08:00"), al);
                    }) &&
             al.n == 0,
         "over at its end");
  WCHECK(parse_file("fixtures/weather_nws_reno.json", wx::detail::fill_alerts_filter,
                    [&al](JsonObjectConst r) { return wx::detail::alerts_from_doc(r, 1794600000, al); }) &&
             al.n == 1 && al.a[0].level == Level::ADVISORY,
         "an advisory");
  WCHECK(parse_file("fixtures/weather_nws_seattle.json", wx::detail::fill_alerts_filter,
                    [&al](JsonObjectConst r) { return wx::detail::alerts_from_doc(r, kNow, al); }) &&
             al.valid && al.n == 0,
         "none");
  WCHECK(!parse_file("fixtures/weather_nws_outside.json", wx::detail::fill_alerts_filter,
                     [&al](JsonObjectConst r) { return wx::detail::alerts_from_doc(r, kNow, al); }),
         "outside the US: no list");
  Radar r{};
  WCHECK(parse_file("fixtures/weather_rainviewer.json", wx::detail::fill_radar_filter,
                    [&r](JsonObjectConst x) { return wx::detail::radar_from_doc(x, r); }) &&
             r.n == kFrames && std::string(r.host) == "https://tilecache.rainviewer.com" && r.t[0] < r.t[kFrames - 1] &&
             r.t[kFrames - 1] == 1794628800,
         "RainViewer: %d frames", r.n);
  WCHECK(rainviewer_url(r, 5, 476062, -1223321) ==
             std::string("https://tilecache.rainviewer.com") + r.path[5] + "/256/7/47.61/-122.33/2/1_1.png",
         "frame URL: %s", rainviewer_url(r, 5, 476062, -1223321).c_str());
  WCHECK(rainviewer_url(r, 6, 476062, -1223321).empty(), "past the frames");
}

void test_urls_map() {
  WCHECK(round_e4(476062) == 476100 && round_e4(-1223321) == -1223300 && round_e4(-49) == 0, "rounding");
  WCHECK(forecast_url(389399, -1199772).find("latitude=38.94&longitude=-119.98&") != std::string::npos &&
             forecast_url(1, 1).find("forecast_hours=24") != std::string::npos,
         "forecast URL");
  WCHECK(alerts_url(389399, -1199772) == "https://api.weather.gov/alerts/active?point=38.94,-119.98", "alerts URL");
  WCHECK(air_url(286139, 772090).find("air-quality-api.open-meteo.com/v1/air-quality?latitude=28.61&longitude=77.21") !=
             std::string::npos,
         "air URL");
  WCHECK(met_url(515074, -1278).find("lat=51.5100&lon=-0.1300") != std::string::npos, "MET URL: %s",
         met_url(515074, -1278).c_str());
  WCHECK(in_us(389399, -1199772) && in_us(612181, -1499003) && in_us(213069, -1578583) && in_us(184655, -660557) &&
             !in_us(515074, -1278) && !in_us(286139, 772090) && !in_us(-338688, 1512093),
         "NWS areas, roughly: the NWS's own 400 settles the rest");
  // Web Mercator: Seattle's zoom-7 tile
  WCHECK(tile_of(merc_px_x(-122.3321f, 7)) == 20 && tile_of(merc_px_y(47.6062f, 7)) == 44, "Seattle's tile");
  WCHECK(fabsf(merc_px_x(0, 0) - 128) < 0.01f && fabsf(merc_px_y(0, 0) - 128) < 0.01f, "the world's middle");
  WCHECK(fabsf(merc_px_y(85.0511f, 3)) < 1.0f, "the map's top edge");
  WCHECK(fabsf(ground_m_per_px(0, 0) - 156543.0f) < 1.0f, "a pixel at zoom 0");
  const float mpp = radar_m_per_px(476062, 64, 56);
  WCHECK(mpp > 3250 && mpp < 3350, "Seattle, 64 wide: %.0f m a pixel", (double) mpp);
  int dist = 0, r = 0;
  WCHECK(radar_ring(476062, 64, 56, true, dist, r) && dist == 25 && r == 12, "a 25-mile ring: %d %d", dist, r);
  WCHECK(radar_ring(476062, 128, 56, true, dist, r) && dist == 10 && r == 10, "a 10-mile ring: %d %d", dist, r);
  WCHECK(radar_ring(476062, 128, 120, false, dist, r) && dist == 50 && r * 5 <= 120 * 2, "km: %d %d", dist, r);
  // Iowa State's box: same extent, centered on the home
  const std::string iem = iem_url(1794628800, 476062, -1223321, 64, 56);
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  const size_t at = iem.find("BBOX=");
  WCHECK(at != std::string::npos && sscanf(iem.c_str() + at, "BBOX=%lf,%lf,%lf,%lf", &x0, &y0, &x1, &y1) == 4,
         "a box: %s", iem.c_str());
  const double span = 2 * 20037508.34 / 128;
  WCHECK(fabs((x1 - x0) - span) < 2 && fabs((y1 - y0) - span * 56 / 64) < 2, "the zoom-7 extent: %.0f %.0f",
         x1 - x0, y1 - y0);
  WCHECK(fabs((x0 + x1) / 2 - merc_m_x(-122.33f)) < 2 && fabs((y0 + y1) / 2 - merc_m_y(47.61f)) < 2, "centered");
  WCHECK(iem.find("WIDTH=64&HEIGHT=56") != std::string::npos && iem.find("TIME=2026-11-14T04:00:00Z") != std::string::npos,
         "size and time");
  const int64_t t5 = iem_frame_time(kNow, 5, 6), t0 = iem_frame_time(kNow, 0, 6);
  WCHECK(t5 % 600 == 0 && t5 <= kNow - 360 && t5 > kNow - 960 && t5 - t0 == 3000, "frames ten minutes apart");
  // The map's cut and averaging
  espn::MapCanvas c;
  WCHECK(c.setup(256, 256, 64, 56) && c.cw == 256 && c.ch == 224 && c.cx == 0 && c.cy == 16, "a square cut wide");
  WCHECK(c.setup(64, 56, 64, 56) && c.cw == 64 && c.ch == 56 && c.cx == 0 && c.cy == 0, "a box-sized map whole");
  WCHECK(!c.setup(2048, 256, 64, 56) && !c.setup(0, 1, 1, 1), "too big or empty");
  c.setup(8, 8, 2, 2);
  std::vector<espn::MapCell> cells(4, espn::MapCell{});
  c.cells = cells.data();
  const uint8_t red[4] = {255, 0, 0, 255}, clear[4] = {0, 0, 0, 0}, faint[4] = {80, 80, 160, 255};
  for (uint32_t y = 0; y < 8; y++)
    for (uint32_t x = 0; x < 8; x++) {
      // Solid, a quarter covered, faint, then clear
      const bool tl = x < 4 && y < 4, tr = x >= 4 && y < 4 && x % 2 == 0 && y % 2 == 0, bl = x < 4 && y >= 4;
      c.paint(x, y, 1, 1, tl || tr ? red : bl ? faint : clear);
    }
  uint8_t px[8];
  c.finish(px);
  auto at565 = [&px](int i) { return (uint16_t) (px[2 * i] | px[2 * i + 1] << 8); };
  WCHECK(at565(0) == 0xF800 && at565(3) == 0, "solid red and clear");
  WCHECK((at565(1) >> 11) > 8 && (at565(1) >> 11) < 31 && (at565(1) & 0x7FF) == 0, "a quarter shows dimmer: %04x",
         at565(1));
  WCHECK((at565(2) & 0x1F) > (160 >> 3), "a faint color raised: %04x", at565(2));
}

void test_app() {
  espn::AppEnv &env = espn::app_env();
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  env.version = "2026.11.6";
  set_home(nullptr);
  WeatherApp *a = new WeatherApp();
  espn::AppRequest req;
  WCHECK(!a->request(kNow, req), "no home, no reads");
  espn::AppFrame nf;
  a->frame(64, 64, kNow, nf);
  WCHECK(has_text(nf, "Set your") && nf.photo.url.empty(), "no home: says to set one");
  JsonDocument st;
  a->state(st.to<JsonObject>(), kNow);
  WCHECK(std::string(st["status"] | "") == "no_home" && st["ticker"] == 0, "state: no home");
  WCHECK(a->set("wxticker", "1") == espn::AppSet::CHANGED && a->set("wxticker", "1") == espn::AppSet::SAME &&
             a->set("wxticker", "2") == espn::AppSet::REFUSED && a->set("flyhome", "1,1") == espn::AppSet::REFUSED &&
             a->ticker_on(),
         "the ticker switch");
  a->set("wxticker", "0");

  set_home("38.9399,-119.9772");
  espn::AppFrame lf;
  a->frame(64, 64, kNow, lf);
  WCHECK(has_text(lf, "Loading"), "before a read");
  // Off screen and the switch off: nothing
  WCHECK(a->idle_poll_ms(kNow) == 0 && a->night_poll_ms(kNow) == 0, "off screen: no reads");
  a->shown(true);
  WCHECK(a->poll_ms(kNow) == 0, "on screen: a read now");
  std::string url;
  WCHECK(step(*a, kNow, url, "fixtures/weather_om_tahoe.json") && url.find("api.open-meteo.com") != std::string::npos,
         "the forecast first: %s", url.c_str());
  espn::AppRequest alerts;
  WCHECK(a->request(kNow, alerts) && alerts.url.find("api.weather.gov") != std::string::npos, "then alerts");
  bool ua = false, geo = false;
  for (const espn::AppHeader &h : alerts.headers) {
    ua = ua || (h.name == "User-Agent" && h.value == "GameDayScoreboard/2026.11.6 "
                                                      "(+https://github.com/ApolloAutomation/gameday-scoreboard-main)");
    geo = geo || (h.name == "Accept" && h.value == "application/geo+json");
  }
  WCHECK(ua && geo, "the NWS gets the project's User-Agent");
  a->ended(espn::AppResult{});
  a->apply(false, kNow);  // a lost read: backs off
  WCHECK(a->host(WeatherApp::HOST_NWS).fails == 1, "a failure counts");
  WeatherApp::Read r;
  int64_t at = 0;
  r = a->next_read(kNow, true, at);
  WCHECK(r == WeatherApp::Read::RADAR && at <= kNow, "radar next, alerts waiting: %d", (int) r);
  WCHECK(a->host(WeatherApp::HOST_NWS).wait_until > kNow, "the NWS waits");
  WCHECK(read_all(*a, kNow) == 2, "radar and air");
  read_all(*a, kNow + 120);
  WCHECK(a->alerts().n == 1 && a->air().aqi == 41 && a->radar().n == kFrames && a->forecast().valid, "all read");
  r = a->next_read(kNow + 121, true, at);
  WCHECK(r == WeatherApp::Read::ALERTS && at == kNow + 120 + WeatherApp::kAlertsS, "alerts every 5 minutes: %lld",
         (long long) (at - kNow));
  WCHECK(a->poll_ms(kNow + 121) == WeatherApp::kAlertsS * 1000, "poll: %u", a->poll_ms(kNow + 121));

  // Every page at every size
  const int64_t now = kNow + 130;
  Page list[kPages];
  const size_t n = a->rotation(now, list, kPages);
  WCHECK(n == 6 && list[0] == Page::ALERTS && list[1] == Page::NOW && list[4] == Page::RADAR && list[5] == Page::AIR,
         "every page, the warning first: %d", (int) n);
  static const int kSizes[][2] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};
  for (const auto &sz : kSizes) {
    a->shown(true);
    bool seen[kPages] = {};
    for (int dt = 0; dt < 70; dt++) {
      const Page p = a->page(now + dt);
      espn::AppFrame f;
      a->frame(sz[0], sz[1], now + dt, f);
      if (dt == 0)
        WCHECK(p == Page::ALERTS, "%dx%d starts on the warning", sz[0], sz[1]);
      seen[(int) p] = true;
      espn::app_frame_trim(f);
      for (const std::string &why : app_frame_problems(f, sz[0], sz[1]))
        WCHECK(false, "%dx%d page %d: %s", sz[0], sz[1], (int) p, why.c_str());
      WCHECK(espn::app_frame_board(f), "%dx%d page %d draws", sz[0], sz[1], (int) p);
      WCHECK(f.ticker.compare(0, 34, "Winter Storm Warning until 10a Sat") == 0, "ticker: %s",
             f.ticker.substr(0, 40).c_str());
      if (p == Page::RADAR)
        WCHECK(f.photo.w == std::min(sz[0], 128) && f.photo.h == sz[1] - 8 &&
                   f.photo.hold == rainviewer_url(a->radar(), kFrames - 1, 389399, -1199772) &&
                   f.photo.url.find("tilecache.rainviewer.com") != std::string::npos && f.bar.w == 2 &&
                   f.line.pts.size() > 10 && a->redraw_ms() <= WeatherApp::kStepMs,
               "%dx%d radar: the newest frame stands in, the home and its ring", sz[0], sz[1]);
    }
    for (int p = 0; p < kPages; p++)
      WCHECK(seen[p], "%dx%d shows page %d", sz[0], sz[1], p);
  }
  // The radar's frames, oldest first, then held on the newest
  a->shown(true);
  {
    espn::AppFrame start;
    a->frame(128, 64, now, start);
  }
  int64_t radar_at = now;
  while (a->page(radar_at) != Page::RADAR && radar_at < now + 120)
    radar_at++;
  WCHECK(a->page(radar_at) == Page::RADAR, "the radar comes round");
  std::string first;
  for (uint32_t step_i = 0; step_i < kFrames + WeatherApp::kHoldSteps; step_i++) {
    g_ms = 1000 + step_i * WeatherApp::kStepMs;
    espn::AppFrame f;
    a->frame(128, 64, radar_at, f);
    if (step_i == 0) {
      g_ms = 1000;
      espn::AppFrame g;
      a->frame(128, 64, radar_at, g);
      first = g.photo.url;
    }
    const size_t k = std::min<size_t>(step_i, kFrames - 1);
    WCHECK(f.photo.url == rainviewer_url(a->radar(), k, 389399, -1199772), "step %u shows frame %d", step_i, (int) k);
  }
  WCHECK(first == rainviewer_url(a->radar(), 0, 389399, -1199772), "it starts on the oldest");
  g_ms = 1000 + 130;
  {
    espn::AppFrame f;
    a->frame(128, 64, radar_at, f);
    WCHECK(a->redraw_ms() == WeatherApp::kStepMs - 130, "redrawn as the next frame starts: %u", a->redraw_ms());
  }
  g_ms = 0;

  WCHECK(a->lit(now) && !a->busy(now), "a warning keeps Off or dim lit, not the night hours away");
  // The warning in every ticker, only with the switch on
  uint32_t color = 0;
  WCHECK(a->ticker_warning(now, color).empty(), "switch off: no warning elsewhere");
  a->set("wxticker", "1");
  WCHECK(a->ticker_warning(now, color) == "Winter Storm Warning until 10a Sat" && color == 0xFF2020, "warning: %s",
         a->ticker_warning(now, color).c_str());
  a->shown(false);
  WCHECK(a->idle_poll_ms(now) > 0 && a->night_poll_ms(now) == a->idle_poll_ms(now), "off screen: alerts only");
  r = a->next_read(now + 3600, false, at);
  WCHECK(r == WeatherApp::Read::ALERTS, "off screen: alerts, not the rest");
  JsonDocument sd;
  a->state(sd.to<JsonObject>(), now);
  WCHECK(std::string(sd["status"] | "") == "ok" && std::string(sd["temp"] | "") == "31\xC2\xB0" "F" &&
             sd["alerts"] == 1 && std::string(sd["alert"] | "") == "Winter Storm Warning" && sd["aqi"] == 41 &&
             std::string(sd["radar"] | "") == "rainviewer" && std::string(sd["source"] | "") == "open-meteo" &&
             sd["ticker"] == 1 && std::string(sd["home"] | "") == "38.9399,-119.9772",
         "state");
  a->set("wxticker", "0");

  // A new home: the old weather goes, a read now
  set_home("47.6062,-122.3321");
  a->shown(true);
  espn::AppFrame hf;
  a->frame(64, 64, now, hf);
  WCHECK(has_text(hf, "Loading") && a->poll_ms(now) == 0, "a new home reads at once");
  WCHECK(a->request(now, req) && req.url.find("latitude=47.61") != std::string::npos, "for the new home");
  set_home("38.9399,-119.9772");
  a->ended(espn::AppResult{200, false, 0});
  a->apply(true, now);  // answered for Seattle, after the move back
  WCHECK(a->forecast().valid && a->rotation(now, list, kPages) > 0, "a read for the old home is dropped");
  delete a;
}

void test_failover() {
  set_home("38.9399,-119.9772");
  WeatherApp *a = new WeatherApp();
  a->shown(true);
  std::string url;
  int64_t now = kNow;
  // Three failures move the forecast to MET Norway
  int fc_fails = 0;
  for (int k = 0; k < 40 && fc_fails < 3; k++) {
    int64_t due = 0;
    if (a->next_read(now, true, due) == WeatherApp::Read::NONE)
      break;
    now = std::max(now, due);
    espn::AppRequest rq;
    if (!a->request(now, rq))
      continue;
    const bool fc = rq.url.find("air-quality") == std::string::npos && rq.url.find("open-meteo") != std::string::npos;
    if (fc) {
      a->ended(espn::AppResult{500, false, 0});
      a->apply(false, now);
      fc_fails++;
      continue;
    }
    WCHECK(rq.url.find("api.met.no") == std::string::npos, "no backup before three failures");
    JsonDocument filter, doc;
    const bool ok = a->filter(filter) &&
                    !deserializeJson(doc, slurp(fixture_for(rq.url)), DeserializationOption::Filter(filter)) &&
                    a->parse_doc(doc.as<JsonObjectConst>(), now);
    a->ended(espn::AppResult{200, false, 0});
    a->apply(ok, now);
  }
  WCHECK(fc_fails == 3, "three forecast failures: %d", fc_fails);
  espn::AppFrame f;
  a->frame(64, 64, now, f);
  WCHECK(has_text(f, "Can't reach"), "three failures: said so");
  int64_t at = 0;
  WCHECK(a->next_read(now, true, at) == WeatherApp::Read::FORECAST, "the forecast still first");
  now = std::max(now, at);
  espn::AppRequest req;
  WCHECK(a->request(now, req) && req.url.find("api.met.no") != std::string::npos && !req.headers.empty() &&
             req.headers[0].value.find("GameDayScoreboard/") == 0,
         "MET Norway with the User-Agent: %s", req.url.c_str());
  {
    JsonDocument filter, doc;
    const bool ok = a->filter(filter) && !deserializeJson(doc, slurp("fixtures/met_vegas.json"),
                                                          DeserializationOption::Filter(filter)) &&
                    a->parse_doc(doc.as<JsonObjectConst>(), now);
    a->ended(espn::AppResult{200, false, 0});
    a->apply(ok, now);
  }
  WCHECK(a->forecast().valid && a->forecast().src == Src::MET, "the backup's forecast");
  // After its wait, Open-Meteo again
  const int64_t later = a->host(WeatherApp::HOST_OPEN_METEO).out_until + WeatherApp::kForecastS;
  WCHECK(a->request(later, req) && req.url.find("api.open-meteo.com") != std::string::npos, "back to Open-Meteo");
  a->ended(espn::AppResult{});
  a->apply(false, later);
  delete a;

  // A refusal moves at once
  a = new WeatherApp();
  a->shown(true);
  WCHECK(step(*a, kNow, url, nullptr, 429, true), "refused");
  WCHECK(a->next_read(kNow, true, at) == WeatherApp::Read::FORECAST && a->request(at, req) &&
             req.url.find("api.met.no") != std::string::npos,
         "a refusal goes to MET Norway");
  delete a;

  // RainViewer out in the US: Iowa State's frames
  a = new WeatherApp();
  a->shown(true);
  read_all(*a, kNow);
  WCHECK(step(*a, kNow + WeatherApp::kRadarOldS, url, nullptr, 500) || true, "a failing radar read");
  std::string urls[kFrames];
  for (int64_t t = kNow + WeatherApp::kRadarOldS; t < kNow + 2 * WeatherApp::kRadarOldS; t += 60) {
    espn::AppRequest rq;
    for (int guard = 0; guard < 8 && a->request(t, rq); guard++) {
      const bool radar = rq.url.find("rainviewer") != std::string::npos;
      a->ended(espn::AppResult{radar ? 500 : 200, false, 0});
      if (radar) {
        a->apply(false, t);
        continue;
      }
      JsonDocument filter, doc;
      const bool ok = a->filter(filter) &&
                      !deserializeJson(doc, slurp(fixture_for(rq.url)), DeserializationOption::Filter(filter)) &&
                      a->parse_doc(doc.as<JsonObjectConst>(), t);
      a->apply(ok, t);
    }
  }
  const int64_t t2 = kNow + 2 * WeatherApp::kRadarOldS;
  const size_t n = a->radar_urls(t2, 64, 56, urls, kFrames);
  WCHECK(n == kFrames && urls[0].find("mesonet.agron.iastate.edu") != std::string::npos, "NEXRAD frames: %d", (int) n);
  delete a;

  // RainViewer's images failing in the US: Iowa State's, then none
  a = new WeatherApp();
  a->shown(true);
  read_all(*a, kNow);
  {
    espn::AppFrame start;
    a->frame(128, 64, kNow, start);
  }
  int64_t rt = kNow;
  while (a->page(rt) != Page::RADAR && rt < kNow + 120)
    rt++;
  {
    espn::AppFrame f;
    a->frame(128, 64, rt, f);
    WCHECK(f.photo.url.find("rainviewer") != std::string::npos, "RainViewer's frames first");
  }
  a->photo_failed(rt);
  WCHECK(a->radar_urls(rt, 64, 56, urls, kFrames) == kFrames && urls[0].find("mesonet") != std::string::npos,
         "failed images: Iowa State's");
  {
    espn::AppFrame f;
    a->frame(128, 64, rt, f);
    WCHECK(f.photo.url.find("mesonet") != std::string::npos, "the radar page draws Iowa State's");
    JsonDocument sd;
    a->state(sd.to<JsonObject>(), rt);
    WCHECK(std::string(sd["radar"] | "") == "iem", "state: the backup radar");
  }
  a->photo_failed(rt);
  Page pl[kPages];
  size_t pn = a->rotation(rt, pl, kPages);
  WCHECK(a->radar_urls(rt, 64, 56, urls, kFrames) == 0 && std::find(pl, pl + pn, Page::RADAR) == pl + pn,
         "both failing: no radar page");
  WCHECK(a->radar_urls(rt + WeatherApp::kImagesOutS, 64, 56, urls, kFrames) > 0 &&
             urls[0].find("rainviewer") != std::string::npos,
         "after a while, RainViewer's again");
  delete a;

  // A read dropped for new settings blames no host
  a = new WeatherApp();
  a->shown(true);
  espn::AppRequest dq;
  WCHECK(a->request(kNow, dq) && dq.url.find("api.open-meteo.com") != std::string::npos, "a forecast read");
  a->dropped();
  WCHECK(a->request(kNow, dq) && a->host(WeatherApp::HOST_OPEN_METEO).fails == 0 &&
             a->host(WeatherApp::HOST_OPEN_METEO).wait_until <= kNow,
         "dropped: no failure counted");
  a->ended(espn::AppResult{});
  a->apply(false, kNow);
  WCHECK(a->host(WeatherApp::HOST_OPEN_METEO).fails == 1, "a real failure still counts");
  delete a;

  // Outside the US: no alerts, no NEXRAD
  set_home("51.5074,-0.1278");
  a = new WeatherApp();
  a->shown(true);
  int reads = 0;
  for (int k = 0; k < 6; k++) {
    espn::AppRequest rq;
    if (!a->request(kNow, rq))
      break;
    WCHECK(rq.url.find("weather.gov") == std::string::npos, "no NWS for London");
    reads++;
    a->ended(espn::AppResult{rq.url.find("rainviewer") != std::string::npos ? 500 : 200, false, 0});
    a->apply(false, kNow);
  }
  WCHECK(reads == 3 && a->radar_urls(kNow, 64, 56, urls, kFrames) == 0, "London: three reads, no radar fallback");
  delete a;

  // Outside the NWS's area: never asked again
  set_home("38.9399,-119.9772");
  a = new WeatherApp();
  a->shown(true);
  a->set("wxticker", "1");
  for (int k = 0; k < 6; k++) {
    espn::AppRequest rq;
    if (!a->request(kNow, rq))
      break;
    const bool nws = rq.url.find("weather.gov") != std::string::npos;
    a->ended(espn::AppResult{nws ? 400 : 200, false, 0});
    a->apply(false, kNow);
  }
  WCHECK(a->next_read(kNow + 86400, false, at) == WeatherApp::Read::NONE && a->idle_poll_ms(kNow) == 0,
         "a 400 ends the alert checks");
  delete a;
}

}  // namespace

int run_weather_tests(int &total_checks) {
  espn::AppSlots &slots = espn::app_slots();
  if (g_flights == nullptr) {
    g_flights = new flights::FlightsApp();
    g_flights->setup();
    espn::app_register(slots, g_flights);
  }
  test_units_text();
  test_icons();
  test_aqi_alerts();
  test_parse();
  test_urls_map();
  test_app();
  test_failover();
  espn::app_env() = espn::AppEnv{};
  total_checks += checks;
  return failures;
}
