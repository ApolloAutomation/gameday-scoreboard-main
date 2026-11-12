// Flight Tracker: parsing, math, names, routes and frames.
#include "flights.h"

#include "flight_airlines.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

namespace flights {

using esphome::gameday::font_px;

namespace {

const double kPi = 3.14159265358979323846;
const double kEarthNm = 3440.065;
const float kNmMiles = 1.150779f;
const float kNmKm = 1.852f;
const float kFtM = 0.3048f;
const float kKtMph = 1.150779f;
const float kKtKmh = 1.852f;

double rad(double d) { return d * kPi / 180.0; }

void copy_cut(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  if (src != nullptr)
    for (; i + 1 < cap && src[i] != '\0'; i++)
      dst[i] = src[i];
  dst[i] = '\0';
}

std::string with_commas(long v) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%ld", v < 0 ? -v : v);
  std::string digits(buf), out;
  for (size_t i = 0; i < digits.size(); i++) {
    if (i > 0 && (digits.size() - i) % 3 == 0)
      out += ',';
    out += digits[i];
  }
  return v < 0 ? "-" + out : out;
}

std::string fmt(const char *f, double v) {
  char buf[32];
  snprintf(buf, sizeof(buf), f, v);
  return buf;
}

std::string cut(const std::string &s, size_t max) { return s.size() > max ? s.substr(0, max) : s; }

}

// ---- Sources and cadence ------------------------------------------------------

const char *source_name(Source s) {
  switch (s) {
    case Source::ADSBLOL:
      return "adsb.lol";
    case Source::LOCAL:
      return "your receiver";
    case Source::FR24:
      return "Flightradar24";
    default:
      return "adsb.fi";
  }
}

const char *source_key(Source s) {
  switch (s) {
    case Source::ADSBLOL:
      return "adsblol";
    case Source::LOCAL:
      return "receiver";
    case Source::FR24:
      return "fr24";
    default:
      return "adsbfi";
  }
}

bool Home::valid() const {
  return std::isfinite(this->lat) && std::isfinite(this->lon) && this->lat >= -90 && this->lat <= 90 &&
         this->lon >= -180 && this->lon <= 180 && !(this->lat == 0 && this->lon == 0) && this->radius_nm > 0;
}

int query_radius_nm(const Home &h) {
  const float r = std::isfinite(h.radius_nm) ? h.radius_nm : 10.0f;
  int nm = (int) std::ceil(r + 1.0f);
  return nm < 1 ? 1 : nm > (int) kMaxRadiusNm ? (int) kMaxRadiusNm : nm;
}

namespace {

double round2(double v) {
  const double r = std::round(v * 100.0) / 100.0;
  return r == 0 ? 0 : r;
}

}

namespace {

// The box's half width in longitude; 180 all of it.
double box_dlon(const Home &h) {
  const double r = query_radius_nm(h) / 60.0;
  const double c = cosf((float) rad(round2(h.lat)));
  return c > r / 180.0 ? std::min(180.0, r / c) : 180.0;
}

}

Bounds query_bounds(const Home &h) {
  Bounds b;
  const double lat = round2(h.lat), lon = round2(h.lon);
  // A nautical mile is a minute of latitude.
  const double r = query_radius_nm(h) / 60.0;
  const double dlon = box_dlon(h);
  b.north = std::min(90.0, lat + r);
  b.south = std::max(-90.0, lat - r);
  b.west = dlon >= 180.0 ? -180.0 : std::max(-180.0, lon - dlon);
  b.east = dlon >= 180.0 ? 180.0 : std::min(180.0, lon + dlon);
  return b;
}

bool box_wraps(const Home &h) {
  if (!h.valid())
    return false;
  const double lon = round2(h.lon), dlon = box_dlon(h);
  return dlon < 180.0 && (lon - dlon < -180.0 || lon + dlon > 180.0);
}

std::string fr24_url(const Home &h) {
  const Bounds b = query_bounds(h);
  char buf[288];
  snprintf(buf, sizeof(buf),
           "https://%s/zones/fcgi/feed.js?bounds=%.2f,%.2f,%.2f,%.2f&faa=1&satellite=1&mlat=1&flarm=1&adsb=1&gnd=0"
           "&air=1&vehicles=0&estimated=0&maxage=14400&gliders=0&stats=0",
           kFr24Host, b.north, b.south, b.west, b.east);
  return buf;
}

std::string adsbfi_url(const Home &h) {
  char buf[96];
  snprintf(buf, sizeof(buf), "https://%s/api/v3/lat/%.2f/lon/%.2f/dist/%d", kAdsbFiHost, round2(h.lat), round2(h.lon),
           query_radius_nm(h));
  return buf;
}

std::string adsblol_url(const Home &h) {
  char buf[96];
  snprintf(buf, sizeof(buf), "https://%s/v2/point/%.2f/%.2f/%d", kAdsbLolHost, round2(h.lat), round2(h.lon),
           query_radius_nm(h));
  return buf;
}

std::string local_url(const std::string &entered, const Home &h) {
  size_t a = 0, b = entered.size();
  while (a < b && isspace((unsigned char) entered[a]))
    a++;
  while (b > a && isspace((unsigned char) entered[b - 1]))
    b--;
  std::string s = entered.substr(a, b - a);
  if (s.empty())
    return s;
  if (s.find("://") == std::string::npos)
    s = "http://" + s;
  const size_t host = s.find("://") + 3;
  size_t path = s.find_first_of("/?", host);
  if (path == std::string::npos) {
    s += "/";
    path = s.size() - 1;
  }
  const std::string hostport = s.substr(host, path - host);
  size_t q = s.find('?', path);
  const std::string p = s.substr(path, q == std::string::npos ? std::string::npos : q - path);
  const bool api = q != std::string::npos || p.find("re-api") != std::string::npos ||
                   (hostport.size() > 6 && hostport.compare(hostport.size() - 6, 6, ":30152") == 0);
  if (!api)
    return p == "/" ? s.substr(0, path) + "/tar1090/data/aircraft.json" : s;
  char circle[64];
  snprintf(circle, sizeof(circle), "circle=%.4f,%.4f,%d", h.lat, h.lon, query_radius_nm(h));
  if (q == std::string::npos)
    return s + "?" + circle;
  size_t at = s.find("circle=", q);
  if (at == std::string::npos)
    return s + (q + 1 == s.size() ? "" : "&") + circle;
  const size_t end = s.find('&', at);
  return s.substr(0, at) + circle + (end == std::string::npos ? "" : s.substr(end));
}

uint32_t feed_gap_ms(const Feed &f, Source s) {
  switch (s) {
    case Source::ADSBLOL:
      return f.lol_gap_ms;
    case Source::LOCAL:
      return kLocalPollMs;
    case Source::FR24:
      return kFr24PollMs;
    default:
      return kFiPollMs;
  }
}

bool feed_out(const Feed &f, Source s, uint32_t now_ms) {
  const espn::HostWait &w = f.wait[(size_t) s];
  return w.waiting(now_ms) && (w.run.throttled || w.run.streak >= kFailoverStreak);
}

FeedPick feed_pick(const Feed &f, bool local, uint32_t now_ms, bool fr24) {
  Source order[kSources];
  size_t n = 0;
  if (local)
    order[n++] = Source::LOCAL;
  if (fr24)
    order[n++] = Source::FR24;
  order[n++] = Source::ADSBFI;
  order[n++] = Source::ADSBLOL;
  FeedPick best;
  bool have = false;
  for (size_t i = 0; i < n; i++) {
    const Source s = order[i];
    const size_t k = (size_t) s;
    uint32_t due = 0;
    if (f.read_once[k]) {
      const uint32_t since = now_ms - f.last_ms[k];
      const uint32_t gap = feed_gap_ms(f, s);
      due = since >= gap ? 0 : gap - since;
    }
    const uint32_t left = f.wait[k].left(now_ms);
    const uint32_t t = left > due ? left : due;
    if (!have || t < best.in_ms) {
      best.source = s;
      best.in_ms = t;
      have = true;
    }
    if (!feed_out(f, s, now_ms))
      break;
  }
  return best;
}

void feed_started(Feed &f, Source s, uint32_t now_ms) {
  f.last_ms[(size_t) s] = now_ms;
  f.read_once[(size_t) s] = true;
}

void feed_done(Feed &f, Source s, int status, bool parsed, uint32_t retry_after_ms, uint32_t rnd, uint32_t now_ms) {
  espn::HostWait &w = f.wait[(size_t) s];
  if (status == 200 && parsed)
    w.clear();
  else
    w.fail(espn::is_throttle_status(status), retry_after_ms, rnd, now_ms);
}

namespace {

long plain_number(const std::string &s) {
  if (s.empty() || s.size() > 9)
    return -1;
  long v = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return -1;
    v = v * 10 + (c - '0');
  }
  return v;
}

}

void feed_lol_limits(Feed &f, const std::string &remaining, const std::string &reset_s) {
  const long left = plain_number(remaining), reset = plain_number(reset_s);
  if (left < 0 || reset < 0)
    return;
  const uint64_t window = (uint64_t) reset * 1000;
  uint64_t gap = left > 0 ? window / (uint64_t) left : window;
  if (gap < kLolMinMs)
    gap = kLolMinMs;
  if (gap > kLolMaxMs)
    gap = kLolMaxMs;
  f.lol_gap_ms = (uint32_t) gap;
}

// ---- Units -------------------------------------------------------------------

Units units_metric() {
  Units u;
  u.alt = AltUnit::METERS;
  u.speed = SpeedUnit::KMH;
  u.dist = DistUnit::KM;
  return u;
}

float to_nm(float value, DistUnit u) {
  switch (u) {
    case DistUnit::MILES:
      return value / kNmMiles;
    case DistUnit::KM:
      return value / kNmKm;
    default:
      return value;
  }
}

float from_nm(float nm, DistUnit u) {
  switch (u) {
    case DistUnit::MILES:
      return nm * kNmMiles;
    case DistUnit::KM:
      return nm * kNmKm;
    default:
      return nm;
  }
}

// ---- Geometry ----------------------------------------------------------------

// Float trig: the firmware links it already.
double distance_nm(double lat1, double lon1, double lat2, double lon2) {
  const float dlat = (float) rad(lat2 - lat1), dlon = (float) rad(lon2 - lon1);
  const float sl = sinf(dlat / 2), so = sinf(dlon / 2);
  const float a = sl * sl + cosf((float) rad(lat1)) * cosf((float) rad(lat2)) * so * so;
  return 2 * kEarthNm * (double) atan2f(sqrtf(a), sqrtf(1 - a));
}

double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  const float dlon = (float) rad(lon2 - lon1), p1 = (float) rad(lat1), p2 = (float) rad(lat2);
  const float y = sinf(dlon) * cosf(p2);
  const float x = cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dlon);
  const double deg = (double) atan2f(y, x) * 180.0 / kPi;
  return deg < 0 ? deg + 360.0 : deg;
}

const char *compass8(float deg) {
  static const char *const k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (!std::isfinite(deg) || deg < 0)
    return "";
  const int i = (int) std::floor(std::fmod(deg + 22.5f, 360.0f) / 45.0f);
  return k[i & 7];
}

float angle_diff(float a, float b) {
  float d = std::fmod(std::fabs(a - b), 360.0f);
  return d > 180.0f ? 360.0f - d : d;
}

// ---- Aircraft ----------------------------------------------------------------

const char *plane_label(const Plane &p) {
  if (p.callsign[0] != '\0')
    return p.callsign;
  if (p.reg[0] != '\0')
    return p.reg;
  return p.hex;
}

void clean_callsign(const char *in, char out[9]) {
  size_t n = 0;
  bool nonzero = false;
  for (size_t i = 0; in != nullptr && in[i] != '\0' && n < 8; i++) {
    const char c = (char) toupper((unsigned char) in[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out[n++] = c;
      nonzero = nonzero || c != '0';
    }
  }
  out[nonzero ? n : 0] = '\0';
}

bool airline_callsign(const char *cs) {
  if (cs == nullptr)
    return false;
  for (int i = 0; i < 3; i++)
    if (cs[i] < 'A' || cs[i] > 'Z')
      return false;
  for (size_t i = 3; cs[i] != '\0'; i++)
    if (cs[i] >= '0' && cs[i] <= '9')
      return true;
  return false;
}

namespace {

float rank(const Plane &p) {
  const float within = p.tier == Tier::NOPOS ? p.seen_s : p.dist_nm;
  return (float) p.tier * 100000.0f + (within < 0 ? 0 : within);
}

}

void nearby_add(Nearby &n, const Plane &p) {
  for (size_t i = 0; i < n.n && p.hex[0] != '\0'; i++)
    if (strcmp(n.planes[i].hex, p.hex) == 0)
      return;
  if (p.tier == Tier::AIR && n.air < UINT16_MAX)
    n.air++;
  else if (p.tier == Tier::GROUND && n.ground < UINT16_MAX)
    n.ground++;
  const float key = rank(p);
  size_t at = n.n;
  while (at > 0 && rank(n.planes[at - 1]) > key)
    at--;
  if (at >= kNear)
    return;
  const size_t last = n.n < kNear ? n.n : kNear - 1;
  for (size_t i = last; i > at; i--)
    n.planes[i] = n.planes[i - 1];
  n.planes[at] = p;
  if (n.n < kNear)
    n.n++;
}

void nearby_finish(Nearby &n) {
  if (n.n == 0)
    return;
  n.tier = n.planes[0].tier;
  uint8_t keep = 0;
  while (keep < n.n && n.planes[keep].tier == n.tier)
    keep++;
  n.n = keep;
}

uint8_t pick_card(const Nearby &n, const char *prev_hex) {
  if (n.n == 0 || prev_hex == nullptr || prev_hex[0] == '\0')
    return 0;
  for (uint8_t i = 0; i < n.n; i++) {
    if (strcmp(n.planes[i].hex, prev_hex) != 0)
      continue;
    const float near = n.planes[0].dist_nm, mine = n.planes[i].dist_nm;
    if (i == 0 || near < 0 || mine < 0 || mine <= near * 1.3f + 0.5f)
      return i;
    return 0;
  }
  return 0;
}

ArduinoJson::Allocator *&json_allocator_slot() {
  static ArduinoJson::Allocator *slot = nullptr;
  return slot;
}

ArduinoJson::Allocator *json_allocator() {
  ArduinoJson::Allocator *a = json_allocator_slot();
  return a != nullptr ? a : ArduinoJson::detail::DefaultAllocator::instance();
}

void fill_aircraft_filter(JsonDocument &f) {
  static const char *const keys[] = {"hex",     "flight",    "r",         "t",          "category", "alt_baro",
                                     "alt_geom", "altitude", "gs",        "speed",      "track",    "lat",
                                     "lon",     "seen_pos",  "seen",      "baro_rate",  "geom_rate", "vert_rate",
                                     "squawk",  "emergency"};
  for (const char *k : keys)
    f[k] = true;
}

namespace {

bool number(JsonVariantConst v) { return v.is<float>() || v.is<int>() || v.is<long>(); }

// Where it is from home; false outside the radius.
bool place_plane(Plane &p, double lat, double lon, const Home &home) {
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::fabs(lat) > 90 || std::fabs(lon) > 180)
    return false;
  p.lat = (float) lat;
  p.lon = (float) lon;
  if (home.valid()) {
    const double d = distance_nm(home.lat, home.lon, lat, lon);
    if (d > home.radius_nm)
      return false;
    p.dist_nm = (float) d;
    p.bearing = (float) bearing_deg(home.lat, home.lon, lat, lon);
  }
  return true;
}

}

bool plane_from_json(JsonObjectConst o, const Home &home, Plane &p) {
  p = Plane();
  if (o.isNull())
    return false;
  const char *hex = o["hex"] | "";
  if (hex[0] == '~')
    hex++;
  size_t h = 0;
  for (; hex[h] != '\0' && h < 6; h++)
    p.hex[h] = (char) tolower((unsigned char) hex[h]);
  p.hex[h] = '\0';
  copy_cut(p.category, sizeof(p.category), o["category"] | "");
  if (p.category[0] == 'C')
    return false;
  p.seen_s = o["seen"] | 0.0f;
  if (!(p.seen_s <= kStaleS))
    return false;
  clean_callsign(o["flight"] | "", p.callsign);
  copy_cut(p.reg, sizeof(p.reg), o["r"] | "");
  copy_cut(p.type, sizeof(p.type), o["t"] | "");
  bool ground = false;
  for (const char *k : {"alt_baro", "alt_geom", "altitude"}) {
    JsonVariantConst v = o[k];
    if (v.is<const char *>() && strcmp(v.as<const char *>(), "ground") == 0) {
      ground = true;
      break;
    }
    if (number(v)) {
      const float ft = v.as<float>();
      if (std::isfinite(ft) && ft > -2000 && ft < 200000) {
        p.alt_ft = (int32_t) std::lround(ft);
        break;
      }
    }
  }
  JsonVariantConst gs = o["gs"].isNull() ? o["speed"] : o["gs"];
  if (number(gs) && gs.as<float>() >= 0 && gs.as<float>() < 2000)
    p.gs_kt = gs.as<float>();
  if (number(o["track"]))
    p.track = std::fmod(std::fmod(o["track"].as<float>(), 360.0f) + 360.0f, 360.0f);
  for (const char *k : {"baro_rate", "geom_rate", "vert_rate"}) {
    if (number(o[k])) {
      const float r = o[k].as<float>();
      p.vrate_fpm = (int16_t) (r > 32000 ? 32000 : r < -32000 ? -32000 : r);
      break;
    }
  }
  const char *sq = o["squawk"] | "";
  p.squawk = (uint16_t) (strlen(sq) == 4 ? atoi(sq) : 0);
  // Not lifeguard or minfuel: those are routine.
  const char *em = o["emergency"] | "none";
  p.emergency = emergency_squawk(p.squawk);
  for (const char *e : {"general", "unlawful", "nordo", "downed"})
    p.emergency = p.emergency || strcmp(em, e) == 0;
  const bool pos = number(o["lat"]) && number(o["lon"]) && (o["seen_pos"] | 0.0f) <= kStaleS;
  // Unplaced, it can't be called nearby.
  if (!pos && home.valid())
    return false;
  if (pos && !place_plane(p, o["lat"].as<double>(), o["lon"].as<double>(), home))
    return false;
  p.tier = !pos ? Tier::NOPOS : ground ? Tier::GROUND : Tier::AIR;
  return true;
}

bool plane_from_fr24(JsonArrayConst a, const Home &home, int64_t now_s, Plane &p) {
  p = Plane();
  // Each field once, in order: an index walks the list.
  JsonVariantConst f[19];
  size_t n = 0;
  for (JsonVariantConst e : a)
    if (n < 19)
      f[n++] = e;
  if (n < 17 || !number(f[1]) || !number(f[2]))
    return false;
  const char *hex = f[0] | "";
  size_t h = 0;
  for (; hex[h] != '\0' && h < 6; h++)
    p.hex[h] = (char) tolower((unsigned char) hex[h]);
  p.hex[h] = '\0';
  const int64_t at = f[10] | (int64_t) 0;
  p.seen_s = at > 0 && now_s > at ? (float) (now_s - at) : 0.0f;
  if (!(p.seen_s <= kStaleS))
    return false;
  clean_callsign(f[16] | "", p.callsign);
  copy_cut(p.reg, sizeof(p.reg), f[9] | "");
  copy_cut(p.type, sizeof(p.type), f[8] | "");
  copy_cut(p.flight, sizeof(p.flight), f[13] | "");
  const char *from = f[11] | "", *to = f[12] | "";
  if (from[0] != '\0' && to[0] != '\0' && strlen(from) < sizeof(p.from) && strlen(to) < sizeof(p.to)) {
    copy_cut(p.from, sizeof(p.from), from);
    copy_cut(p.to, sizeof(p.to), to);
  }
  const bool ground = (f[14] | 0) != 0;
  if (!ground && number(f[4])) {
    const float ft = f[4].as<float>();
    if (std::isfinite(ft) && ft > -2000 && ft < 200000)
      p.alt_ft = (int32_t) std::lround(ft);
  }
  if (number(f[5]) && f[5].as<float>() >= 0 && f[5].as<float>() < 2000)
    p.gs_kt = f[5].as<float>();
  if (number(f[3]))
    p.track = std::fmod(std::fmod(f[3].as<float>(), 360.0f) + 360.0f, 360.0f);
  if (number(f[15])) {
    const float r = f[15].as<float>();
    p.vrate_fpm = (int16_t) (r > 32000 ? 32000 : r < -32000 ? -32000 : r);
  }
  const char *sq = f[6] | "";
  p.squawk = (uint16_t) (strlen(sq) == 4 ? atoi(sq) : 0);
  p.emergency = emergency_squawk(p.squawk);
  if (!place_plane(p, f[1].as<double>(), f[2].as<double>(), home))
    return false;
  p.tier = ground ? Tier::GROUND : Tier::AIR;
  return true;
}

// ---- Names -------------------------------------------------------------------

const AirlineName *airline_for(const char *callsign) {
  if (!airline_callsign(callsign))
    return nullptr;
  size_t lo = 0, hi = sizeof(kAirlines) / sizeof(kAirlines[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = strncmp(kAirlines[mid].icao, callsign, 3);
    if (c == 0)
      return &kAirlines[mid];
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}

std::string airline_name(const char *callsign) {
  const AirlineName *a = airline_for(callsign);
  return a != nullptr ? a->name : "";
}

std::string flight_number(const char *callsign) {
  const AirlineName *a = airline_for(callsign);
  std::string cs = callsign != nullptr ? callsign : "";
  if (a == nullptr || a->iata[0] == '\0' || cs[3] < '0' || cs[3] > '9')
    return cs;
  size_t at = 3;
  while (at < cs.size() && cs[at] == '0' && at + 1 < cs.size())
    at++;
  return std::string(a->iata) + cs.substr(at);
}

namespace {

// Code, then friendly name, each NUL-ended; packed for flash.
const char kTypes[] =
    "A109\0" "Leonardo AW109\0"
    "A139\0" "Leonardo AW139\0"
    "A169\0" "Leonardo AW169\0"
    "A19N\0" "Airbus A319neo\0"
    "A20N\0" "Airbus A320neo\0"
    "A21N\0" "Airbus A321neo\0"
    "A306\0" "Airbus A300-600\0"
    "A318\0" "Airbus A318\0"
    "A319\0" "Airbus A319\0"
    "A320\0" "Airbus A320\0"
    "A321\0" "Airbus A321\0"
    "A332\0" "Airbus A330-200\0"
    "A333\0" "Airbus A330-300\0"
    "A337\0" "Airbus BelugaXL\0"
    "A338\0" "Airbus A330-800\0"
    "A339\0" "Airbus A330-900\0"
    "A343\0" "Airbus A340-300\0"
    "A346\0" "Airbus A340-600\0"
    "A359\0" "Airbus A350-900\0"
    "A35K\0" "Airbus A350-1000\0"
    "A388\0" "Airbus A380\0"
    "A3ST\0" "Airbus Beluga\0"
    "AS50\0" "Airbus H125\0"
    "AT45\0" "ATR 42-500\0"
    "AT72\0" "ATR 72\0"
    "AT76\0" "ATR 72-600\0"
    "B06\0" "Bell 206\0"
    "B190\0" "Beech 1900\0"
    "B350\0" "King Air 350\0"
    "B37M\0" "Boeing 737 MAX 7\0"
    "B38M\0" "Boeing 737 MAX 8\0"
    "B39M\0" "Boeing 737 MAX 9\0"
    "B3XM\0" "Boeing 737 MAX 10\0"
    "B407\0" "Bell 407\0"
    "B429\0" "Bell 429\0"
    "B52\0" "Boeing B-52\0"
    "B712\0" "Boeing 717\0"
    "B733\0" "Boeing 737-300\0"
    "B734\0" "Boeing 737-400\0"
    "B735\0" "Boeing 737-500\0"
    "B736\0" "Boeing 737-600\0"
    "B737\0" "Boeing 737-700\0"
    "B738\0" "Boeing 737-800\0"
    "B739\0" "Boeing 737-900\0"
    "B744\0" "Boeing 747-400\0"
    "B748\0" "Boeing 747-8\0"
    "B752\0" "Boeing 757-200\0"
    "B753\0" "Boeing 757-300\0"
    "B762\0" "Boeing 767-200\0"
    "B763\0" "Boeing 767-300\0"
    "B764\0" "Boeing 767-400\0"
    "B772\0" "Boeing 777-200\0"
    "B773\0" "Boeing 777-300\0"
    "B778\0" "Boeing 777-8\0"
    "B779\0" "Boeing 777-9\0"
    "B77L\0" "Boeing 777-200LR\0"
    "B77W\0" "Boeing 777-300ER\0"
    "B788\0" "Boeing 787-8\0"
    "B789\0" "Boeing 787-9\0"
    "B78X\0" "Boeing 787-10\0"
    "BCS1\0" "Airbus A220-100\0"
    "BCS3\0" "Airbus A220-300\0"
    "BE20\0" "King Air 200\0"
    "BE36\0" "Beech Bonanza\0"
    "BE58\0" "Beech Baron\0"
    "BE9L\0" "King Air 90\0"
    "BLCF\0" "747 Dreamlifter\0"
    "C130\0" "C-130 Hercules\0"
    "C152\0" "Cessna 152\0"
    "C17\0" "Boeing C-17\0"
    "C172\0" "Cessna 172\0"
    "C182\0" "Cessna 182\0"
    "C208\0" "Cessna Caravan\0"
    "C25A\0" "Citation CJ2\0"
    "C25B\0" "Citation CJ3\0"
    "C25C\0" "Citation CJ4\0"
    "C25M\0" "Citation M2\0"
    "C30J\0" "C-130J Hercules\0"
    "C510\0" "Citation Mustang\0"
    "C525\0" "Citation CJ1\0"
    "C56X\0" "Citation Excel\0"
    "C5M\0" "Lockheed C-5M\0"
    "C680\0" "Cessna Sovereign\0"
    "C68A\0" "Cessna Latitude\0"
    "C700\0" "Cessna Longitude\0"
    "C750\0" "Citation X\0"
    "CL30\0" "Challenger 300\0"
    "CL35\0" "Challenger 350\0"
    "CL60\0" "Challenger 600\0"
    "CRJ2\0" "CRJ200\0"
    "CRJ7\0" "CRJ700\0"
    "CRJ9\0" "CRJ900\0"
    "CRJX\0" "CRJ1000\0"
    "DA40\0" "Diamond DA40\0"
    "DA42\0" "Diamond DA42\0"
    "DH8A\0" "Dash 8-100\0"
    "DH8B\0" "Dash 8-200\0"
    "DH8C\0" "Dash 8-300\0"
    "DH8D\0" "Dash 8-400\0"
    "E135\0" "Embraer ERJ-135\0"
    "E145\0" "Embraer ERJ-145\0"
    "E170\0" "Embraer E170\0"
    "E190\0" "Embraer E190\0"
    "E195\0" "Embraer E195\0"
    "E290\0" "Embraer E190-E2\0"
    "E295\0" "Embraer E195-E2\0"
    "E50P\0" "Phenom 100\0"
    "E55P\0" "Phenom 300\0"
    "E75L\0" "Embraer E175\0"
    "E75S\0" "Embraer E175\0"
    "EC30\0" "Airbus H130\0"
    "EC35\0" "Airbus H135\0"
    "EC45\0" "Airbus H145\0"
    "F2TH\0" "Falcon 2000\0"
    "F900\0" "Falcon 900\0"
    "FA50\0" "Falcon 50\0"
    "FA7X\0" "Falcon 7X\0"
    "FA8X\0" "Falcon 8X\0"
    "G280\0" "Gulfstream G280\0"
    "GA5C\0" "Gulfstream G500\0"
    "GA6C\0" "Gulfstream G600\0"
    "GA7C\0" "Gulfstream G700\0"
    "GL5T\0" "Global 5000\0"
    "GL7T\0" "Global 7500\0"
    "GLEX\0" "Global Express\0"
    "GLF4\0" "Gulfstream IV\0"
    "GLF5\0" "Gulfstream V\0"
    "GLF6\0" "Gulfstream G650\0"
    "H25B\0" "Hawker 800\0"
    "H60\0" "Black Hawk\0"
    "HDJT\0" "HondaJet\0"
    "K35R\0" "Boeing KC-135\0"
    "LJ35\0" "Learjet 35\0"
    "LJ45\0" "Learjet 45\0"
    "LJ60\0" "Learjet 60\0"
    "LJ75\0" "Learjet 75\0"
    "MD11\0" "MD-11\0"
    "MD88\0" "MD-88\0"
    "P28A\0" "Piper Cherokee\0"
    "P28R\0" "Piper Arrow\0"
    "PA34\0" "Piper Seneca\0"
    "PA46\0" "Piper Malibu\0"
    "PC12\0" "Pilatus PC-12\0"
    "PC24\0" "Pilatus PC-24\0"
    "R22\0" "Robinson R22\0"
    "R44\0" "Robinson R44\0"
    "R66\0" "Robinson R66\0"
    "S76\0" "Sikorsky S-76\0"
    "S92\0" "Sikorsky S-92\0"
    "SF50\0" "Cirrus Vision Jet\0"
    "SR20\0" "Cirrus SR20\0"
    "SR22\0" "Cirrus SR22\0"
    "TBM7\0" "Daher TBM 700\0"
    "TBM9\0" "Daher TBM 900\0"
    "V22\0" "V-22 Osprey\0";

const char *type_for(const char *type) {
  if (type == nullptr || type[0] == '\0')
    return nullptr;
  for (const char *p = kTypes; *p != '\0';) {
    const char *full = p + strlen(p) + 1;
    if (strcmp(p, type) == 0)
      return full;
    p = full + strlen(full) + 1;
  }
  return nullptr;
}

}

std::string type_text(const char *type, size_t max) {
  const char *full = type_for(type);
  if (full != nullptr && strlen(full) <= max)
    return full;
  return type != nullptr ? cut(type, max) : "";
}

const char *category_text(const char *category) {
  if (category == nullptr || category[0] == '\0' || category[1] == '\0' || category[2] != '\0')
    return nullptr;
  static const char *const a[] = {nullptr,  "Light plane", "Small plane", "Airliner",
                                  "Airliner", "Heavy jet",   "Fast jet",    "Helicopter"};
  static const char *const b[] = {nullptr, "Glider", "Balloon", "Skydivers", "Ultralight", nullptr, "Drone", "Spacecraft"};
  const int i = category[1] - '0';
  if (i < 0 || i > 7)
    return nullptr;
  return category[0] == 'A' ? a[i] : category[0] == 'B' ? b[i] : nullptr;
}

// ---- Routes ------------------------------------------------------------------

namespace {

bool stop_located(const Stop &s) { return std::isfinite(s.lat) && std::isfinite(s.lon); }
bool plane_located(const Plane &p) { return std::isfinite(p.lat) && std::isfinite(p.lon); }

double detour(const Stop &a, const Stop &b, const Plane &p) {
  return distance_nm(a.lat, a.lon, p.lat, p.lon) + distance_nm(p.lat, p.lon, b.lat, b.lon) -
         distance_nm(a.lat, a.lon, b.lat, b.lon);
}

}

bool route_fits(const Stop &a, const Stop &b, const Plane &p) {
  if (!stop_located(a) || !stop_located(b) || !plane_located(p))
    return false;
  const double ab = distance_nm(a.lat, a.lon, b.lat, b.lon);
  const double slack = std::max(40.0, 0.2 * ab);
  if (detour(a, b, p) > slack)
    return false;
  const double from_a = distance_nm(a.lat, a.lon, p.lat, p.lon), to_b = distance_nm(p.lat, p.lon, b.lat, b.lon);
  if (p.track >= 0 && from_a > 50 && to_b > 50 &&
      angle_diff(p.track, (float) bearing_deg(p.lat, p.lon, b.lat, b.lon)) > 100.0f)
    return false;
  return true;
}

Leg route_leg(const Route &r, const Plane &p) {
  Leg leg;
  if (r.state != RouteState::KNOWN || r.n < 2)
    return leg;
  size_t best = 0;
  bool located = plane_located(p);
  for (size_t i = 0; i < r.n; i++)
    located = located && stop_located(r.stops[i]);
  if (located) {
    double least = 1e18;
    for (size_t i = 0; i + 1 < r.n; i++) {
      const double d = detour(r.stops[i], r.stops[i + 1], p);
      if (d < least) {
        least = d;
        best = i;
      }
    }
  } else if (r.n != 2) {
    return leg;
  }
  const Stop &a = r.stops[best], &b = r.stops[best + 1];
  const bool ok = r.source == RouteSource::ADSBIM ? r.plausible : route_fits(a, b, p);
  if (ok) {
    leg.from = &a;
    leg.to = &b;
  }
  return leg;
}

std::string leg_text(const Leg &l, const char *sep) {
  if (!l.shown())
    return "";
  return std::string(l.from->code) + sep + l.to->code;
}

std::string route_text(const Plane &p, RouteCache *routes, uint32_t now_s, const char *sep) {
  if (p.from[0] != '\0' && p.to[0] != '\0')
    return std::string(p.from) + sep + p.to;
  const Route *r = routes != nullptr ? routes->find(p.callsign, now_s) : nullptr;
  return r != nullptr ? leg_text(route_leg(*r, p), sep) : "";
}

int RouteCache::index_(const char *cs, uint32_t now_s) const {
  if (cs == nullptr || cs[0] == '\0')
    return -1;
  for (size_t i = 0; i < kRouteCache; i++) {
    const Route &r = this->slots[i];
    if (r.state == RouteState::EMPTY || strcmp(r.callsign, cs) != 0)
      continue;
    const uint32_t ttl = r.state == RouteState::KNOWN ? kRouteKnownS : kRouteUnknownS;
    return now_s - r.at_s < ttl ? (int) i : -1;
  }
  return -1;
}

const Route *RouteCache::find(const char *cs, uint32_t now_s) {
  const int i = this->index_(cs, now_s);
  if (i < 0)
    return nullptr;
  this->slots[i].used_s = now_s;
  return &this->slots[i];
}

bool RouteCache::wants(const char *cs, uint32_t now_s) const {
  return airline_callsign(cs) && this->index_(cs, now_s) < 0;
}

Route &RouteCache::put(const char *cs, uint32_t now_s) {
  size_t pick = kRouteCache;
  for (size_t i = 0; i < kRouteCache && pick == kRouteCache; i++)
    if (this->slots[i].state != RouteState::EMPTY && strcmp(this->slots[i].callsign, cs) == 0)
      pick = i;
  for (size_t i = 0; i < kRouteCache && pick == kRouteCache; i++)
    if (this->slots[i].state == RouteState::EMPTY)
      pick = i;
  if (pick == kRouteCache) {
    pick = 0;
    for (size_t i = 1; i < kRouteCache; i++)
      if (now_s - this->slots[i].used_s > now_s - this->slots[pick].used_s)
        pick = i;
  }
  Route &r = this->slots[pick];
  r = Route();
  copy_cut(r.callsign, sizeof(r.callsign), cs);
  r.state = RouteState::UNKNOWN;
  r.at_s = now_s;
  r.used_s = now_s;
  return r;
}

void RouteCache::clear() {
  for (Route &r : this->slots)
    r = Route();
}

RouteAsk route_asks(const Nearby &near, const RouteCache &cache, uint32_t now_s) {
  RouteAsk ask;
  for (size_t i = 0; i < near.n; i++) {
    const Plane &p = near.planes[i];
    if (!plane_located(p) || p.from[0] != '\0' || !cache.wants(p.callsign, now_s))
      continue;
    bool dup = false;
    for (size_t k = 0; k < ask.n && !dup; k++)
      dup = strcmp(ask.callsign[k], p.callsign) == 0;
    if (dup)
      continue;
    copy_cut(ask.callsign[ask.n], sizeof(ask.callsign[0]), p.callsign);
    ask.lat[ask.n] = p.lat;
    ask.lon[ask.n] = p.lon;
    ask.n++;
  }
  return ask;
}

std::string routeset_body(const RouteAsk &ask) {
  std::string out = "{\"planes\":[";
  for (size_t i = 0; i < ask.n; i++) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%s{\"callsign\":\"%s\",\"lat\":%.3f,\"lng\":%.3f}", i ? "," : "", ask.callsign[i],
             ask.lat[i], ask.lon[i]);
    out += buf;
  }
  return out + "]}";
}

std::string adsbdb_url(const char *callsign) {
  char cs[9];
  clean_callsign(callsign, cs);
  return std::string("https://api.adsbdb.com/v0/callsign/") + cs;
}

void fill_routeset_filter(JsonDocument &f) {
  JsonObject e = f.add<JsonObject>();
  e["callsign"] = true;
  e["plausible"] = true;
  e["airport_codes"] = true;
  e["_airport_codes_iata"] = true;
  JsonObject a = e["_airports"].add<JsonObject>();
  for (const char *k : {"iata", "icao", "lat", "lon"})
    a[k] = true;
}

void fill_adsbdb_filter(JsonDocument &f) {
  JsonObject fr = f["response"]["flightroute"].to<JsonObject>();
  for (const char *end : {"origin", "destination"})
    for (const char *k : {"iata_code", "icao_code", "latitude", "longitude"})
      fr[end][k] = true;
}

namespace {

void set_stop(Stop &s, const char *iata, const char *icao, JsonVariantConst lat, JsonVariantConst lon) {
  copy_cut(s.code, sizeof(s.code), iata != nullptr && strlen(iata) == 3 ? iata : icao != nullptr ? icao : "");
  if (number(lat) && number(lon)) {
    s.lat = lat.as<float>();
    s.lon = lon.as<float>();
  }
}

void route_from_entry(JsonObjectConst e, Route &r) {
  r.source = RouteSource::ADSBIM;
  const char *codes = e["_airport_codes_iata"] | (e["airport_codes"] | "unknown");
  if (e.isNull() || strcmp(codes, "unknown") == 0 || codes[0] == '\0')
    return;
  r.n = 0;
  for (JsonObjectConst a : e["_airports"].as<JsonArrayConst>()) {
    if (r.n >= kStops)
      break;
    set_stop(r.stops[r.n], a["iata"] | "", a["icao"] | "", a["lat"], a["lon"]);
    if (r.stops[r.n].code[0] != '\0')
      r.n++;
  }
  if (r.n == 0) {
    std::string all = codes;
    size_t from = 0;
    while (from <= all.size() && r.n < kStops) {
      const size_t dash = all.find('-', from);
      const std::string one = all.substr(from, dash == std::string::npos ? std::string::npos : dash - from);
      if (!one.empty() && one.size() < sizeof(r.stops[0].code))
        copy_cut(r.stops[r.n++].code, sizeof(r.stops[0].code), one.c_str());
      if (dash == std::string::npos)
        break;
      from = dash + 1;
    }
  }
  if (r.n >= 2) {
    r.state = RouteState::KNOWN;
    r.plausible = e["plausible"] | false;
  } else {
    r.n = 0;
  }
}

}

bool routes_from_doc(JsonArrayConst arr, const RouteAsk &ask, RouteCache &cache, uint32_t now_s) {
  if (arr.isNull())
    return false;
  for (size_t i = 0; i < ask.n; i++) {
    JsonObjectConst e = arr[i];
    const char *cs = e["callsign"] | "";
    if (!e.isNull() && strcmp(cs, ask.callsign[i]) != 0) {
      e = JsonObjectConst();
      for (JsonObjectConst x : arr)
        if (strcmp(x["callsign"] | "", ask.callsign[i]) == 0)
          e = x;
    }
    Route &r = cache.put(ask.callsign[i], now_s);
    route_from_entry(e, r);
  }
  return true;
}

bool adsbdb_from_doc(JsonObjectConst root, const char *callsign, RouteCache &cache, uint32_t now_s) {
  if (root.isNull())
    return false;
  char cs[9];
  clean_callsign(callsign, cs);
  Route &r = cache.put(cs, now_s);
  r.source = RouteSource::ADSBDB;
  JsonObjectConst fr = root["response"]["flightroute"];
  if (fr.isNull())
    return true;
  JsonObjectConst o = fr["origin"], d = fr["destination"];
  set_stop(r.stops[0], o["iata_code"] | "", o["icao_code"] | "", o["latitude"], o["longitude"]);
  set_stop(r.stops[1], d["iata_code"] | "", d["icao_code"] | "", d["latitude"], d["longitude"]);
  if (r.stops[0].code[0] != '\0' && r.stops[1].code[0] != '\0') {
    r.n = 2;
    r.state = RouteState::KNOWN;
  }
  return true;
}

void route_unknown(RouteCache &cache, const char *callsign, uint32_t now_s) {
  char cs[9];
  clean_callsign(callsign, cs);
  Route &r = cache.put(cs, now_s);
  r.source = RouteSource::ADSBDB;
}

// ---- Text --------------------------------------------------------------------

std::string alt_text(const Plane &p, AltUnit u, Fmt f) {
  if (p.tier == Tier::GROUND)
    return f == Fmt::FULL ? "On ground" : "GND";
  if (p.alt_ft == kNoAlt)
    return "";
  const long v = u == AltUnit::METERS ? std::lround(p.alt_ft * kFtM) : p.alt_ft;
  const char *unit = u == AltUnit::METERS ? "m" : "ft";
  if (f == Fmt::FULL)
    return with_commas(v) + " " + unit;
  if (f == Fmt::SHORT)
    return std::to_string(v) + unit;
  return with_commas(v);
}

std::string speed_text(const Plane &p, SpeedUnit u, Fmt f) {
  if (p.gs_kt < 0)
    return "";
  const float v = u == SpeedUnit::MPH ? p.gs_kt * kKtMph : u == SpeedUnit::KMH ? p.gs_kt * kKtKmh : p.gs_kt;
  const char *unit = u == SpeedUnit::MPH ? "mph" : u == SpeedUnit::KMH ? "km/h" : "kt";
  const std::string n = std::to_string(std::lround(v));
  if (f == Fmt::FULL)
    return n + " " + unit;
  return f == Fmt::SHORT ? n + unit : n;
}

std::string dist_text(float nm, DistUnit u, Fmt f) {
  if (!(nm >= 0))
    return "";
  const float v = from_nm(nm, u);
  std::string n = v < 9.95f ? fmt("%.1f", v) : fmt("%.0f", v);
  const char *unit = u == DistUnit::MILES ? "mi" : u == DistUnit::KM ? "km" : "nm";
  if (f == Fmt::FULL)
    return n + " " + unit;
  return f == Fmt::SHORT ? n + unit : n;
}

std::string where_text(const Plane &p, DistUnit u, Fmt f) {
  const std::string d = dist_text(p.dist_nm, u, f);
  const char *dir = compass8(p.bearing);
  if (d.empty())
    return "";
  return dir[0] == '\0' ? d : d + " " + dir;
}

const char *climb_text(const Plane &p) {
  if (p.tier == Tier::GROUND)
    return "Taxiing";
  if (p.alt_ft == kNoAlt)
    return "";
  if (p.vrate_fpm >= 500)
    return "Climbing";
  if (p.vrate_fpm <= -500)
    return "Descending";
  return "Level";
}

std::string fit_words(const std::string &s, size_t max) {
  if (s.size() <= max)
    return s;
  std::string out = s;
  while (out.size() > max) {
    const size_t sp = out.rfind(' ');
    if (sp == std::string::npos || sp == 0)
      return out.substr(0, max);
    out.resize(sp);
  }
  return out;
}

// ---- Frames ------------------------------------------------------------------

namespace {

struct Card {
  std::string label, who, sub, tail, type_code, alt_full, speed_full, speed_short, where_full, where_short;
  const char *climb{""};
  const char *cat{nullptr};
  uint32_t label_color{kColCallsign};
  bool route{false};
};

Card card_of(const Plane &p, RouteCache *routes, const Units &u, uint32_t now_s) {
  Card c;
  c.label = plane_label(p);
  c.label_color = p.emergency ? kColAlert : kColCallsign;
  c.who = airline_name(p.callsign);
  const char *cat = category_text(p.category);
  c.cat = cat;
  if (c.who.empty())
    c.who = p.type[0] != '\0' ? type_text(p.type, 16) : cat != nullptr ? cat : "";
  const std::string leg = route_text(p, routes, now_s, "-");
  const std::string fn = p.flight[0] != '\0' ? std::string(p.flight) : flight_number(p.callsign);
  if (p.callsign[0] != '\0' && fn != p.callsign)
    c.tail = fn;
  else if (p.reg[0] != '\0' && c.label != p.reg)
    c.tail = p.reg;
  c.route = !leg.empty();
  c.sub = c.route ? leg : c.tail;
  c.type_code = p.type;
  c.alt_full = alt_text(p, u.alt, Fmt::FULL);
  c.speed_full = speed_text(p, u.speed, Fmt::FULL);
  c.speed_short = speed_text(p, u.speed, Fmt::SHORT);
  c.where_full = where_text(p, u.dist, Fmt::FULL);
  c.where_short = where_text(p, u.dist, Fmt::SHORT);
  c.climb = climb_text(p);
  return c;
}

void put(Frame &f, int x, int y, int w, BoardFont font, Align a, uint32_t color, const std::string &text) {
  if (text.empty() || f.items.size() >= kFrameMax)
    return;
  Text t;
  t.x = (int16_t) x;
  t.y = (int16_t) y;
  t.w = (int16_t) w;
  t.font = font;
  t.align = a;
  t.color = color;
  t.text = cut(text, (size_t) (w / font_px(font)));
  f.items.push_back(t);
}

std::string pick_fit(const std::string &a, const std::string &b, size_t max) { return a.size() <= max ? a : cut(b, max); }

std::string type_fit(const Card &c, size_t max) {
  if (!c.type_code.empty())
    return type_text(c.type_code.c_str(), max);
  return c.cat != nullptr ? fit_words(c.cat, max) : "";
}

void row(Frame &f, const Plane &p, int x0, int y, int w, const Units &u) {
  put(f, x0 + 1, y, 40, BoardFont::S8, Align::LEFT, p.emergency ? kColAlert : kColCallsign, plane_label(p));
  if (w >= 128) {
    put(f, x0 + 43, y, 20, BoardFont::S8, Align::LEFT, kColType, p.type);
    put(f, x0 + 65, y, 30, BoardFont::S8, Align::RIGHT, kColDim, alt_text(p, u.alt, Fmt::BARE));
    put(f, x0 + 99, y, 28, BoardFont::S8, Align::RIGHT, kColDist, dist_text(p.dist_nm, u.dist, Fmt::BARE));
  } else {
    put(f, x0 + 43, y, 20, BoardFont::S8, Align::RIGHT, kColDist, dist_text(p.dist_nm, u.dist, Fmt::BARE));
  }
}

void rows(Frame &f, const Nearby &n, uint8_t card, int x0, int y, int w, int limit, const Units &u) {
  for (uint8_t i = 0; i < n.n && y + 8 <= limit; i++) {
    if (i == card)
      continue;
    row(f, n.planes[i], x0, y, w, u);
    y += 8;
  }
}

std::string count_text(const Nearby &n) {
  const unsigned c = n.tier == Tier::AIR ? n.air : n.tier == Tier::GROUND ? n.ground : n.n;
  return std::to_string(c) + (c == 1 ? " plane" : " planes");
}

void compact(Frame &f, const Card &c, const Nearby &n, const FrameOpts &o) {
  const int w = o.w;
  put(f, 0, 0, w, BoardFont::S16, Align::CENTER, c.label_color, c.label);
  put(f, 0, 15, w, BoardFont::S8, Align::CENTER, kColAirline, fit_words(c.who, (size_t) w / 5));
  put(f, 0, 23, w, BoardFont::S8, Align::CENTER, c.route ? kColRoute : kColDim, c.sub);
  put(f, 1, 31, 25, BoardFont::S8, Align::LEFT, kColType, c.type_code);
  // "1185km/h" too wide: the number alone.
  const std::string bare = c.speed_short.substr(0, c.speed_short.find_first_not_of("0123456789"));
  put(f, 27, 31, w - 28, BoardFont::S8, Align::RIGHT, kColValue, pick_fit(c.speed_short, bare, (size_t) (w - 28) / 5));
  put(f, 0, 39, w, BoardFont::S8, Align::CENTER, kColValue, c.alt_full);
  put(f, 0, 47, w, BoardFont::S8, Align::CENTER, kColDist, pick_fit(c.where_full, c.where_short, (size_t) w / 5));
  if (!o.ticker && n.n > 1)
    put(f, 0, 55, w, BoardFont::S8, Align::CENTER, kColDim, "+" + std::to_string(n.n - 1) + " more");
}

// The list alone, nearest first.
void list_frame(Frame &f, const Nearby &n, const FrameOpts &o) {
  const int lw = o.w >= 128 ? 128 : 64;
  if (lw >= 128) {
    put(f, 1, 0, 60, BoardFont::S8, Align::LEFT, kColDim, "Nearby");
    put(f, 66, 0, 61, BoardFont::S8, Align::RIGHT, kColDim, count_text(n));
  } else {
    put(f, 0, 0, lw, BoardFont::S8, Align::CENTER, kColDim, count_text(n));
  }
  rows(f, n, kNear, 0, 8, lw, o.ticker ? o.h - 8 : o.h, o.units);
}

// The plane's card in the left 128 pixels.
void card_left(Frame &f, const Card &c) {
  put(f, 1, 0, 64, BoardFont::S16, Align::LEFT, c.label_color, c.label);
  put(f, 66, 0, 61, BoardFont::S8, Align::RIGHT, kColAirline, fit_words(c.who, 12));
  if (c.route)
    put(f, 66, 8, 61, BoardFont::S8, Align::RIGHT, kColDim, c.tail);
  put(f, 1, 17, 126, BoardFont::S12, Align::LEFT, c.route ? kColRoute : kColDim, c.sub);
  put(f, 1, 30, 126, BoardFont::S8, Align::LEFT, kColType, type_fit(c, 25));
  put(f, 1, 38, 60, BoardFont::S8, Align::LEFT, kColValue, c.alt_full);
  put(f, 66, 38, 61, BoardFont::S8, Align::RIGHT, kColValue, c.speed_full);
  put(f, 1, 46, 60, BoardFont::S8, Align::LEFT, kColDist, pick_fit(c.where_full, c.where_short, 12));
  put(f, 66, 46, 61, BoardFont::S8, Align::RIGHT, kColDim, c.climb);
}

void card_wide(Frame &f, const Card &c, const Nearby &n, const FrameOpts &o) {
  card_left(f, c);
  const int x0 = 128, lw = o.w - 128;
  if (lw >= 128) {
    put(f, x0 + 1, 0, 60, BoardFont::S8, Align::LEFT, kColDim, "Nearby");
    put(f, x0 + 66, 0, 61, BoardFont::S8, Align::RIGHT, kColDim, count_text(n));
  } else {
    put(f, x0, 0, lw, BoardFont::S8, Align::CENTER, kColDim, count_text(n));
  }
  rows(f, n, o.card, x0, 8, lw >= 128 ? 128 : 64, o.ticker ? o.h - 8 : o.h, o.units);
}

}

Frame flight_frame(const Nearby &near, RouteCache *routes, const FrameOpts &o, uint32_t now_s) {
  Frame f;
  if (near.n == 0)
    return f;
  f.items.reserve(kFrameMax);
  FrameOpts opts = o;
  if (opts.card >= near.n)
    opts.card = 0;
  const Card c = card_of(near.planes[opts.card], routes, opts.units, now_s);
  if (opts.w >= 192)
    card_wide(f, c, near, opts);
  else if (opts.list)
    list_frame(f, near, opts);
  else if (opts.w < 128)
    compact(f, c, near, opts);
  else
    card_left(f, c);
  if (opts.ticker)
    f.ticker = flight_ticker(near, routes, opts, now_s);
  return f;
}

namespace {

void wrap_into(std::vector<std::string> &lines, const std::string &text, size_t max) {
  std::string line;
  size_t at = 0;
  while (at <= text.size()) {
    size_t sp = text.find(' ', at);
    if (sp == std::string::npos)
      sp = text.size();
    const std::string word = text.substr(at, sp - at);
    if (!line.empty() && line.size() + 1 + word.size() > max) {
      lines.push_back(line);
      line.clear();
    }
    line += (line.empty() ? "" : " ") + word;
    at = sp + 1;
  }
  if (!line.empty())
    lines.push_back(line);
}

}

Frame note_frame(Note note, const FrameOpts &o, float radius_nm) {
  std::string text, sub;
  switch (note) {
    case Note::NO_HOME:
      text = "Set your location on the device page";
      break;
    case Note::NONE_NEAR:
      text = "No planes nearby";
      sub = "within " + dist_text(radius_nm, o.units.dist, Fmt::FULL);
      break;
    case Note::NO_DATA:
      text = "Can't reach flight data";
      break;
    default:
      text = "Looking for planes";
      break;
  }
  const size_t max = (size_t) o.w / 5;
  std::vector<std::string> lines;
  wrap_into(lines, text, max);
  const size_t main = lines.size();
  if (!sub.empty())
    wrap_into(lines, sub, max);
  Frame f;
  const int limit = o.ticker ? o.h - 8 : o.h;
  int y = (limit - (int) lines.size() * 8) / 2;
  for (size_t i = 0; i < lines.size(); i++) {
    put(f, 0, y, o.w, BoardFont::S8, Align::CENTER, i < main ? kColValue : kColDim, lines[i]);
    y += 8;
  }
  return f;
}

std::string flight_ticker(const Nearby &near, RouteCache *routes, const FrameOpts &o, uint32_t now_s, size_t planes) {
  if (near.n == 0)
    return "";
  std::string out = count_text(near) + (near.tier == Tier::GROUND ? " on the ground" : " nearby");
  for (size_t k = 0; k < near.n && k < planes; k++) {
    const size_t i = k == 0 ? o.card : k <= o.card ? k - 1 : k;
    if (i >= near.n)
      break;
    const Plane &p = near.planes[i];
    std::string one;
    auto add = [&one](const std::string &s, const char *sep) {
      if (!s.empty())
        one += (one.empty() ? "" : sep) + s;
    };
    if (p.emergency)
      add(emergency_squawk(p.squawk) ? "Emergency " + std::to_string(p.squawk) : "Emergency", " ");
    add(plane_label(p), " ");
    add(airline_name(p.callsign), " ");
    const char *cat = category_text(p.category);
    add(p.type[0] != '\0' ? type_text(p.type, 17) : cat != nullptr ? cat : "", ", ");
    add(route_text(p, routes, now_s, " to "), ", ");
    add(alt_text(p, o.units.alt, Fmt::FULL), ", ");
    add(speed_text(p, o.units.speed, Fmt::FULL), ", ");
    add(where_text(p, o.units.dist, Fmt::FULL), ", ");
    out += " | " + one;
  }
  return out;
}

int text_px(const Text &t) { return (int) t.text.size() * font_px(t.font); }

}
