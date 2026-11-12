// Flight Tracker core against recorded flight data answers.
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "flight_airlines.h"
#include "flights.h"

using namespace flights;
using esphome::gameday::font_desc_bottom;
using esphome::gameday::font_ink_top;

namespace {

int checks = 0;
int failures = 0;

#define FCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

#define FCHECK_EQ(a, b)                                                                               \
  do {                                                                                                \
    checks++;                                                                                         \
    const std::string va = (a), vb = (b);                                                             \
    if (va != vb) {                                                                                   \
      failures++;                                                                                     \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                                 \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

Home jfk(float radius_nm = 25) {
  Home h;
  h.lat = 40.64;
  h.lon = -73.78;
  h.radius_nm = radius_nm;
  return h;
}

Home ord(float radius_nm = 10) {
  Home h;
  h.lat = 41.98;
  h.lon = -87.90;
  h.radius_nm = radius_nm;
  return h;
}

Nearby read(const char *file, const Home &h) {
  Nearby n;
  FCHECK(parse_aircraft(slurp(file), h, n), "%s parses", file);
  return n;
}

// The fixture's plane with this callsign, read alone.
Plane plane_named(const char *file, const char *cs, const Home &h) {
  JsonDocument doc;
  deserializeJson(doc, slurp(file));
  for (JsonObjectConst o : doc["ac"].as<JsonArrayConst>()) {
    char c[9];
    clean_callsign(o["flight"] | "", c);
    Plane p;
    if (strcmp(c, cs) == 0 && plane_from_json(o, h, p))
      return p;
  }
  FCHECK(false, "%s has %s", file, cs);
  return Plane();
}

int index_of(const Nearby &n, const char *label) {
  for (int i = 0; i < n.n; i++)
    if (strcmp(plane_label(n.planes[i]), label) == 0)
      return i;
  return -1;
}

void test_adsbfi() {
  const Nearby n = read("fixtures/flights_adsbfi_jfk.json", jfk());
  FCHECK(n.total == 57, "57 aircraft listed: %u", n.total);
  FCHECK(n.air == 23 && n.ground == 34, "23 up, 34 down: %u, %u", n.air, n.ground);
  FCHECK(n.n == kNear && n.tier == Tier::AIR, "the nearest 8 airborne: %u", n.n);
  const char *want[] = {"JBU1407", "LOT5N", "N77NY", "N24NY", "N7QD", "UAL2495", "JBU416", "CNS482"};
  for (size_t i = 0; i < kNear; i++)
    FCHECK_EQ(plane_label(n.planes[i]), want[i]);
  for (size_t i = 1; i < n.n; i++)
    FCHECK(n.planes[i - 1].dist_nm <= n.planes[i].dist_nm, "nearest first at %zu", i);
  const Plane &p = n.planes[0];
  FCHECK(std::fabs(p.dist_nm - 1.267f) < 0.05f, "JBU1407 1.267 nm: %f", p.dist_nm);
  FCHECK(std::fabs(p.bearing - 148.6f) < 0.5f, "JBU1407 SSE: %f", p.bearing);
  FCHECK_EQ(p.type, "A21N");
  FCHECK_EQ(p.reg, "N4062J");
  FCHECK_EQ(p.hex, "a4c592");
  FCHECK(p.alt_ft == 1525 && std::fabs(p.gs_kt - 176.8f) < 0.01f, "JBU1407 1525 ft, 176.8 kt");
  FCHECK(p.vrate_fpm > 500 && !p.emergency, "climbing out, no emergency");
  FCHECK_EQ(p.category, "A3");

  // Every listed dst and dir agree with the haversine.
  JsonDocument doc;
  deserializeJson(doc, slurp("fixtures/flights_adsbfi_jfk.json"));
  int seen = 0;
  for (JsonObjectConst o : doc["ac"].as<JsonArrayConst>()) {
    const double d = distance_nm(40.64, -73.78, o["lat"].as<double>(), o["lon"].as<double>());
    const double b = bearing_deg(40.64, -73.78, o["lat"].as<double>(), o["lon"].as<double>());
    FCHECK(std::fabs(d - o["dst"].as<double>()) < 0.05 + 0.002 * d, "dst %f vs %f", d, o["dst"].as<double>());
    FCHECK(angle_diff((float) b, o["dir"].as<float>()) < 0.5f || d < 0.3, "dir %f vs %f", b, o["dir"].as<double>());
    seen++;
  }
  FCHECK(seen == 57, "every plane compared");
}

void test_ground_only() {
  const Nearby n = read("fixtures/flights_adsbfi_jfk.json", jfk(1));
  FCHECK(n.air == 0 && n.ground == 14, "within a mile all 14 are on the ground: %u %u", n.air, n.ground);
  FCHECK(n.n == kNear && n.tier == Tier::GROUND, "ground planes when nothing flies");
  FCHECK_EQ(plane_label(n.planes[0]), "N3240J");
  FCHECK(n.planes[0].callsign[0] == '\0', "00000000 is no callsign");
  FCHECK(index_of(n, "N950JT") >= 0, ".N950JT cleans up");
  const Nearby wide = read("fixtures/flights_adsbfi_jfk.json", jfk(2));
  FCHECK(wide.tier == Tier::AIR && wide.n == 1, "one plane up within two miles drops the ground");
  FCHECK_EQ(plane_label(wide.planes[0]), "JBU1407");
}

void test_other_sources() {
  const Nearby e = read("fixtures/flights_adsbfi_empty.json", jfk());
  FCHECK(e.total == 0 && e.n == 0, "an empty sky");
  const Nearby lol = read("fixtures/flights_adsblol_jfk.json", jfk());
  FCHECK(lol.total == 55 && lol.n == kNear && lol.tier == Tier::AIR, "adsb.lol reads the same: %u", lol.total);
  FCHECK_EQ(plane_label(lol.planes[0]), "DAL8846");
  const Nearby r = read("fixtures/flights_readsb_ord.json", ord());
  FCHECK(r.total == 26 && r.n > 0, "readsb's aircraft key: %u", r.total);

  // tar1090 and PiAware: no dst, dir, type or registration.
  std::string bare = slurp("fixtures/flights_readsb_ord.json");
  bare = std::regex_replace(bare, std::regex(",\"(dst|dir)\":[-0-9.]+"), "");
  bare = std::regex_replace(bare, std::regex("\"(t|r|desc)\":\"[^\"]*\","), "");
  FCHECK(bare.find("\"dst\"") == std::string::npos && bare.find("\"t\":") == std::string::npos, "stripped");
  Nearby b;
  FCHECK(parse_aircraft(bare, ord(), b), "tar1090's shape parses");
  FCHECK(b.n == r.n && b.air == r.air, "the same planes");
  for (size_t i = 0; i < b.n; i++) {
    FCHECK_EQ(b.planes[i].hex, r.planes[i].hex);
    FCHECK(std::fabs(b.planes[i].dist_nm - r.planes[i].dist_nm) < 1e-4f, "same distance");
    FCHECK(b.planes[i].type[0] == '\0', "no type");
  }

  // Old dump1090: altitude and speed.
  std::string old = slurp("fixtures/flights_adsbfi_jfk.json");
  old = std::regex_replace(old, std::regex("\"alt_baro\""), "\"altitude\"");
  old = std::regex_replace(old, std::regex("\"alt_geom\":[-0-9]+,"), "");
  old = std::regex_replace(old, std::regex("\"gs\""), "\"speed\"");
  Nearby o;
  FCHECK(parse_aircraft(old, jfk(), o), "old keys parse");
  FCHECK(o.n == kNear && o.planes[0].alt_ft == 1525 && std::fabs(o.planes[0].gs_kt - 176.8f) < 0.01f,
         "old keys give altitude and speed");
  FCHECK(o.ground == 34, "ground from altitude");
}

void test_streaming() {
  const std::string json = slurp("fixtures/flights_adsbfi_jfk.json");
  Nearby n;
  FCHECK(!parse_aircraft(json.substr(0, json.size() / 2), jfk(), n), "a cut-off read fails");
  FCHECK(!parse_aircraft(std::string("{\"msg\":\"ac\",\"now\":1}"), jfk(), n), "no array, no read");
  // A nested "ac" is not the list.
  std::string nested = "{\"x\":{\"ac\":[{\"hex\":\"000001\"}]},\"y\":\"ac\"," + json.substr(1);
  FCHECK(parse_aircraft(nested, jfk(), n) && n.total == 57, "skips the nested one: %u", n.total);
  // Ten copies of every plane: still eight kept.
  const size_t a = json.find('['), z = json.rfind(']');
  const std::string items = json.substr(a + 1, z - a - 1);
  std::string big = json.substr(0, a + 1);
  for (int i = 0; i < 10; i++)
    big += (i ? "," : "") + items;
  big += json.substr(z);
  FCHECK(parse_aircraft(big, jfk(), n) && n.total == 570 && n.n == kNear, "570 planes in: %u", n.total);
  FCHECK_EQ(plane_label(n.planes[7]), "CNS482");
  FCHECK_EQ(plane_label(n.planes[1]), "LOT5N");

  // Ground vehicles and stale ones are dropped.
  std::string veh = std::regex_replace(json, std::regex("\"category\":\"A3\""), "\"category\":\"C1\"");
  FCHECK(parse_aircraft(veh, jfk(), n) && index_of(n, "JBU1407") < 0 && index_of(n, "LOT5N") == 0,
         "C categories are not planes");
  std::string stale = std::regex_replace(json, std::regex("\"seen\":0.0,\"rssi\":-15.3"), "\"seen\":61.0,\"rssi\":-15.3");
  FCHECK(parse_aircraft(stale, jfk(), n) && n.total == 57, "stale parses");
  Plane p;
  JsonDocument d;
  deserializeJson(d, "{\"hex\":\"~abc123\",\"flight\":\"TEST1  \",\"seen\":1.0}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && p.tier == Tier::NOPOS, "no position");
  FCHECK_EQ(p.hex, "abc123");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"seen_pos\":90,\"seen\":1}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && p.tier == Tier::NOPOS, "an old position is none");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"seen\":75}");
  FCHECK(!plane_from_json(d.as<JsonObjectConst>(), jfk(), p), "unheard for 75 s");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"squawk\":\"7700\",\"alt_baro\":5000}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && p.emergency && p.squawk == 7700, "7700");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":41.7,\"lon\":-73.8}");
  FCHECK(!plane_from_json(d.as<JsonObjectConst>(), jfk(), p), "outside the radius");
  Home none;
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":41.7,\"lon\":-73.8}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), none, p) && p.dist_nm < 0, "no home, no distance");
}

void test_nopos_order() {
  Nearby n;
  for (float seen : {5.0f, 1.0f, 3.0f}) {
    Plane p;
    p.tier = Tier::NOPOS;
    p.seen_s = seen;
    nearby_add(n, p);
  }
  nearby_finish(n);
  FCHECK(n.n == 3 && n.tier == Tier::NOPOS && n.planes[0].seen_s == 1.0f && n.planes[2].seen_s == 5.0f,
         "heard most recently first");
}

void test_names() {
  FCHECK_EQ(airline_name("UAL755"), "United");
  FCHECK_EQ(airline_name("DAL2064"), "Delta");
  FCHECK_EQ(airline_name("JBU1407"), "JetBlue");
  FCHECK_EQ(airline_name("EDVMX480"), "Endeavor Air");
  FCHECK_EQ(airline_name("EJA871"), "NetJets");
  FCHECK_EQ(airline_name("FIN16"), "Finnair");
  FCHECK_EQ(airline_name("N1103S"), "");
  FCHECK_EQ(airline_name("GEZBC"), "");
  FCHECK_EQ(airline_name("ZZZ999"), "");
  FCHECK_EQ(flight_number("UAL755"), "UA755");
  FCHECK_EQ(flight_number("BAW0117"), "BA117");
  FCHECK_EQ(flight_number("EDVMX480"), "EDVMX480");
  FCHECK_EQ(flight_number("N1103S"), "N1103S");
  const size_t count = sizeof(kAirlines) / sizeof(kAirlines[0]);
  FCHECK(count >= 300, "a few hundred airlines: %zu", count);
  for (size_t i = 0; i < count; i++) {
    FCHECK(strlen(kAirlines[i].icao) == 3 && kAirlines[i].name[0] != '\0', "%s complete", kAirlines[i].icao);
    FCHECK(i == 0 || strcmp(kAirlines[i - 1].icao, kAirlines[i].icao) < 0, "%s sorted", kAirlines[i].icao);
    std::string cs = std::string(kAirlines[i].icao) + "1";
    const AirlineName *a = airline_for(cs.c_str());
    FCHECK(a != nullptr && strcmp(a->icao, kAirlines[i].icao) == 0, "%s found", kAirlines[i].icao);
    for (const char *c = kAirlines[i].name; *c != '\0'; c++)
      FCHECK((unsigned char) *c >= 0x20 && (unsigned char) *c < 0x7F, "%s ASCII", kAirlines[i].icao);
  }
  FCHECK_EQ(type_text("B738", 25), "Boeing 737-800");
  FCHECK_EQ(type_text("B738", 12), "737-800");
  FCHECK_EQ(type_text("B38M", 4), "B38M");
  FCHECK_EQ(type_text("ZZZZ", 12), "ZZZZ");
  FCHECK_EQ(type_text("", 12), "");
  FCHECK_EQ(category_text("A7"), "Helicopter");
  FCHECK_EQ(category_text("B1"), "Glider");
  FCHECK(category_text("A") == nullptr && category_text("C1") == nullptr && category_text("") == nullptr, "no word");

  char cs[9];
  clean_callsign("ual755  ", cs);
  FCHECK_EQ(cs, "UAL755");
  clean_callsign("@@@@@@@@", cs);
  FCHECK_EQ(cs, "");
  clean_callsign("00000000", cs);
  FCHECK_EQ(cs, "");
  clean_callsign("ABCDEFGHIJ", cs);
  FCHECK_EQ(cs, "ABCDEFGH");
  FCHECK(airline_callsign("LOT5N") && airline_callsign("EDVMX480") && !airline_callsign("N7QD") &&
             !airline_callsign("GEZBC") && !airline_callsign("") && !airline_callsign("AB1"),
         "airline-shaped callsigns");
}

void test_units() {
  Plane p;
  p.alt_ft = 29000;
  p.gs_kt = 471.7f;
  p.dist_nm = 22.97f;
  p.bearing = 234.4f;
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::FULL), "29,000 ft");
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::SHORT), "29000ft");
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::BARE), "29,000");
  FCHECK_EQ(alt_text(p, AltUnit::METERS, Fmt::FULL), "8,839 m");
  FCHECK_EQ(speed_text(p, SpeedUnit::MPH, Fmt::FULL), "543 mph");
  FCHECK_EQ(speed_text(p, SpeedUnit::KMH, Fmt::SHORT), "874km/h");
  FCHECK_EQ(speed_text(p, SpeedUnit::KNOTS, Fmt::FULL), "472 kt");
  FCHECK_EQ(where_text(p, DistUnit::MILES, Fmt::FULL), "26 mi SW");
  FCHECK_EQ(where_text(p, DistUnit::KM, Fmt::SHORT), "43km SW");
  FCHECK_EQ(dist_text(1.267f, DistUnit::MILES, Fmt::FULL), "1.5 mi");
  FCHECK_EQ(dist_text(1.267f, DistUnit::KM, Fmt::BARE), "2.3");
  FCHECK_EQ(dist_text(8.66f, DistUnit::NM, Fmt::FULL), "8.7 nm");
  FCHECK_EQ(dist_text(9.96f, DistUnit::NM, Fmt::FULL), "10 nm");
  FCHECK_EQ(dist_text(-1, DistUnit::NM, Fmt::FULL), "");
  FCHECK_EQ(climb_text(p), "Level");
  p.vrate_fpm = -1200;
  FCHECK_EQ(climb_text(p), "Descending");
  p.alt_ft = 120000;
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::FULL), "120,000 ft");
  p.alt_ft = -150;
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::BARE), "-150");
  p.tier = Tier::GROUND;
  FCHECK_EQ(alt_text(p, AltUnit::FEET, Fmt::FULL), "On ground");
  FCHECK_EQ(alt_text(p, AltUnit::METERS, Fmt::BARE), "GND");
  p = Plane();
  FCHECK(alt_text(p, AltUnit::FEET, Fmt::FULL).empty() && speed_text(p, SpeedUnit::MPH, Fmt::FULL).empty() &&
             where_text(p, DistUnit::MILES, Fmt::FULL).empty(),
         "unknowns are blank");
  FCHECK(std::fabs(to_nm(10, DistUnit::MILES) - 8.69f) < 0.01f && std::fabs(to_nm(10, DistUnit::KM) - 5.40f) < 0.01f &&
             to_nm(10, DistUnit::NM) == 10,
         "radius to nautical miles");
  FCHECK(std::fabs(from_nm(to_nm(25, DistUnit::KM), DistUnit::KM) - 25) < 1e-4f, "round trip");
  FCHECK(units_metric().alt == AltUnit::METERS && units_metric().speed == SpeedUnit::KMH, "metric");
}

void test_geometry() {
  const double d = distance_nm(40.6398, -73.7789, 33.9425, -118.4081);
  FCHECK(std::fabs(d - 2146) < 5, "JFK to LAX about 2,146 nm: %f", d);
  const double b = bearing_deg(40.6398, -73.7789, 33.9425, -118.4081);
  FCHECK(std::fabs(b - 274) < 1.5, "heading west: %f", b);
  FCHECK(distance_nm(10, 20, 10, 20) == 0, "zero");
  FCHECK(std::fabs(bearing_deg(0, 0, 1, 0)) < 1e-9 && std::fabs(bearing_deg(0, 0, 0, 1) - 90) < 1e-9, "N and E");
  FCHECK_EQ(compass8(0), "N");
  FCHECK_EQ(compass8(22.4f), "N");
  FCHECK_EQ(compass8(22.6f), "NE");
  FCHECK_EQ(compass8(359.9f), "N");
  FCHECK_EQ(compass8(274.3f), "W");
  FCHECK_EQ(compass8(234.4f), "SW");
  FCHECK_EQ(compass8(-1), "");
  FCHECK(std::fabs(angle_diff(350, 10) - 20) < 1e-4f && std::fabs(angle_diff(90, 270) - 180) < 1e-4f, "angles");
}

void test_urls() {
  Home h;
  h.lat = 40.6413;
  h.lon = -73.7781;
  h.radius_nm = 10;
  FCHECK(h.valid(), "a home");
  FCHECK_EQ(adsbfi_url(h), "https://opendata.adsb.fi/api/v3/lat/40.64/lon/-73.78/dist/11");
  FCHECK_EQ(adsblol_url(h), "https://api.adsb.lol/v2/point/40.64/-73.78/11");
  h.radius_nm = 300;
  FCHECK(query_radius_nm(h) == 250, "capped at 250");
  h.radius_nm = 0.2f;
  FCHECK(query_radius_nm(h) == 2, "at least the rounding");
  h.lat = -0.001;
  h.lon = 0.004;
  FCHECK_EQ(adsbfi_url(h), "https://opendata.adsb.fi/api/v3/lat/0.00/lon/0.00/dist/2");
  Home zero;
  zero.lat = 0;
  zero.lon = 0;
  FCHECK(!zero.valid() && !Home().valid(), "unset homes");
  h = jfk(10);
  FCHECK_EQ(local_url("192.168.1.50", h), "http://192.168.1.50/tar1090/data/aircraft.json");
  FCHECK_EQ(local_url(" http://pi.local/ ", h), "http://pi.local/tar1090/data/aircraft.json");
  FCHECK_EQ(local_url("http://pi.local/skyaware/data/aircraft.json", h), "http://pi.local/skyaware/data/aircraft.json");
  FCHECK_EQ(local_url("pi.local:30152", h), "http://pi.local:30152/?circle=40.6400,-73.7800,11");
  FCHECK_EQ(local_url("http://pi.local/re-api/", h), "http://pi.local/re-api/?circle=40.6400,-73.7800,11");
  FCHECK_EQ(local_url("http://pi.local/re-api/?circle=1,2,3&jv2", h),
            "http://pi.local/re-api/?circle=40.6400,-73.7800,11&jv2");
  FCHECK_EQ(local_url("http://pi.local/re-api/?all", h), "http://pi.local/re-api/?all&circle=40.6400,-73.7800,11");
  FCHECK_EQ(local_url("   ", h), "");
}

void test_feed() {
  Feed f;
  FeedPick p = feed_pick(f, false, 1000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == 0, "adsb.fi first");
  feed_started(f, Source::ADSBFI, 1000);
  feed_done(f, Source::ADSBFI, 200, true, 0, 0, 1500);
  p = feed_pick(f, false, 2000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == kFiPollMs - 1000, "every 20 s: %u", p.in_ms);
  feed_started(f, Source::ADSBFI, 21000);
  feed_done(f, Source::ADSBFI, -1, false, 0, 7, 21500);
  p = feed_pick(f, false, 22000);
  FCHECK(p.source == Source::ADSBLOL && p.in_ms == 0, "adsb.lol while adsb.fi backs off");
  feed_started(f, Source::ADSBLOL, 22000);
  feed_done(f, Source::ADSBLOL, 200, true, 0, 0, 22500);
  p = feed_pick(f, false, 23000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms > 0 && p.in_ms <= 30000, "back to adsb.fi soon: %u", p.in_ms);
  feed_started(f, Source::ADSBFI, 60000);
  feed_done(f, Source::ADSBFI, 429, false, 120000, 0, 60500);
  p = feed_pick(f, false, 61000);
  FCHECK(p.source == Source::ADSBLOL && p.in_ms == kLolMinMs - 39000, "adsb.lol at its pace: %u", p.in_ms);
  FCHECK(f.wait[0].left(61000) >= 120000, "Retry-After honored: %u", f.wait[0].left(61000));
  feed_done(f, Source::ADSBFI, 200, true, 0, 0, 62000);
  p = feed_pick(f, false, 62000);
  FCHECK(p.source == Source::ADSBFI, "a good read ends the wait");

  Feed l;
  p = feed_pick(l, true, 0);
  FCHECK(p.source == Source::LOCAL && p.in_ms == 0, "the owner's receiver first");
  feed_started(l, Source::LOCAL, 0);
  feed_done(l, Source::LOCAL, -1, false, 0, 0, 100);
  p = feed_pick(l, true, 200);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == 0, "adsb.fi while it is down");
  feed_done(l, Source::LOCAL, 200, true, 0, 0, 300);
  p = feed_pick(l, true, kLocalPollMs);
  FCHECK(p.source == Source::LOCAL && p.in_ms == 0, "every 10 s");

  feed_lol_limits(f, "498", "72122");
  FCHECK(f.lol_gap_ms == kLolMinMs, "plenty left: %u", f.lol_gap_ms);
  feed_lol_limits(f, "10", "3600");
  FCHECK(f.lol_gap_ms == 360000, "spread over the window: %u", f.lol_gap_ms);
  feed_lol_limits(f, "0", "3600");
  FCHECK(f.lol_gap_ms == 3600000, "none left: wait for the reset");
  feed_lol_limits(f, "x", "3600");
  FCHECK(f.lol_gap_ms == 3600000, "garbage ignored");
}

RouteAsk recorded_ask() {
  // The request that recorded flights_routeset_jfk.json.
  const char *cs[] = {"JBU1407", "LOT5N", "UAL2495", "JBU416", "CNS482", "AAL1070", "UAL1263", "UPS1125"};
  const float lat[] = {40.622f, 40.628f, 40.799f, 40.463f, 40.694f, 40.645f, 40.858f, 40.736f};
  const float lon[] = {-73.765f, -73.672f, -73.859f, -73.818f, -74.021f, -74.051f, -73.811f, -73.49f};
  RouteAsk a;
  for (size_t i = 0; i < 8; i++) {
    strcpy(a.callsign[i], cs[i]);
    a.lat[i] = lat[i];
    a.lon[i] = lon[i];
  }
  a.n = 8;
  return a;
}

RouteAsk ask_of(std::initializer_list<const char *> cs) {
  RouteAsk a;
  for (const char *c : cs)
    strcpy(a.callsign[a.n++], c);
  return a;
}

void test_routeset() {
  const char *file = "fixtures/flights_adsbfi_jfk.json";
  const Home h = jfk();
  RouteCache cache;
  const Nearby n = read(file, h);
  const RouteAsk want = route_asks(n, cache, 100);
  FCHECK(want.n == 5, "airline callsigns among the nearest: %u", want.n);
  FCHECK_EQ(want.callsign[0], "JBU1407");
  FCHECK_EQ(want.callsign[4], "CNS482");
  JsonDocument body;
  FCHECK(!deserializeJson(body, routeset_body(want)) && body["planes"].size() == 5, "the body is JSON");
  FCHECK_EQ(body["planes"][0]["callsign"].as<std::string>(), "JBU1407");
  FCHECK(std::fabs(body["planes"][0]["lng"].as<float>() - -73.765f) < 0.001f, "lng is the longitude");

  const RouteAsk ask = recorded_ask();
  FCHECK(parse_routeset(slurp("fixtures/flights_routeset_jfk.json"), ask, cache, 100), "routeset parses");
  const Route *r = cache.find("JBU1407", 100);
  FCHECK(r != nullptr && r->state == RouteState::KNOWN && r->plausible && r->n == 2, "JBU1407 known");
  FCHECK_EQ(leg_text(route_leg(*r, plane_named(file, "JBU1407", h)), " > "), "JFK > CDG");
  r = cache.find("UAL1263", 100);
  FCHECK(r != nullptr && r->state == RouteState::KNOWN && !r->plausible, "UAL1263's route is implausible");
  FCHECK(!route_leg(*r, plane_named(file, "UAL1263", h)).shown(), "so it isn't shown");
  r = cache.find("CNS482", 100);
  FCHECK(r != nullptr && r->state == RouteState::UNKNOWN, "PlaneSense has none");
  FCHECK(!cache.wants("CNS482", 100) && !cache.wants("JBU1407", 200), "both cached");
  FCHECK(route_asks(n, cache, 200).n == 0, "nothing more to ask");
  FCHECK_EQ(leg_text(route_leg(*cache.find("UPS1125", 100), plane_named(file, "UPS1125", h)), "-"), "JFK-RFD");
  FCHECK(std::fabs(cache.find("UPS1125", 100)->stops[1].lat - 42.195f) < 0.01f, "stops keep coordinates");

  RouteCache c2;
  FCHECK(parse_routeset(slurp("fixtures/flights_routeset_unknown.json"),
                        ask_of({"N1103S", "ZZZ999", "EJA871", "UAL5", "JBU416"}), c2, 100),
         "unknowns and a null parse");
  FCHECK(c2.find("EJA871", 100) != nullptr && c2.find("EJA871", 100)->state == RouteState::UNKNOWN, "null is unknown");
  FCHECK(c2.find("ZZZ999", 100)->state == RouteState::UNKNOWN, "unknown");
  const Route *u5 = c2.find("UAL5", 100);
  FCHECK(u5 != nullptr && u5->state == RouteState::KNOWN && u5->plausible, "UAL5 known");
  FCHECK_EQ(std::string(u5->stops[0].code) + "-" + u5->stops[1].code, "IAH-LHR");

  RouteCache c3;
  FCHECK(parse_routeset(slurp("fixtures/flights_routeset_legs.json"), ask_of({"UAL755", "DAL2064", "FIN16"}), c3, 100),
         "legs parse");
  const Route *u755 = c3.find("UAL755", 100);
  FCHECK(u755 != nullptr && u755->n == 4, "four stops: %u", u755 != nullptr ? u755->n : 0);
  FCHECK_EQ(leg_text(route_leg(*u755, plane_named(file, "UAL755", h)), " > "), "IAD > BOS");

  RouteCache c4;
  FCHECK(parse_routeset(slurp("fixtures/flights_routeset_far.json"),
                        ask_of({"JBU416", "UPS1125", "AAL1070", "SWA1234"}), c4, 100),
         "far parses");
  FCHECK(!c4.find("JBU416", 100)->plausible && !c4.find("SWA1234", 100)->plausible, "adsb.im says no");
  FCHECK(!route_leg(*c4.find("JBU416", 100), plane_named(file, "JBU416", h)).shown(), "not shown");

  // Answers out of order match by callsign.
  RouteCache c5;
  FCHECK(parse_routeset(slurp("fixtures/flights_routeset_legs.json"), ask_of({"FIN16", "UAL755"}), c5, 100), "parses");
  FCHECK_EQ(std::string(c5.find("FIN16", 100)->stops[1].code), "HEL");
  FCHECK(c5.find("UAL755", 100)->n == 4, "by callsign");
  FCHECK(!parse_routeset(std::string("{\"error\":1}"), ask_of({"FIN16"}), c5, 100), "not an array");
}

void test_adsbdb() {
  const char *file = "fixtures/flights_adsbfi_jfk.json";
  const Home h = jfk();
  FCHECK_EQ(adsbdb_url("ual755 "), "https://api.adsbdb.com/v0/callsign/UAL755");
  RouteCache cache;
  FCHECK(parse_adsbdb(slurp("fixtures/flights_adsbdb_ual755.json"), "UAL755", cache, 10), "adsbdb parses");
  const Route *r = cache.find("UAL755", 10);
  FCHECK(r != nullptr && r->state == RouteState::KNOWN && r->source == RouteSource::ADSBDB && r->n == 2, "known");
  FCHECK_EQ(leg_text(route_leg(*r, plane_named(file, "UAL755", h)), " > "), "IAD > BOS");
  FCHECK(parse_adsbdb(slurp("fixtures/flights_adsbdb_aal1070.json"), "AAL1070", cache, 10), "parses");
  r = cache.find("AAL1070", 10);
  FCHECK(r != nullptr && r->state == RouteState::KNOWN, "a route listed");
  FCHECK(!route_leg(*r, plane_named(file, "AAL1070", h)).shown(), "DFW-TPA doesn't pass over New York");
  FCHECK(parse_adsbdb(slurp("fixtures/flights_adsbdb_jbu1407.json"), "JBU1407", cache, 10), "parses");
  FCHECK_EQ(leg_text(route_leg(*cache.find("JBU1407", 10), plane_named(file, "JBU1407", h)), " > "), "JFK > CDG");
  FCHECK(parse_adsbdb(slurp("fixtures/flights_adsbdb_unknown.json"), "ZZZ999", cache, 10), "unknown parses");
  FCHECK(cache.find("ZZZ999", 10)->state == RouteState::UNKNOWN, "unknown");
  route_unknown(cache, "N1103S", 10);
  FCHECK(cache.find("N1103S", 10)->state == RouteState::UNKNOWN, "a 404");

  // The reverse route is a different flight.
  Plane p = plane_named(file, "UAL755", h);
  Stop bos, iad;
  strcpy(bos.code, "BOS");
  bos.lat = 42.3643f;
  bos.lon = -71.0052f;
  strcpy(iad.code, "IAD");
  iad.lat = 38.9445f;
  iad.lon = -77.4558f;
  FCHECK(route_fits(iad, bos, p) && !route_fits(bos, iad, p), "direction counts");
  p.track = -1;
  FCHECK(route_fits(bos, iad, p), "no track, no direction check");
}

void test_cache() {
  RouteCache c;
  c.put("UAL1", 0).state = RouteState::KNOWN;
  FCHECK(c.find("UAL1", kRouteKnownS - 1) != nullptr, "fresh");
  FCHECK(c.find("UAL1", kRouteKnownS) == nullptr && c.wants("UAL1", kRouteKnownS), "stale");
  c.put("UAL2", 0);
  FCHECK(c.find("UAL2", kRouteUnknownS - 1) != nullptr && c.find("UAL2", kRouteUnknownS) == nullptr, "unknown ages");
  FCHECK(!c.wants("N123AB", 0), "no route asked for a registration");
  c.clear();
  char cs[9];
  for (unsigned i = 0; i < kRouteCache; i++) {
    snprintf(cs, sizeof(cs), "AAL%u", i);
    c.put(cs, i);
  }
  c.find("AAL0", 100);
  c.put("AAL99", 101);
  FCHECK(c.find("AAL0", 102) != nullptr && c.find("AAL1", 102) == nullptr, "least used goes");
  FCHECK(c.find("AAL99", 102) != nullptr, "the new one is in");
  c.put("AAL99", 103).state = RouteState::KNOWN;
  int copies = 0;
  for (const Route &r : c.slots)
    copies += strcmp(r.callsign, "AAL99") == 0;
  FCHECK(copies == 1, "one slot a callsign");
  // Wraparound of the clock.
  RouteCache w;
  w.put("DAL1", 0xFFFFFFF0u).state = RouteState::KNOWN;
  FCHECK(w.find("DAL1", 0x10u) != nullptr, "across the wrap");
}

void test_pick() {
  Nearby n = read("fixtures/flights_adsbfi_jfk.json", jfk());
  FCHECK(pick_card(n, "") == 0 && pick_card(n, nullptr) == 0, "nearest by default");
  FCHECK(pick_card(n, n.planes[1].hex) == 0, "LOT5N at 5 nm loses to 1.3 nm");
  FCHECK(pick_card(n, "zzzzzz") == 0, "gone");
  n.planes[1].dist_nm = 1.9f;
  FCHECK(pick_card(n, n.planes[1].hex) == 1, "close enough to stay");
}

// Ink boxes never overlap and stay inside the frame.
void check_frame(const Frame &f, const FrameOpts &o, const char *what) {
  const int limit = o.ticker ? o.h - 8 : o.h;
  FCHECK(f.items.size() <= kFrameMax, "%s: %zu labels", what, f.items.size());
  struct Box {
    int x0, x1, y0, y1;
  };
  std::vector<Box> boxes;
  for (const Text &t : f.items) {
    const int px = text_px(t);
    FCHECK(px <= t.w, "%s: \"%s\" fits %d px", what, t.text.c_str(), t.w);
    FCHECK(t.x >= 0 && t.x + t.w <= o.w, "%s: \"%s\" inside, x %d w %d", what, t.text.c_str(), t.x, t.w);
    const int x0 = t.align == Align::LEFT ? t.x : t.align == Align::RIGHT ? t.x + t.w - px : t.x + (t.w - px) / 2;
    const Box b{x0, x0 + px, t.y + font_ink_top(t.font), t.y + font_desc_bottom(t.font)};
    FCHECK(b.y1 < limit, "%s: \"%s\" clears the ticker (%d)", what, t.text.c_str(), b.y1);
    for (unsigned char c : t.text)
      FCHECK(c >= 0x20 && c < 0x7F, "%s: ASCII", what);
    for (const Box &o2 : boxes)
      FCHECK(b.x1 <= o2.x0 || o2.x1 <= b.x0 || b.y1 < o2.y0 || o2.y1 < b.y0, "%s: \"%s\" overlaps", what,
             t.text.c_str());
    boxes.push_back(b);
  }
}

const Text *find_text(const Frame &f, const std::string &text) {
  for (const Text &t : f.items)
    if (t.text == text)
      return &t;
  return nullptr;
}

void test_frames() {
  const char *file = "fixtures/flights_adsbfi_jfk.json";
  const Nearby n = read(file, jfk());
  RouteCache cache;
  parse_routeset(slurp("fixtures/flights_routeset_jfk.json"), recorded_ask(), cache, 100);
  for (int w : {64, 128, 192, 256})
    for (bool ticker : {true, false})
      for (int metric = 0; metric < 2; metric++)
        for (uint8_t card = 0; card < n.n; card++) {
          FrameOpts o;
          o.w = w;
          o.ticker = ticker;
          o.card = card;
          if (metric)
            o.units = units_metric();
          char what[64];
          snprintf(what, sizeof(what), "%d%s%s card %u", w, ticker ? " ticker" : "", metric ? " metric" : "", card);
          const Frame f = flight_frame(n, &cache, o, 100);
          check_frame(f, o, what);
          FCHECK(!f.items.empty() && f.items[0].text == plane_label(n.planes[card]), "%s: callsign first", what);
          FCHECK(ticker == !f.ticker.empty(), "%s: ticker", what);
        }
  FrameOpts o;
  o.w = 128;
  Frame f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "JetBlue") != nullptr && find_text(f, "JFK > CDG") != nullptr, "the 128 card");
  FCHECK(find_text(f, "A321neo") != nullptr && find_text(f, "1.5 mi SE") != nullptr, "type and where");
  FCHECK(find_text(f, "1,525 ft") != nullptr && find_text(f, "203 mph") != nullptr, "altitude and speed");
  FCHECK(find_text(f, "LOT5N") != nullptr && find_text(f, "N24NY") != nullptr && find_text(f, "N7QD") == nullptr,
         "three rows under the ticker");
  FCHECK(find_text(f, "JFK > CDG")->color == kColRoute, "route in gold");
  o.ticker = false;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "N7QD") != nullptr, "a fourth row without it");
  o.w = 256;
  o.ticker = true;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "Airbus A321neo") != nullptr && find_text(f, "Climbing") != nullptr, "the wide card");
  FCHECK(find_text(f, "23 planes") != nullptr && find_text(f, "Nearby") != nullptr, "the list's head");
  FCHECK(find_text(f, "JBU416") != nullptr && find_text(f, "CNS482") == nullptr, "six rows on the right");
  FCHECK(find_text(f, "5,200") != nullptr, "the last row whole");
  o.ticker = false;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "CNS482") != nullptr && find_text(f, "13") != nullptr && f.items.size() < kFrameMax,
         "seven rows without the ticker: %zu labels", f.items.size());
  o.ticker = true;
  FCHECK(find_text(f, "B6 1407") == nullptr && find_text(f, "B61407") != nullptr, "flight number over the route");
  o.w = 64;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "JFK > CDG") != nullptr && find_text(f, "A21N") != nullptr && find_text(f, "203mph") != nullptr,
         "the 64 card");
  o.card = 2;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "Bell 407") != nullptr, "no airline: the type: %s", f.items[1].text.c_str());

  // Ground-only, emergencies and no routes still fit.
  Nearby g = read(file, jfk(1));
  g.planes[0].emergency = true;
  g.planes[0].squawk = 7700;
  for (int w : {64, 128, 256}) {
    FrameOpts og;
    og.w = w;
    const Frame fg = flight_frame(g, nullptr, og, 100);
    check_frame(fg, og, "ground");
    FCHECK(fg.items[0].color == kColAlert, "emergency in red");
    FCHECK(fg.ticker.find("Emergency 7700 N3240J") != std::string::npos, "the ticker says so: %s", fg.ticker.c_str());
    FCHECK(fg.ticker.rfind("14 planes on the ground", 0) == 0, "ground count first");
  }
  FCHECK(flight_frame(Nearby(), nullptr, o, 0).items.empty(), "no planes, no frame");
}

void test_notes() {
  for (int w : {64, 128, 192, 256})
    for (int k = 0; k < 4; k++) {
      FrameOpts o;
      o.w = w;
      const Frame f = note_frame((Note) k, o, to_nm(10, DistUnit::MILES), Source::ADSBFI);
      check_frame(f, o, "note");
      FCHECK(!f.items.empty(), "a note");
    }
  FrameOpts o;
  o.w = 64;
  Frame f = note_frame(Note::NO_HOME, o, 10, Source::ADSBFI);
  FCHECK(f.items.size() == 4 && f.items[0].text == "Set your", "wrapped for one panel: %zu", f.items.size());
  f = note_frame(Note::NONE_NEAR, o, to_nm(10, DistUnit::MILES), Source::ADSBFI);
  FCHECK(f.items.size() == 2 && f.items[1].text == "within 10 mi", "%s", f.items.back().text.c_str());
  o.w = 128;
  f = note_frame(Note::NO_DATA, o, 10, Source::ADSBLOL);
  FCHECK(f.items.size() == 1 && f.items[0].text == "Can't reach adsb.lol", "one line on two panels");
}

void test_ticker() {
  const Nearby n = read("fixtures/flights_adsbfi_jfk.json", jfk());
  RouteCache cache;
  parse_routeset(slurp("fixtures/flights_routeset_jfk.json"), recorded_ask(), cache, 100);
  FrameOpts o;
  const std::string t = flight_ticker(n, &cache, o, 100);
  FCHECK_EQ(t,
            "23 planes nearby | JBU1407 JetBlue, Airbus A321neo, JFK to CDG, 1,525 ft, 203 mph, 1.5 mi SE | "
            "LOT5N LOT, Boeing 777-200, JFK to WAW, 4,500 ft, 244 mph, 5.7 mi E | "
            "N77NY, Bell 407, 1,500 ft, 5 mph, 8.8 mi W");
  o.card = 1;
  o.units = units_metric();
  const std::string m = flight_ticker(n, nullptr, o, 100, 2);
  FCHECK(m.find("| LOT5N LOT, Boeing 777-200, 1,372 m, 393 km/h, 9.2 km E | JBU1407") != std::string::npos,
         "the card first, metric: %s", m.c_str());
}

}

int run_flight_tests(int &total_checks) {
  test_adsbfi();
  test_ground_only();
  test_other_sources();
  test_streaming();
  test_nopos_order();
  test_names();
  test_units();
  test_geometry();
  test_urls();
  test_feed();
  test_routeset();
  test_adsbdb();
  test_cache();
  test_pick();
  test_frames();
  test_notes();
  test_ticker();
  total_checks += checks;
  return failures;
}
