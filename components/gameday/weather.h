// Weather data core: sources, icons, units and map math; pure.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>

#include "apps.h"

namespace wx {

// ---- Units ---------------------------------------------------------------------

// US zones Fahrenheit; US and UK mph.
struct Units {
  bool f{false};
  bool mph{false};
};
Units units_for_zone(int tz);

// Whole degrees in the unit; c10 is tenths Celsius.
int temp_of(int16_t c10, bool f);
// "72°", or "72°F" with the unit.
std::string deg(int16_t c10, const Units &u, bool unit = false);
int speed_of(int16_t kmh10, bool mph);
// "N", "NE", ...; "" for none.
const char *compass(int deg);
// "SW 9 mph", "Calm"; tight drops the space.
std::string wind_text(int16_t kmh10, int16_t dir, const Units &u, bool tight);
// Clock time, like 6:42a
std::string clock_text(const struct tm &t);
// The hour alone, like 3a or 12p
std::string hour_text(const struct tm &t);
const char *day_name(const struct tm &t);
// An ISO time with its offset; 0 for none.
int64_t iso_epoch(const char *s);

// ---- Data ----------------------------------------------------------------------

enum class Icon : uint8_t {
  NONE = 0,
  SUN,
  MOON,
  PARTLY_DAY,
  PARTLY_NIGHT,
  CLOUDY,
  FOG,
  DRIZZLE,
  RAIN,
  HEAVY_RAIN,
  THUNDER,
  SNOW,
  SLEET,
  WIND,
};
static const int kIcons = 14;

// All zero to start; the parsers set each -1.
struct Now {
  bool valid;
  int64_t at;
  int16_t temp_c10, feels_c10;
  int16_t wind_kmh10, gust_kmh10;
  int16_t wind_dir;  // degrees it blows from, -1 none
  int8_t humidity;   // -1 for none
  uint8_t code;      // WMO weather code
  bool day;
};

struct Hour {
  int64_t at;
  int16_t temp_c10;
  uint8_t code;
  int8_t pop;  // chance of rain or snow, -1 none
  bool day;
};

struct Day {
  int64_t at;  // that day's local midnight
  int16_t hi_c10, lo_c10;
  uint8_t code;
  int8_t pop;
  int64_t sunrise, sunset;
};

static const size_t kHours = 24;
static const size_t kDays = 7;

enum class Src : uint8_t { OPEN_METEO = 0, MET };

struct Forecast {
  bool valid;
  Now now;
  Hour hours[kHours];
  uint8_t n_hours;
  Day days[kDays];
  uint8_t n_days;
  Src src;
};

enum class Pollutant : uint8_t { NONE = 0, PM25, PM10, OZONE, NO2, SO2, CO };

struct Air {
  bool valid;
  int64_t at;
  int16_t aqi;
  int16_t pm25_10;  // tenths of a microgram per cubic meter, -1 none
  Pollutant main;
};

// Most urgent last; NWS names end in these words.
enum class Level : uint8_t { STATEMENT = 0, ADVISORY, WATCH, WARNING };

static const size_t kAlerts = 4;
static const size_t kAlertEvent = 40;
static const size_t kAlertText = 320;

struct Alert {
  Level level{Level::STATEMENT};
  int64_t ends{0};  // 0 not given
  char event[kAlertEvent]{};
  char text[kAlertText]{};  // the description, tidied and cut
};

struct Alerts {
  bool valid{false};
  uint8_t n{0};
  Alert a[kAlerts];
};

static const size_t kFrames = 6;
static const size_t kFramePath = 32;

struct Radar {
  bool valid{false};
  uint8_t n{0};
  int64_t t[kFrames]{};
  char path[kFrames][kFramePath]{};
  char host[40]{};
};

// ---- Sources --------------------------------------------------------------------

// Always metric; the text functions convert.
std::string forecast_url(int32_t lat_e4, int32_t lon_e4);
std::string air_url(int32_t lat_e4, int32_t lon_e4);
std::string alerts_url(int32_t lat_e4, int32_t lon_e4);
static const char *const kRadarIndexUrl = "https://api.rainviewer.com/public/weather-maps.json";
// MET Norway's compact forecast (app_f1.h's), the backup.
std::string met_url(int32_t lat_e4, int32_t lon_e4);
// The National Weather Service's own boxes, roughly.
bool in_us(int32_t lat_e4, int32_t lon_e4);

namespace detail {
void fill_forecast_filter(JsonDocument &f);
bool forecast_from_doc(JsonObjectConst root, Forecast &out);
void fill_met_filter(JsonDocument &f);
bool met_from_doc(JsonObjectConst root, int64_t now, Forecast &out);
void fill_air_filter(JsonDocument &f);
bool air_from_doc(JsonObjectConst root, Air &out);
void fill_alerts_filter(JsonDocument &f);
bool alerts_from_doc(JsonObjectConst root, int64_t now, Alerts &out);
void fill_radar_filter(JsonDocument &f);
bool radar_from_doc(JsonObjectConst root, Radar &out);
}

// Whole documents, for host tests.
template<typename TIn, typename Fill, typename From> bool parse_with(TIn &in, Fill fill, From from) {
  JsonDocument filter(espn::detail::json_allocator());
  fill(filter);
  JsonDocument doc(espn::detail::json_allocator());
  if (deserializeJson(doc, in, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20)))
    return false;
  return from(doc.as<JsonObjectConst>());
}

// ---- Icons, words, colors ----------------------------------------------------------

Icon icon_for(uint8_t code, bool day);
// Strong wind on a dry sky shows the wind.
Icon now_icon(const Now &n);
// MET symbols: "_night" is night; no suffix is day.
bool met_day(const char *symbol);

// "Rain", "Pt cloudy"; within max characters.
std::string sky_text(uint8_t code, bool day, size_t max);

// AQI bands: 0 Good to 5 Hazardous.
int aqi_band(int aqi);
const char *aqi_name(int band, bool tight);
uint32_t aqi_color(int band);
const char *pollutant_name(Pollutant p);

Level alert_level(const char *event);
uint32_t level_color(Level l);
// NWS wording to one line: its "* WHAT..." part alone.
std::string tidy_alert(const char *desc, size_t max);

// A 2-bit icon (weather_art.cpp); inks 1-3.
struct IconArt {
  uint8_t w, h;
  const uint8_t *bits;
  uint32_t ink[3];
};
// big: 16 square; else 10.
const IconArt *icon_art(Icon i, bool big);
// A 16-square warning sign; ink 1 the alert's color.
const IconArt *hazard_art();
espn::AppArt icon_at(Icon i, bool big, int x, int y, int scale = 1);

// ---- Radar ----------------------------------------------------------------------

// RainViewer's keyless images stop at zoom 7.
static const int kRadarZoom = 7;
static const int kTilePx = 256;

// Half the world's width in EPSG:3857 meters.
static const float kMercHalf = 20037508.34f;

// EPSG:3857 meters, as WMS boxes take them, in floats.
inline float merc_m_x(float lon) { return lon * kMercHalf / 180.0f; }
inline float merc_m_y(float lat) {
  const float s = sinf(lat * (float) M_PI / 180.0f);
  return 0.5f * logf((1.0f + s) / (1.0f - s)) * 6378137.0f;
}
// World pixels at zoom z, 256 a tile.
inline float merc_px_x(float lon, int z) { return (merc_m_x(lon) + kMercHalf) / (2.0f * kMercHalf) * kTilePx * (float) (1 << z); }
inline float merc_px_y(float lat, int z) { return (kMercHalf - merc_m_y(lat)) / (2.0f * kMercHalf) * kTilePx * (float) (1 << z); }
inline int tile_of(float px) { return (int) floorf(px / kTilePx); }
// Ground meters across one world pixel.
inline float ground_m_per_px(float lat, int z) {
  return 156543.034f * cosf(lat * (float) M_PI / 180.0f) / (float) (1 << z);
}

// The home rounded to 0.01 degrees for the map services.
int32_t round_e4(int32_t e4);
// Frame i, centered on the home, 256 px at kRadarZoom.
std::string rainviewer_url(const Radar &r, size_t i, int32_t lat_e4, int32_t lon_e4);
// The same extent from Iowa State's NEXRAD mosaic.
std::string iem_url(int64_t t, int32_t lat_e4, int32_t lon_e4, int w, int h);
// Its frames, 10 minutes apart, the newest last.
int64_t iem_frame_time(int64_t now, size_t i, size_t n);
// Ground meters per pixel of a w by h radar.
float radar_m_per_px(int32_t lat_e4, int w, int h);
// A round ring distance and radius; false for none.
bool radar_ring(int32_t lat_e4, int w, int h, bool miles, int &dist, int &radius_px);

}  // namespace wx
