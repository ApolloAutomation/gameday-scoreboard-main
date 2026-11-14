// Flight Tracker data core; no ESPHome or LVGL includes.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "board_layout.h"
#include "logo_icon.h"
#include "plane_art.h"
#include "poll_timing.h"

namespace flights {

using esphome::gameday::BoardFont;

static const size_t kNear = 8;
static const size_t kRouteCache = 16;
static const size_t kStops = 4;
static const size_t kFrameMax = 44;
static const int32_t kNoAlt = INT32_MIN;
static const float kMaxRadiusNm = 250.0f;
static const float kStaleS = 60.0f;

// ---- Sources and cadence ------------------------------------------------------

enum class Source : uint8_t { ADSBFI = 0, ADSBLOL, LOCAL, FR24 };
static const size_t kSources = 4;

static const char *const kFr24Host = "data-cloud.flightradar24.com";
static const char *const kAdsbFiHost = "opendata.adsb.fi";
static const char *const kAdsbLolHost = "api.adsb.lol";
static const char *const kRoutesetUrl = "http://adsb.im/api/0/routeset";

static const uint32_t kFr24PollMs = 20 * 1000;
static const uint32_t kFiPollMs = 20 * 1000;
static const uint32_t kLocalPollMs = 10 * 1000;
static const uint32_t kLolMinMs = 180 * 1000;  // 500 a day per address
static const uint32_t kLolMaxMs = 24 * 3600 * 1000;
static const uint32_t kRoutesetGapMs = 30 * 1000;  // adsb.im wants batches
static const uint32_t kAdsbdbGapMs = 5 * 1000;
// Ordinary failures in a row before the next source.
static const uint8_t kFailoverStreak = 2;

const char *source_name(Source s);
// The state document's word for it.
const char *source_key(Source s);

struct Home {
  double lat{NAN};
  double lon{NAN};
  float radius_nm{10.0f};
  bool valid() const;
};

struct Bounds {
  double north{0}, south{0}, west{0}, east{0};
};
// A box around the rounded center, covering the radius.
Bounds query_bounds(const Home &h);
// Across the 180th meridian: Flightradar24's box can't wrap.
bool box_wraps(const Home &h);
std::string fr24_url(const Home &h);
// Query center rounded to 0.01 degrees.
std::string adsbfi_url(const Home &h);
std::string adsblol_url(const Home &h);
// Receiver JSON as typed; readsb's API gets circle.
std::string local_url(const std::string &entered, const Home &h);
// Covers the rounded center's offset.
int query_radius_nm(const Home &h);

struct Feed {
  espn::HostWait wait[kSources];
  uint32_t last_ms[kSources]{};
  bool read_once[kSources]{};
  uint32_t lol_gap_ms{kLolMinMs};
};

struct FeedPick {
  Source source{Source::ADSBFI};
  uint32_t in_ms{0};  // 0: read now
};

uint32_t feed_gap_ms(const Feed &f, Source s);
// Out after kFailoverStreak failures or a refusal.
bool feed_out(const Feed &f, Source s, uint32_t now_ms);
// Receiver, Flightradar24, adsb.fi, adsb.lol; each while earlier ones are out.
FeedPick feed_pick(const Feed &f, bool local, uint32_t now_ms, bool fr24 = true);
void feed_started(Feed &f, Source s, uint32_t now_ms);
// status: HTTP status, 0 or less when never answered.
void feed_done(Feed &f, Source s, int status, bool parsed, uint32_t retry_after_ms, uint32_t rnd, uint32_t now_ms);
// adsb.lol's x-ratelimit-remaining and x-ratelimit-reset (seconds).
void feed_lol_limits(Feed &f, const std::string &remaining, const std::string &reset_s);

// ---- Units -------------------------------------------------------------------

enum class AltUnit : uint8_t { FEET = 0, METERS };
enum class SpeedUnit : uint8_t { MPH = 0, KMH, KNOTS };
enum class DistUnit : uint8_t { MILES = 0, KM, NM };

struct Units {
  AltUnit alt{AltUnit::FEET};
  SpeedUnit speed{SpeedUnit::MPH};
  DistUnit dist{DistUnit::MILES};
};
Units units_metric();

float to_nm(float value, DistUnit u);
float from_nm(float nm, DistUnit u);

// ---- Geometry ----------------------------------------------------------------

double distance_nm(double lat1, double lon1, double lat2, double lon2);
// Initial great-circle bearing, 0 to 360.
double bearing_deg(double lat1, double lon1, double lat2, double lon2);
const char *compass8(float deg);
float angle_diff(float a, float b);

// ---- Aircraft ----------------------------------------------------------------

enum class Tier : uint8_t { AIR = 0, GROUND, NOPOS };

struct Plane {
  char hex[7]{};
  char callsign[9]{};
  char reg[9]{};
  char type[5]{};
  char category[3]{};
  char flight[9]{};  // Flightradar24's flight number
  char brand[4]{};   // and the airline painted on it
  char from[5]{};    // and its route, when it has one
  char to[5]{};
  int32_t alt_ft{kNoAlt};
  float gs_kt{-1.0f};
  float track{-1.0f};
  float lat{NAN};
  float lon{NAN};
  float dist_nm{-1.0f};
  float bearing{-1.0f};
  float seen_s{0.0f};
  int16_t vrate_fpm{0};
  uint16_t squawk{0};
  Tier tier{Tier::AIR};
  bool emergency{false};
  bool anon{false};  // a "~" address, not the plane's own
};

struct Nearby {
  Plane planes[kNear];
  uint8_t n{0};
  uint16_t total{0};
  uint16_t air{0};     // airborne within the radius
  uint16_t ground{0};  // on the ground within the radius
  Tier tier{Tier::AIR};
};

// Callsign, else registration, else the hex address.
const char *plane_label(const Plane &p);
// Six hex digits from the plane itself.
bool hex_usable(const Plane &p);
// Uppercase letters and digits only; "" for zeros.
void clean_callsign(const char *in, char out[9]);
// Three letters, then a digit somewhere after.
bool airline_callsign(const char *cs);

void nearby_add(Nearby &n, const Plane &p);
// Keeps only the best tier present.
void nearby_finish(Nearby &n);
// Keeps the shown plane unless another is clearly nearer.
uint8_t pick_card(const Nearby &n, const char *prev_hex);

ArduinoJson::Allocator *&json_allocator_slot();
inline void set_json_allocator(ArduinoJson::Allocator *a) { json_allocator_slot() = a; }
ArduinoJson::Allocator *json_allocator();

void fill_aircraft_filter(JsonDocument &f);
inline bool emergency_squawk(uint16_t sq) { return sq == 7500 || sq == 7600 || sq == 7700; }
// False when the plane is dropped.
bool plane_from_json(JsonObjectConst o, const Home &home, Plane &p);
// One Flightradar24 flight array; now_s dates its position.
bool plane_from_fr24(JsonArrayConst a, const Home &home, int64_t now_s, Plane &p);

namespace detail {

// Any reader as a source: peek holds one character.
template<typename R> class Source {
 public:
  explicit Source(R &r) : r_(r) {}
  int read() {
    if (this->held_ >= 0) {
      const int c = this->held_;
      this->held_ = -1;
      return c;
    }
    return this->r_.read();
  }
  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (int c; i < n && (c = this->read()) >= 0; i++)
      buf[i] = (char) c;
    return i;
  }
  int peek() {
    if (this->held_ < 0)
      this->held_ = this->r_.read();
    return this->held_;
  }
  DeserializationError json(JsonDocument &doc, JsonDocument &filter, uint8_t nesting) {
    return deserializeJson(doc, *this, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(nesting));
  }

 private:
  R &r_;
  int held_{-1};
};

inline bool space(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// The next character past white space, left unread.
template<typename S> int peek_past_space(S &in) {
  int c;
  while (space(c = in.peek()))
    in.read();
  return c;
}

// Reads up to the '[' of root "ac" or "aircraft".
template<typename S> bool seek_aircraft(S &in) {
  int depth = 0;
  bool str = false, esc = false, keyish = false;
  char key[10];
  size_t len = 0;
  for (int c; (c = in.read()) >= 0;) {
    if (str) {
      if (esc) {
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        str = false;
        key[len < sizeof(key) ? len : sizeof(key) - 1] = '\0';
        keyish = depth == 1 && len < sizeof(key);
      } else if (len < sizeof(key)) {
        key[len++] = (char) c;
      }
      continue;
    }
    if (c == '"') {
      str = true;
      len = 0;
      continue;
    }
    if (c == ':' && keyish && (strcmp(key, "ac") == 0 || strcmp(key, "aircraft") == 0) &&
        peek_past_space(in) == '[') {
      in.read();
      return true;
    }
    if (!space(c))
      keyish = false;
    if (c == '{' || c == '[')
      depth++;
    else if (c == '}' || c == ']')
      depth--;
  }
  return false;
}

// After the opening quote: through the closing one.
template<typename S> bool skip_string(S &in) {
  for (int c; (c = in.read()) >= 0;) {
    if (c == '\\')
      in.read();
    else if (c == '"')
      return true;
  }
  return false;
}

// Skips the value starting at the next character.
template<typename S> bool skip_value(S &in) {
  int c = in.read();
  if (c == '"')
    return skip_string(in);
  if (c == '{' || c == '[') {
    for (int depth = 1; depth > 0;) {
      c = in.read();
      if (c < 0 || (c == '"' && !skip_string(in)))
        return false;
      if (c == '{' || c == '[')
        depth++;
      else if (c == '}' || c == ']')
        depth--;
    }
    return true;
  }
  for (;;) {
    c = in.peek();
    if (c < 0)
      return false;
    if (c == ',' || c == '}' || c == ']' || space(c))
      return true;
    in.read();
  }
}

class StrReader {
 public:
  explicit StrReader(const std::string &s) : s_(s) {}
  int read() { return this->at_ < this->s_.size() ? (unsigned char) this->s_[this->at_++] : -1; }
  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (; i < n && this->at_ < this->s_.size(); i++)
      buf[i] = this->s_[this->at_++];
    return i;
  }

 private:
  const std::string &s_;
  size_t at_{0};
};

}

// One plane at a time, so memory stays flat.
template<typename S> bool parse_aircraft_from(S &in, const Home &home, Nearby &out) {
  out = Nearby();
  if (!detail::seek_aircraft(in))
    return false;
  JsonDocument filter(json_allocator());
  fill_aircraft_filter(filter);
  JsonDocument doc(json_allocator());
  for (;;) {
    const int c = detail::peek_past_space(in);
    if (c == ']')
      break;
    if (c == ',') {
      in.read();
      continue;
    }
    if (c != '{' || in.json(doc, filter, 6))
      return false;
    if (out.total < UINT16_MAX)
      out.total++;
    Plane p;
    if (plane_from_json(doc.as<JsonObjectConst>(), home, p))
      nearby_add(out, p);
  }
  nearby_finish(out);
  return true;
}

// Flightradar24's feed: one flight's array at a time.
template<typename S> bool parse_fr24_from(S &in, const Home &home, int64_t now_s, Nearby &out) {
  out = Nearby();
  if (detail::peek_past_space(in) != '{')
    return false;
  in.read();
  JsonDocument filter(json_allocator());
  filter.add(true);
  JsonDocument doc(json_allocator());
  for (;;) {
    int c = detail::peek_past_space(in);
    if (c == '}')
      break;
    if (c == ',') {
      in.read();
      continue;
    }
    if (c != '"')
      return false;
    in.read();
    if (!detail::skip_string(in) || detail::peek_past_space(in) != ':')
      return false;
    in.read();
    if (detail::peek_past_space(in) != '[') {
      if (!detail::skip_value(in))
        return false;
      continue;
    }
    if (in.json(doc, filter, 2))
      return false;
    if (out.total < UINT16_MAX)
      out.total++;
    Plane p;
    if (plane_from_fr24(doc.as<JsonArrayConst>(), home, now_s, p))
      nearby_add(out, p);
  }
  nearby_finish(out);
  return true;
}

// Plain readers, with read() and readBytes().
template<typename TInput, typename = decltype(std::declval<TInput &>().read())>
bool parse_aircraft(TInput &input, const Home &home, Nearby &out) {
  detail::Source<TInput> in(input);
  return parse_aircraft_from(in, home, out);
}

inline bool parse_aircraft(const std::string &json, const Home &home, Nearby &out) {
  detail::StrReader r(json);
  return parse_aircraft(r, home, out);
}

template<typename TInput, typename = decltype(std::declval<TInput &>().read())>
bool parse_fr24(TInput &input, const Home &home, int64_t now_s, Nearby &out) {
  detail::Source<TInput> in(input);
  return parse_fr24_from(in, home, now_s, out);
}

inline bool parse_fr24(const std::string &json, const Home &home, int64_t now_s, Nearby &out) {
  detail::StrReader r(json);
  return parse_fr24(r, home, now_s, out);
}

// ---- Names -------------------------------------------------------------------

struct AirlineName;
const AirlineName *airline_for(const char *callsign);
std::string airline_name(const char *callsign);
// "UA755" for UAL755 when the IATA code is known.
std::string flight_number(const char *callsign);
// "Boeing 737-800", else "737-800", else the code.
std::string type_text(const char *type, size_t max);

// AirlineName::logo: which source answers with a usable logo.
enum : uint8_t { kLogoUnknown = 0, kLogoGoogle = 1, kLogoFlightAware = 2, kLogoNone = 3 };
using ::espn::kGoogleLogoUrl;
static const char *const kFlightAwareLogoUrl = "https://www.flightaware.com/images/airline_logos/90p/";
const AirlineName *airline_by_icao(const char *icao);
// The airline painted on it, else the one flying it.
const AirlineName *logo_airline(const Plane &p);
// Source k's URL, hinted first; 0: no more.
size_t airline_logo_url(const AirlineName &a, uint8_t k, char *out, size_t n);
// The same by ICAO code, as an AppIcon's url.
size_t logo_url(const char *icao, uint8_t kind, uint8_t k, char *out, size_t n);
// Its IATA code when the flight number starts with it.
std::string logo_chip(const AirlineName &a, const std::string &flight_no);
// For a plane with no type: "Helicopter", "Glider".
const char *category_text(const char *category);

// ---- Routes ------------------------------------------------------------------

struct Stop {
  char code[5]{};  // IATA, else ICAO
  float lat{NAN};
  float lon{NAN};
};

enum class RouteState : uint8_t { EMPTY = 0, KNOWN, UNKNOWN };
enum class RouteSource : uint8_t { NONE = 0, ADSBIM, ADSBDB };

struct Route {
  char callsign[9]{};
  Stop stops[kStops];
  uint8_t n{0};
  RouteState state{RouteState::EMPTY};
  RouteSource source{RouteSource::NONE};
  bool plausible{false};  // adsb.im's own judgment
  uint32_t at_s{0};
  uint32_t used_s{0};
};

static const uint32_t kRouteKnownS = 3 * 3600;
static const uint32_t kRouteUnknownS = 3600;

class RouteCache {
 public:
  const Route *find(const char *cs, uint32_t now_s);
  bool wants(const char *cs, uint32_t now_s) const;
  Route &put(const char *cs, uint32_t now_s);
  void clear();
  Route slots[kRouteCache];

 private:
  int index_(const char *cs, uint32_t now_s) const;
};

struct Leg {
  const Stop *from{nullptr};
  const Stop *to{nullptr};
  bool shown() const { return this->from != nullptr && this->to != nullptr; }
};

// Nearest leg, and only when it fits the plane.
Leg route_leg(const Route &r, const Plane &p);
// adsbdb has no plausible flag: check the geometry.
bool route_fits(const Stop &a, const Stop &b, const Plane &p);
std::string leg_text(const Leg &l, const char *sep);
// Flightradar24's route, else a looked-up leg that fits.
std::string route_text(const Plane &p, RouteCache *routes, uint32_t now_s, const char *sep);

struct RouteAsk {
  char callsign[kNear][9]{};
  float lat[kNear]{};
  float lon[kNear]{};
  uint8_t n{0};
};

// Planes with no route of their own and none cached.
RouteAsk route_asks(const Nearby &near, const RouteCache &cache, uint32_t now_s);
std::string routeset_body(const RouteAsk &ask);
std::string adsbdb_url(const char *callsign);

void fill_routeset_filter(JsonDocument &f);
void fill_adsbdb_filter(JsonDocument &f);
bool routes_from_doc(JsonArrayConst arr, const RouteAsk &ask, RouteCache &cache, uint32_t now_s);
bool adsbdb_from_doc(JsonObjectConst root, const char *callsign, RouteCache &cache, uint32_t now_s);
// A 404 from adsbdb.
void route_unknown(RouteCache &cache, const char *callsign, uint32_t now_s);

template<typename S> bool parse_routeset_from(S &in, const RouteAsk &ask, RouteCache &cache, uint32_t now_s) {
  JsonDocument filter(json_allocator());
  fill_routeset_filter(filter);
  JsonDocument doc(json_allocator());
  if (in.json(doc, filter, 5))
    return false;
  return routes_from_doc(doc.as<JsonArrayConst>(), ask, cache, now_s);
}

template<typename S> bool parse_adsbdb_from(S &in, const char *callsign, RouteCache &cache, uint32_t now_s) {
  JsonDocument filter(json_allocator());
  fill_adsbdb_filter(filter);
  JsonDocument doc(json_allocator());
  if (in.json(doc, filter, 5))
    return false;
  return adsbdb_from_doc(doc.as<JsonObjectConst>(), callsign, cache, now_s);
}

template<typename TInput, typename = decltype(std::declval<TInput &>().read())>
bool parse_routeset(TInput &input, const RouteAsk &ask, RouteCache &cache, uint32_t now_s) {
  detail::Source<TInput> in(input);
  return parse_routeset_from(in, ask, cache, now_s);
}

template<typename TInput, typename = decltype(std::declval<TInput &>().read())>
bool parse_adsbdb(TInput &input, const char *callsign, RouteCache &cache, uint32_t now_s) {
  detail::Source<TInput> in(input);
  return parse_adsbdb_from(in, callsign, cache, now_s);
}

inline bool parse_routeset(const std::string &json, const RouteAsk &ask, RouteCache &cache, uint32_t now_s) {
  detail::StrReader r(json);
  return parse_routeset(r, ask, cache, now_s);
}

inline bool parse_adsbdb(const std::string &json, const char *callsign, RouteCache &cache, uint32_t now_s) {
  detail::StrReader r(json);
  return parse_adsbdb(r, callsign, cache, now_s);
}

// ---- Photos ------------------------------------------------------------------

static const char *const kPhotoApi = "https://api.planespotters.net/pub/photos/";
static const size_t kPhotoCache = 16;
static const size_t kPhotoUrlMax = 119;
static const size_t kPhotoByMax = 31;
static const uint32_t kPhotoKnownS = 24 * 3600;
static const uint32_t kPhotoNoneS = 12 * 3600;
static const uint32_t kPhotoGapMs = 5 * 1000;  // the least time between lookups

enum class PhotoState : uint8_t { EMPTY = 0, KNOWN, NONE };

// A plane's photo on Planespotters, or that it has none.
struct PlanePhoto {
  char key[9]{};  // hex, else registration
  char url[kPhotoUrlMax + 1]{};
  char by[kPhotoByMax + 1]{};  // who took the photo
  PhotoState state{PhotoState::EMPTY};
  uint32_t at_s{0};
  uint32_t used_s{0};
};

class PhotoCache {
 public:
  const PlanePhoto *find(const char *key, uint32_t now_s);
  bool wants(const char *key, uint32_t now_s) const;
  PlanePhoto &put(const char *key, uint32_t now_s);
  PlanePhoto slots[kPhotoCache];

 private:
  int index_(const char *key, uint32_t now_s) const;
};

// Its hex, else a registration fit for a URL.
bool photo_key(const Plane &p, char out[9]);
std::string photo_url(const char *key);
// Planespotters asks for a way to reach us.
std::string photo_agent(const std::string &version);
void fill_photo_filter(JsonDocument &f);
// KNOWN or NONE into out; false when unreadable.
bool photo_from_doc(JsonObjectConst root, PlanePhoto &out);

template<typename S> bool parse_photo_from(S &in, PlanePhoto &out) {
  JsonDocument filter(json_allocator());
  fill_photo_filter(filter);
  JsonDocument doc(json_allocator());
  if (in.json(doc, filter, 5))
    return false;
  return photo_from_doc(doc.as<JsonObjectConst>(), out);
}

inline bool parse_photo(const std::string &json, PlanePhoto &out) {
  detail::StrReader r(json);
  detail::Source<detail::StrReader> in(r);
  return parse_photo_from(in, out);
}

// ---- Text --------------------------------------------------------------------

enum class Fmt : uint8_t { FULL = 0, SHORT, BARE };

// "29,000 ft", "29000ft", "29000"; "GND" on the ground.
std::string alt_text(const Plane &p, AltUnit u, Fmt f);
// "472 mph", "472mph", "472".
std::string speed_text(const Plane &p, SpeedUnit u, Fmt f);
// "23 mi", "4.2mi", "4.2".
std::string dist_text(float nm, DistUnit u, Fmt f);
// "23 mi SW".
std::string where_text(const Plane &p, DistUnit u, Fmt f);
const char *climb_text(const Plane &p);
std::string fit_words(const std::string &s, size_t max);

// ---- Frames ------------------------------------------------------------------

static const uint32_t kColCallsign = 0xFFFFFF;
static const uint32_t kColAirline = 0xA0A0A0;
static const uint32_t kColRoute = 0xFFD000;
static const uint32_t kColType = 0x6EC1FF;
static const uint32_t kColValue = 0xFFFFFF;
static const uint32_t kColDim = 0xA0A0A0;
static const uint32_t kColDist = 0x2ED573;
static const uint32_t kColAlert = 0xFF4040;
// The plane picture: a little under the white text.
static const uint32_t kColBody = 0xC8C8C8;
static const uint32_t kColDetail = 0x6E6E6E;
static const uint32_t kColTail = 0x8C8C8C;

enum class Align : uint8_t { LEFT = 0, CENTER, RIGHT };

struct Text {
  int16_t x{0};
  int16_t y{0};
  int16_t w{0};  // the label's width, clipped
  BoardFont font{BoardFont::S8};
  Align align{Align::LEFT};
  uint32_t color{kColValue};
  std::string text;
};

// A plane picture's top left; flip faces it left.
struct Art {
  int16_t x{0}, y{0};
  PlaneKind kind{PlaneKind::NONE};
  bool flip{false};
  uint8_t scale{1};
  uint32_t tail{kColTail};
};

static const int kLogoPx = 16;
// Full screen's card logo.
static const int kLogoBigPx = 32;

// An airline logo's top left and whose.
struct Logo {
  int16_t x{0}, y{0};
  uint8_t px{kLogoPx};
  char icao[4]{};
  int8_t alt{-1};  // items index of the code shown meanwhile
};

// The card's photo; the art shows until it loads.
struct Photo {
  int16_t x{0}, y{0};
  uint8_t w{0}, h{0};  // 0 on the list: credit only
  std::string url;  // empty when there is none
  std::string credit;  // into the ticker at `at`
  size_t at{0};
};

struct Frame {
  std::vector<Text> items;
  std::vector<Art> art;
  std::vector<Logo> logos;
  Photo photo;
  std::string ticker;
};

struct FrameOpts {
  int w{128};
  int h{64};
  bool ticker{true};  // bottom 8 rows kept for it
  Units units;
  uint8_t card{0};  // index into Nearby::planes
  bool list{false};  // under 192 wide: the list, not the card
  const PlanePhoto *photo{nullptr};  // the card plane's
};

// The photo's spot on a card 128 wide.
static const int kPhotoX = 72, kPhotoY = 17, kPhotoW = 56, kPhotoH = 21;
// Full screen's: the panel's width, doubled pixels.
static const int kPhotoTallX = 1, kPhotoTallY = 51, kPhotoTallW = 126, kPhotoTallH = 52;

enum class Note : uint8_t { NO_HOME = 0, NONE_NEAR, NO_DATA, LOADING };

// Under 192 wide the card or the list; wider, both.
// 128x128 (full screen): the tall card or list.
Frame flight_frame(const Nearby &near, RouteCache *routes, const FrameOpts &o, uint32_t now_s);
Frame note_frame(Note note, const FrameOpts &o, float radius_nm);
// card_end: where the card plane's words end.
std::string flight_ticker(const Nearby &near, RouteCache *routes, const FrameOpts &o, uint32_t now_s,
                          size_t planes = 3, size_t *card_end = nullptr);
// Text width in pixels at the font's advance.
int text_px(const Text &t);

}
