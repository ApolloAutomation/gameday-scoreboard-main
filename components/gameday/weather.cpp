// Weather data core (weather.h): URLs, parsing, words and map math.
#include "weather.h"

#include <cstdio>
#include <cstring>

#include "app_f1.h"
#include "f1.h"
#include "timezones.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace wx {

using espn::app_local;

// ---- Units ---------------------------------------------------------------------

Units units_for_zone(int tz) { return Units{espn::tz_fahrenheit(tz), espn::tz_miles(tz)}; }

static long round10(long v) { return v >= 0 ? (v + 5) / 10 : -((-v + 5) / 10); }

int temp_of(int16_t c10, bool f) { return (int) round10(f ? (long) c10 * 9 / 5 + 320 : c10); }

std::string deg(int16_t c10, const Units &u, bool unit) {
  return std::to_string(temp_of(c10, u.f)) + "\xC2\xB0" + (unit ? (u.f ? "F" : "C") : "");
}

int speed_of(int16_t kmh10, bool mph) { return (int) round10(mph ? (long) kmh10 * 1000 / 1609 : kmh10); }

const char *compass(int d) {
  static const char *const k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (d < 0)
    return "";
  return k[((d % 360) * 2 + 45) / 90 % 8];
}

std::string wind_text(int16_t kmh10, int16_t dir, const Units &u, bool tight) {
  const int v = speed_of(kmh10, u.mph);
  if (v < 1)
    return "Calm";
  const char *c = compass(dir);
  char buf[24];
  snprintf(buf, sizeof buf, "%s%s%d%s%s", c, *c != '\0' ? " " : "", v, tight ? "" : " ", u.mph ? "mph" : "km/h");
  return buf;
}

std::string clock_text(const struct tm &t) {
  char buf[12];
  const int h = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
  snprintf(buf, sizeof(buf), "%d:%02d%c", h, t.tm_min, t.tm_hour < 12 ? 'a' : 'p');
  return buf;
}

std::string hour_text(const struct tm &t) {
  const int h = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
  return std::to_string(h) + (t.tm_hour < 12 ? "a" : "p");
}

const char *day_name(const struct tm &t) {
  static const char *const k[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return t.tm_wday >= 0 && t.tm_wday < 7 ? k[t.tm_wday] : "";
}

int64_t iso_epoch(const char *s) {
  if (s == nullptr || strlen(s) < 16)
    return 0;
  const std::string str(s);
  int64_t t = espn::parse_iso8601_z(str);
  if (t == 0)
    return 0;
  // The offset after the seconds, if any
  const size_t at = str.find_first_of("+-Z", 16);
  if (at == std::string::npos || str[at] == 'Z' || at + 3 > str.size())
    return t;
  const int hh = atoi(str.c_str() + at + 1), mm = at + 6 <= str.size() ? atoi(str.c_str() + at + 4) : 0;
  const int64_t off = (int64_t) hh * 3600 + mm * 60;
  return str[at] == '-' ? t + off : t - off;
}

// ---- Sources --------------------------------------------------------------------

int32_t round_e4(int32_t e4) { return (int32_t) (e4 >= 0 ? (e4 + 50) / 100 * 100 : -((-e4 + 50) / 100 * 100)); }

// "47.61", from degrees times 10,000 rounded.
static std::string hundredths(int32_t e4) {
  const int32_t r = round_e4(e4) / 100;
  const uint32_t a = (uint32_t) (r < 0 ? -r : r);
  char buf[16];
  snprintf(buf, sizeof(buf), "%s%u.%02u", r < 0 ? "-" : "", (unsigned) (a / 100), (unsigned) (a % 100));
  return buf;
}

static std::string at_home(const char *lat_key, const char *lon_key, int32_t lat_e4, int32_t lon_e4) {
  return std::string(lat_key) + hundredths(lat_e4) + lon_key + hundredths(lon_e4);
}

std::string forecast_url(int32_t lat_e4, int32_t lon_e4) {
  return "https://api.open-meteo.com/v1/forecast" + at_home("?latitude=", "&longitude=", lat_e4, lon_e4) +
         "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m,"
         "wind_direction_10m,wind_gusts_10m"
         "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
         "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
         "&timezone=auto&timeformat=unixtime&forecast_days=7&forecast_hours=24";
}

std::string air_url(int32_t lat_e4, int32_t lon_e4) {
  return "https://air-quality-api.open-meteo.com/v1/air-quality" +
         at_home("?latitude=", "&longitude=", lat_e4, lon_e4) +
         "&current=us_aqi,pm2_5,us_aqi_pm2_5,us_aqi_pm10,us_aqi_ozone,us_aqi_nitrogen_dioxide,"
         "us_aqi_sulphur_dioxide,us_aqi_carbon_monoxide&timeformat=unixtime";
}

std::string alerts_url(int32_t lat_e4, int32_t lon_e4) {
  return "https://api.weather.gov/alerts/active" + at_home("?point=", ",", lat_e4, lon_e4);
}

std::string met_url(int32_t lat_e4, int32_t lon_e4) { return espn::f1_met_url(round_e4(lat_e4), round_e4(lon_e4)); }

bool in_us(int32_t lat_e4, int32_t lon_e4) {
  struct Box {
    int16_t s, n, w, e;  // tenths of a degree
  };
  // The lower 48, Alaska, Hawaii, Puerto Rico, Guam
  static const Box k[] = {{240, 500, -1250, -660}, {510, 720, -1800, -1290}, {180, 230, -1610, -1540},
                          {175, 186, -680, -645},  {130, 210, 1440, 1465}};
  const int32_t la = lat_e4 / 1000, lo = lon_e4 / 1000;
  for (const Box &b : k)
    if (la >= b.s && la <= b.n && lo >= b.w && lo <= b.e)
      return true;
  return false;
}

namespace detail {

using espn::detail::field;
using espn::detail::flag;
using espn::detail::keep;

// One reader each, not one per call
__attribute__((noinline)) static float num(JsonVariantConst v, float none) {
  return v.is<float>() ? v.as<float>() : none;
}
__attribute__((noinline)) static int64_t epoch(JsonVariantConst v) {
  return v.is<long long>() ? v.as<long long>() : 0;
}
__attribute__((noinline)) static const char *str(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  return s != nullptr ? s : "";
}

static long clamp(float v, long lo, long hi) {
  const long t = lroundf(v);
  return t < lo ? lo : t > hi ? hi : t;
}

static int16_t c10_of(JsonVariantConst v, int16_t none) {
  return v.is<float>() ? (int16_t) clamp(num(v, 0) * 10.0f, -1000, 1000) : none;
}

static int16_t kmh10_of(JsonVariantConst v) { return (int16_t) clamp(num(v, 0) * 10.0f, 0, 5000); }

static int8_t pct_of(JsonVariantConst v) { return v.is<float>() ? (int8_t) clamp(num(v, 0), 0, 100) : (int8_t) -1; }

static uint8_t code_of(JsonVariantConst v) {
  const long c = v.is<float>() ? clamp(num(v, 0), -1, 999) : -1;
  return (uint8_t) (c < 0 || c > 99 ? 255 : c);
}

static int16_t dir_of(JsonVariantConst v) { return v.is<float>() ? (int16_t) (clamp(num(v, 0), 0, 1000) % 360) : -1; }

// 1 or missing is day
static bool day_of(JsonVariantConst v) { return num(v, 1) != 0; }

void fill_forecast_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"error"});
  keep(f["current"].to<JsonObject>(), {"time", "temperature_2m", "apparent_temperature", "relative_humidity_2m",
                                       "weather_code", "is_day", "wind_speed_10m", "wind_direction_10m",
                                       "wind_gusts_10m"});
  keep(f["hourly"].to<JsonObject>(), {"time", "temperature_2m", "weather_code", "precipitation_probability", "is_day"});
  keep(f["daily"].to<JsonObject>(), {"time", "weather_code", "temperature_2m_max", "temperature_2m_min",
                                     "precipitation_probability_max", "sunrise", "sunset"});
}

bool forecast_from_doc(JsonObjectConst root, Forecast &out) {
  JsonObjectConst cur = root["current"];
  if (flag(root, "error") || cur.isNull() || !cur["temperature_2m"].is<float>())
    return false;
  Forecast f{};
  Now &n = f.now;
  n.at = epoch(cur["time"]);
  n.temp_c10 = c10_of(cur["temperature_2m"], 0);
  n.feels_c10 = c10_of(cur["apparent_temperature"], n.temp_c10);
  n.humidity = pct_of(cur["relative_humidity_2m"]);
  n.code = code_of(cur["weather_code"]);
  n.day = day_of(cur["is_day"]);
  n.wind_kmh10 = kmh10_of(cur["wind_speed_10m"]);
  n.gust_kmh10 = kmh10_of(cur["wind_gusts_10m"]);
  n.wind_dir = dir_of(cur["wind_direction_10m"]);
  n.valid = true;
  JsonObjectConst h = root["hourly"];
  JsonArrayConst ht = h["time"];
  for (size_t i = 0; i < ht.size() && f.n_hours < kHours; i++) {
    if (!h["temperature_2m"][i].is<float>())
      continue;
    Hour &o = f.hours[f.n_hours++];
    o.at = epoch(ht[i]);
    o.temp_c10 = c10_of(h["temperature_2m"][i], 0);
    o.code = code_of(h["weather_code"][i]);
    o.pop = pct_of(h["precipitation_probability"][i]);
    o.day = day_of(h["is_day"][i]);
  }
  JsonObjectConst d = root["daily"];
  JsonArrayConst dt = d["time"];
  for (size_t i = 0; i < dt.size() && f.n_days < kDays; i++) {
    if (!d["temperature_2m_max"][i].is<float>() || !d["temperature_2m_min"][i].is<float>())
      continue;
    Day &o = f.days[f.n_days++];
    o.at = epoch(dt[i]);
    o.hi_c10 = c10_of(d["temperature_2m_max"][i], 0);
    o.lo_c10 = c10_of(d["temperature_2m_min"][i], 0);
    o.code = code_of(d["weather_code"][i]);
    o.pop = pct_of(d["precipitation_probability_max"][i]);
    o.sunrise = epoch(d["sunrise"][i]);
    o.sunset = epoch(d["sunset"][i]);
  }
  f.valid = true;
  out = f;
  return true;
}

void fill_met_filter(JsonDocument &f) {
  JsonObject t = f["properties"]["timeseries"].add<JsonObject>();
  keep(t, {"time"});
  JsonObject d = t["data"].to<JsonObject>();
  keep(d["instant"]["details"].to<JsonObject>(),
       {"air_temperature", "relative_humidity", "wind_speed", "wind_speed_of_gust", "wind_from_direction"});
  for (const char *k : {"next_1_hours", "next_6_hours"})
    keep(d[k]["summary"].to<JsonObject>(), {"symbol_code"});
}

// Steadman's apparent temperature, as Open-Meteo's.
static int16_t feels_c10(float t, float rh, float ws) {
  const float e = rh / 100.0f * 6.105f * powf(2.7182818f, 17.27f * t / (237.7f + t));
  return (int16_t) lround((t + 0.33f * e - 0.70f * ws - 4.0f) * 10.0f);
}

bool met_from_doc(JsonObjectConst root, int64_t now, Forecast &out) {
  JsonArrayConst series = root["properties"]["timeseries"];
  if (series.isNull())
    return false;
  Forecast f{};
  f.src = Src::MET;
  int day_key = -1, noon_gap = 99;
  size_t looked = 0;
  for (JsonVariantConst e : series) {
    if (++looked > 120)
      break;
    JsonVariantConst det = e["data"]["instant"]["details"];
    const int64_t t = espn::detail::when(e, "time");
    if (t <= 0 || !det["air_temperature"].is<float>())
      continue;
    const char *s1 = str(e["data"]["next_1_hours"]["summary"]["symbol_code"]);
    const char *s6 = str(e["data"]["next_6_hours"]["summary"]["symbol_code"]);
    const char *sym = *s1 != '\0' ? s1 : s6;
    const float temp = det["air_temperature"].as<float>();
    const int16_t c10 = c10_of(det["air_temperature"], 0);
    const uint8_t code = *sym != '\0' ? espn::met_weather_code(sym) : 255;
    // The latest hour not ahead of now, else the first
    if (!f.now.valid || t <= now) {
      Now &n = f.now;
      const float ws = num(det["wind_speed"], 0), rh = num(det["relative_humidity"], -1);
      n.valid = true;
      n.at = t;
      n.temp_c10 = c10;
      n.feels_c10 = rh >= 0 ? feels_c10(temp, rh, ws) : c10;
      n.humidity = pct_of(det["relative_humidity"]);
      n.wind_kmh10 = kmh10_of(det["wind_speed"]) * 36 / 10;
      n.gust_kmh10 = kmh10_of(det["wind_speed_of_gust"]) * 36 / 10;
      n.wind_dir = dir_of(det["wind_from_direction"]);
      n.code = code;
      n.day = met_day(sym);
    }
    if (t + 1800 >= now && *s1 != '\0' && f.n_hours < kHours) {
      Hour &h = f.hours[f.n_hours++];
      h.at = t;
      h.temp_c10 = c10;
      h.code = code;
      h.pop = -1;
      h.day = met_day(s1);
    }
    // Days by the panel's own calendar
    const struct tm lt = app_local(t);
    const int key = lt.tm_year * 400 + lt.tm_yday;
    if (key != day_key) {
      if (f.n_days >= kDays)
        break;
      day_key = key;
      noon_gap = 99;
      Day &d = f.days[f.n_days++];
      d.at = t - lt.tm_hour * 3600 - lt.tm_min * 60;
      d.hi_c10 = d.lo_c10 = c10;
      d.code = 255;
      d.pop = -1;
    }
    Day &d = f.days[f.n_days - 1];
    d.hi_c10 = std::max(d.hi_c10, c10);
    d.lo_c10 = std::min(d.lo_c10, c10);
    const int gap = std::abs(lt.tm_hour - 12);
    if (code != 255 && gap < noon_gap) {
      noon_gap = gap;
      d.code = *s6 != '\0' ? espn::met_weather_code(s6) : code;
    }
  }
  if (!f.now.valid)
    return false;
  f.valid = true;
  out = f;
  return true;
}

void fill_air_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"error"});
  keep(f["current"].to<JsonObject>(), {"time", "us_aqi", "pm2_5", "us_aqi_pm2_5", "us_aqi_pm10", "us_aqi_ozone",
                                       "us_aqi_nitrogen_dioxide", "us_aqi_sulphur_dioxide", "us_aqi_carbon_monoxide"});
}

bool air_from_doc(JsonObjectConst root, Air &out) {
  JsonObjectConst cur = root["current"];
  if (flag(root, "error") || cur.isNull() || !cur["us_aqi"].is<float>())
    return false;
  Air a{};
  a.pm25_10 = -1;
  a.at = epoch(cur["time"]);
  a.aqi = (int16_t) clamp(num(cur["us_aqi"], 0), 0, 999);
  if (cur["pm2_5"].is<float>())
    a.pm25_10 = (int16_t) clamp(num(cur["pm2_5"], 0) * 10.0f, 0, 9999);
  static const char *const kSub[] = {"us_aqi_pm2_5", "us_aqi_pm10", "us_aqi_ozone", "us_aqi_nitrogen_dioxide",
                                     "us_aqi_sulphur_dioxide", "us_aqi_carbon_monoxide"};
  float most = 0;
  for (int i = 0; i < 6; i++) {
    const float v = num(cur[kSub[i]], -1);
    if (v > most) {
      most = v;
      a.main = (Pollutant) (i + 1);
    }
  }
  a.valid = true;
  out = a;
  return true;
}

void fill_alerts_filter(JsonDocument &f) {
  keep(f["features"].add<JsonObject>()["properties"].to<JsonObject>(),
       {"event", "description", "ends", "expires", "status", "messageType"});
}

bool alerts_from_doc(JsonObjectConst root, int64_t now, Alerts &out) {
  JsonArrayConst feats = root["features"];
  if (feats.isNull())
    return false;
  Alerts a;
  for (JsonVariantConst ft : feats) {
    JsonVariantConst p = ft["properties"];
    const char *event = str(p["event"]), *status = str(p["status"]);
    if (*event == '\0' || (*status != '\0' && strcmp(status, "Actual") != 0) ||
        strcmp(str(p["messageType"]), "Cancel") == 0)
      continue;
    int64_t ends = iso_epoch(str(p["ends"]));
    if (ends == 0)
      ends = iso_epoch(str(p["expires"]));
    if (ends != 0 && ends <= now)
      continue;
    bool seen = false;
    for (size_t i = 0; i < a.n; i++)
      seen = seen || strncmp(a.a[i].event, event, kAlertEvent - 1) == 0;
    if (seen)
      continue;
    Alert al;
    al.level = alert_level(event);
    al.ends = ends;
    snprintf(al.event, sizeof(al.event), "%s", event);
    snprintf(al.text, sizeof(al.text), "%s", tidy_alert(str(p["description"]), kAlertText - 1).c_str());
    // Most urgent first, then the soonest to end
    size_t at = a.n;
    while (at > 0 && (a.a[at - 1].level < al.level || (a.a[at - 1].level == al.level && a.a[at - 1].ends != 0 &&
                                                         al.ends != 0 && a.a[at - 1].ends > al.ends)))
      at--;
    if (at >= kAlerts)
      continue;
    for (size_t i = std::min<size_t>(a.n, kAlerts - 1); i > at; i--)
      a.a[i] = a.a[i - 1];
    a.a[at] = al;
    if (a.n < kAlerts)
      a.n++;
  }
  a.valid = true;
  out = a;
  return true;
}

void fill_radar_filter(JsonDocument &f) {
  keep(f.to<JsonObject>(), {"host"});
  keep(f["radar"]["past"].add<JsonObject>(), {"time", "path"});
}

bool radar_from_doc(JsonObjectConst root, Radar &out) {
  const char *host = str(root["host"]);
  JsonArrayConst past = root["radar"]["past"];
  if (strncmp(host, "https://", 8) != 0 || strlen(host) >= sizeof(out.host) || past.isNull())
    return false;
  Radar r;
  snprintf(r.host, sizeof(r.host), "%s", host);
  for (JsonVariantConst p : past) {
    const char *path = str(p["path"]);
    const int64_t t = epoch(p["time"]);
    if (t <= 0 || *path != '/' || strlen(path) >= kFramePath)
      continue;
    // The newest kFrames, oldest first
    if (r.n == kFrames) {
      memmove(&r.t[0], &r.t[1], sizeof(r.t[0]) * (kFrames - 1));
      memmove(&r.path[0], &r.path[1], sizeof(r.path[0]) * (kFrames - 1));
      r.n--;
    }
    r.t[r.n] = t;
    snprintf(r.path[r.n], kFramePath, "%s", path);
    r.n++;
  }
  if (r.n == 0)
    return false;
  r.valid = true;
  out = r;
  return true;
}

}  // namespace detail

// ---- Icons, words, colors ----------------------------------------------------------

Icon icon_for(uint8_t c, bool day) {
  if (c <= 1)
    return day ? Icon::SUN : Icon::MOON;
  if (c == 2)
    return day ? Icon::PARTLY_DAY : Icon::PARTLY_NIGHT;
  if (c == 3)
    return Icon::CLOUDY;
  if (c == 45 || c == 48)
    return Icon::FOG;
  if (c >= 51 && c <= 55)
    return Icon::DRIZZLE;
  if (c == 56 || c == 57 || c == 66 || c == 67)
    return Icon::SLEET;
  if (c == 65 || c == 82)
    return Icon::HEAVY_RAIN;
  if ((c >= 61 && c <= 63) || c == 80 || c == 81)
    return Icon::RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86)
    return Icon::SNOW;
  if (c >= 95 && c <= 99)
    return Icon::THUNDER;
  return Icon::NONE;
}

Icon now_icon(const Now &n) {
  const bool windy = n.wind_kmh10 >= 400 || n.gust_kmh10 >= 650;
  return windy && n.code <= 3 ? Icon::WIND : icon_for(n.code, n.day);
}

bool met_day(const char *s) { return s == nullptr || strstr(s, "_night") == nullptr; }

std::string sky_text(uint8_t code, bool day, size_t max) {
  if (code <= 1 && !day)
    return "Clear";
  if (code == 2)
    return max >= 13 ? "Partly cloudy" : "Pt cloudy";
  if (code == 3)
    return "Cloudy";
  if (code == 1)
    return max >= 12 ? "Mostly sunny" : "Sunny";
  if (code == 0)
    return "Sunny";
  if (code >= 95)
    return max >= 12 ? "Thunderstorm" : "T-storm";
  const std::string l = espn::f1_weather_long(code), w = espn::f1_weather_word(code);
  return l.size() <= max ? l : w;
}

int aqi_band(int aqi) { return aqi <= 50 ? 0 : aqi <= 100 ? 1 : aqi <= 150 ? 2 : aqi <= 200 ? 3 : aqi <= 300 ? 4 : 5; }

const char *aqi_name(int band, bool tight) {
  static const char *const k[] = {"Good", "Moderate", "Unhealthy for sensitive groups", "Unhealthy",
                                  "Very unhealthy", "Hazardous"};
  if (tight && (band == 2 || band == 4))
    return band == 2 ? "Sensitive" : "V unhealthy";
  return k[band < 0 ? 0 : band > 5 ? 5 : band];
}

uint32_t aqi_color(int band) {
  // EPA's colors, lifted for LEDs
  static const uint32_t k[] = {0x00E400, 0xFFE000, 0xFF7E00, 0xFF2020, 0xC040FF, 0xC02040};
  return k[band < 0 ? 0 : band > 5 ? 5 : band];
}

const char *pollutant_name(Pollutant p) {
  static const char *const k[] = {"", "PM2.5", "PM10", "Ozone", "NO2", "SO2", "CO"};
  return k[(int) p < 7 ? (int) p : 0];
}

Level alert_level(const char *event) {
  const size_t n = event != nullptr ? strlen(event) : 0;
  auto ends = [event, n](const char *w) {
    const size_t k = strlen(w);
    return n >= k && strcmp(event + n - k, w) == 0;
  };
  if (ends("Warning") || ends("Emergency"))
    return Level::WARNING;
  if (ends("Watch"))
    return Level::WATCH;
  if (ends("Advisory"))
    return Level::ADVISORY;
  return Level::STATEMENT;
}

uint32_t level_color(Level l) {
  static const uint32_t k[] = {0x40C8FF, 0xFFD000, 0xFF8C00, 0xFF2020};
  return k[(int) l & 3];
}

std::string tidy_alert(const char *desc, size_t max) {
  std::string out;
  // Its start only: some run to many KB
  for (const char *c = desc != nullptr ? desc : ""; *c != '\0' && out.size() < 1024; c++) {
    const char ch = *c == '\n' || *c == '\r' || *c == '\t' ? ' ' : *c;
    if (ch != ' ' || (!out.empty() && out.back() != ' '))
      out += ch;
  }
  // NWS sections: "* WHAT...", the what alone
  const size_t what = out.find("* WHAT...");
  if (what != std::string::npos) {
    out.erase(0, what + 9);
    const size_t next = out.find(" * ");
    if (next != std::string::npos)
      out.resize(next);
  }
  for (size_t at; (at = out.find("...")) != std::string::npos;)
    out.replace(at, 3, ", ");
  if (out.size() > max) {
    const size_t cut = out.rfind(' ', max - 3);
    out = out.substr(0, cut == std::string::npos ? max - 3 : cut) + "...";
  }
  return out;
}

espn::AppArt icon_at(Icon i, bool big, int x, int y, int scale) {
  espn::AppArt a;
  const IconArt *art = icon_art(i, big);
  if (art == nullptr)
    return a;
  a.x = (int16_t) x;
  a.y = (int16_t) y;
  a.w = art->w;
  a.h = art->h;
  a.bits = art->bits;
  a.scale = (uint8_t) (scale < 1 ? 1 : scale);
  for (int k = 0; k < 3; k++)
    a.ink[k] = art->ink[k];
  return a;
}

// ---- Radar ----------------------------------------------------------------------

std::string rainviewer_url(const Radar &r, size_t i, int32_t lat_e4, int32_t lon_e4) {
  if (!r.valid || i >= r.n)
    return "";
  // Size, zoom, center; color 2, smoothed, snow shown
  return std::string(r.host) + r.path[i] + "/256/" + std::to_string(kRadarZoom) + "/" + hundredths(lat_e4) + "/" +
         hundredths(lon_e4) + "/2/1_1.png";
}

std::string iem_url(int64_t t, int32_t lat_e4, int32_t lon_e4, int w, int h) {
  if (w < 1 || h < 1)
    return "";
  const float lat = round_e4(lat_e4) / 10000.0f, lon = round_e4(lon_e4) / 10000.0f;
  // The world's width at zoom 7 over 256 px
  const float span = 2.0f * kMercHalf / (float) (1 << kRadarZoom);
  const float hw = w >= h ? span / 2 : span / 2 * w / h, hh = w >= h ? span / 2 * h / w : span / 2;
  const float x = merc_m_x(lon), y = merc_m_y(lat);
  int yr = 0, mo = 0, d = 0;
  espn::detail::civil_from_epoch(t, yr, mo, d);
  const int64_t sod = ((t % 86400) + 86400) % 86400;
  char buf[320];
  snprintf(buf, sizeof(buf),
           "https://mesonet.agron.iastate.edu/cgi-bin/wms/nexrad/n0q-t.cgi?SERVICE=WMS&REQUEST=GetMap"
           "&VERSION=1.1.1&LAYERS=nexrad-n0q-wmst&STYLES=&SRS=EPSG:3857&BBOX=%.0f,%.0f,%.0f,%.0f&WIDTH=%d&HEIGHT=%d"
           "&FORMAT=image/png&TRANSPARENT=true&TIME=%04d-%02d-%02dT%02d:%02d:00Z",
           (double) (x - hw), (double) (y - hh), (double) (x + hw), (double) (y + hh), w, h, yr, mo, d,
           (int) (sod / 3600), (int) (sod % 3600 / 60));
  return buf;
}

int64_t iem_frame_time(int64_t now, size_t i, size_t n) {
  // Its mosaic lands a few minutes after each five
  const int64_t last = (now - 360) / 600 * 600;
  return last - (int64_t) (n - 1 - (i < n ? i : n - 1)) * 600;
}

float radar_m_per_px(int32_t lat_e4, int w, int h) {
  const int side = std::max(1, std::max(w, h));
  return ground_m_per_px(lat_e4 / 10000.0f, kRadarZoom) * kTilePx / side;
}

bool radar_ring(int32_t lat_e4, int w, int h, bool miles, int &dist, int &radius_px) {
  static const int kMi[] = {100, 50, 25, 10, 5}, kKm[] = {150, 100, 50, 25, 10};
  const float unit_m = miles ? 1609.344f : 1000.0f, per_px = radar_m_per_px(lat_e4, w, h);
  // The biggest whose circle keeps a margin
  const int room = std::min(w, h) * 2 / 5;
  for (int i = 0; i < 5; i++) {
    const int r = (int) lroundf((miles ? kMi[i] : kKm[i]) * unit_m / per_px);
    if (r <= room && r >= 6) {
      dist = miles ? kMi[i] : kKm[i];
      radius_px = r;
      return true;
    }
  }
  return false;
}

}  // namespace wx
