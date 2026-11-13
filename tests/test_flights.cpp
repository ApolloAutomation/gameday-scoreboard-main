// Flight Tracker core against recorded flight data answers.
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "app_flights.h"
#include "flight_airlines.h"
#include "flights.h"
#include "timezones.h"

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
  FCHECK(!plane_from_json(d.as<JsonObjectConst>(), jfk(), p), "no position: not nearby");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), Home(), p) && p.tier == Tier::NOPOS, "no home: kept");
  FCHECK_EQ(p.hex, "abc123");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"seen_pos\":90,\"seen\":1}");
  FCHECK(!plane_from_json(d.as<JsonObjectConst>(), jfk(), p), "an old position: not nearby");
  // Only a real emergency shows red.
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"squawk\":\"1200\",\"emergency\":\"lifeguard\"}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && !p.emergency, "lifeguard is routine");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"emergency\":\"minfuel\"}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && !p.emergency, "minfuel is routine");
  deserializeJson(d, "{\"hex\":\"abc123\",\"lat\":40.7,\"lon\":-73.8,\"squawk\":\"1200\",\"emergency\":\"general\"}");
  FCHECK(plane_from_json(d.as<JsonObjectConst>(), jfk(), p) && p.emergency, "general is one");
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
  FCHECK_EQ(type_text("B738", 6), "B738");
  FCHECK_EQ(type_text("B3XM", 16), "737 MAX 10");
  FCHECK_EQ(type_text("B38M", 14), "737 MAX 8");
  FCHECK_EQ(type_text("SF50", 14), "Vision Jet");
  FCHECK_EQ(type_text("CL35", 14), "Challenger 350");
  FCHECK_EQ(type_text("C25B", 10), "C25B");
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
  FCHECK(std::fabs(bearing_deg(0, 0, 1, 0)) < 1e-4 && std::fabs(bearing_deg(0, 0, 0, 1) - 90) < 1e-4, "N and E");
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
  FCHECK(p.source == Source::FR24 && p.in_ms == 0, "Flightradar24 first");
  feed_started(f, Source::FR24, 1000);
  feed_done(f, Source::FR24, 200, true, 0, 0, 1500);
  p = feed_pick(f, false, 2000);
  FCHECK(p.source == Source::FR24 && p.in_ms == kFr24PollMs - 1000, "every 20 s: %u", p.in_ms);
  feed_started(f, Source::FR24, 21000);
  feed_done(f, Source::FR24, -1, false, 0, 7, 21500);
  p = feed_pick(f, false, 22000);
  FCHECK(p.source == Source::FR24 && p.in_ms > 0 && p.in_ms <= 30000, "one failure: try it again: %u", p.in_ms);
  FCHECK(!feed_out(f, Source::FR24, 22000), "not out yet");
  feed_started(f, Source::FR24, 22000 + p.in_ms);
  feed_done(f, Source::FR24, 503, false, 0, 7, 22500 + p.in_ms);
  const uint32_t t2 = 23000 + p.in_ms;
  p = feed_pick(f, false, t2);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == 0, "two failures: adsb.fi");
  feed_started(f, Source::ADSBFI, t2);
  feed_done(f, Source::ADSBFI, 200, true, 0, 0, t2 + 500);
  p = feed_pick(f, false, t2 + 1000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == kFiPollMs - 1000, "adsb.fi at its pace: %u", p.in_ms);
  const uint32_t back = t2 + f.wait[(size_t) Source::FR24].left(t2);
  p = feed_pick(f, false, back);
  FCHECK(p.source == Source::FR24 && p.in_ms == 0, "back to Flightradar24 once its wait ends");
  feed_started(f, Source::FR24, back);
  feed_done(f, Source::FR24, 200, true, 0, 0, back + 500);
  FCHECK(f.wait[(size_t) Source::FR24].run.streak == 0, "a good read clears its run");

  Feed r;
  feed_started(r, Source::FR24, 1000);
  feed_done(r, Source::FR24, 403, false, 0, 0, 1500);
  p = feed_pick(r, false, 2000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == 0, "a refusal moves on at once");
  FCHECK(r.wait[(size_t) Source::FR24].left(2000) >= espn::kThrottleBaseMs / 2 - 500, "and waits the long way");
  feed_started(r, Source::ADSBFI, 2000);
  feed_done(r, Source::ADSBFI, 429, false, 120000, 0, 2500);
  p = feed_pick(r, false, 3000);
  FCHECK(p.source == Source::ADSBLOL && p.in_ms == 0, "adsb.lol while both are out");
  FCHECK(r.wait[(size_t) Source::ADSBFI].left(3000) >= 119000, "Retry-After honored: %u",
         r.wait[(size_t) Source::ADSBFI].left(3000));
  feed_started(r, Source::ADSBLOL, 3000);
  feed_done(r, Source::ADSBLOL, 200, true, 0, 0, 3500);
  p = feed_pick(r, false, 4000);
  FCHECK(p.source == Source::FR24 && p.in_ms == 147500, "Flightradar24 before adsb.lol's next turn: %u", p.in_ms);
  feed_done(r, Source::ADSBFI, 200, true, 0, 0, 5000);
  p = feed_pick(r, false, 5000);
  FCHECK(p.source == Source::ADSBFI && p.in_ms == kFiPollMs - 3000, "a good read ends the wait: %u", p.in_ms);

  Feed l;
  p = feed_pick(l, true, 0);
  FCHECK(p.source == Source::LOCAL && p.in_ms == 0, "the owner's receiver first");
  feed_started(l, Source::LOCAL, 0);
  feed_done(l, Source::LOCAL, -1, false, 0, 0, 100);
  feed_started(l, Source::LOCAL, 30000);
  feed_done(l, Source::LOCAL, -1, false, 0, 0, 30100);
  p = feed_pick(l, true, 30200);
  FCHECK(p.source == Source::FR24 && p.in_ms == 0, "Flightradar24 while it is down");
  feed_done(l, Source::LOCAL, 200, true, 0, 0, 30300);
  p = feed_pick(l, true, kLocalPollMs + 30000);
  FCHECK(p.source == Source::LOCAL && p.in_ms == 0, "every 10 s");

  feed_lol_limits(f, "498", "72122");
  FCHECK(f.lol_gap_ms == kLolMinMs, "plenty left: %u", f.lol_gap_ms);
  feed_lol_limits(f, "10", "3600");
  FCHECK(f.lol_gap_ms == 360000, "spread over the window: %u", f.lol_gap_ms);
  feed_lol_limits(f, "0", "3600");
  FCHECK(f.lol_gap_ms == 3600000, "none left: wait for the reset");
  feed_lol_limits(f, "0", "200000");
  FCHECK(f.lol_gap_ms == kLolMaxMs, "at most a day: %u", f.lol_gap_ms);
  feed_lol_limits(f, "0", "3600");
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
  auto apart = [](const Box &a, const Box &b) { return a.x1 <= b.x0 || b.x1 <= a.x0 || a.y1 < b.y0 || b.y1 < a.y0; };
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
      FCHECK(apart(b, o2), "%s: \"%s\" overlaps", what, t.text.c_str());
    boxes.push_back(b);
  }
  // Pictures and logos: inside, over the ticker, clear of text.
  std::vector<Box> pics;
  for (const Art &a : f.art) {
    const PlaneSprite *s = plane_sprite(a.kind);
    FCHECK(s != nullptr, "%s: a picture of something", what);
    if (s == nullptr)
      continue;
    const Box b{a.x, a.x + s->w, a.y, a.y + s->h - 1};
    FCHECK(b.x0 >= 0 && b.x1 <= o.w && b.y0 >= 0 && b.y1 < limit, "%s: the picture inside", what);
    for (const Box &t : boxes)
      FCHECK(apart(b, t), "%s: the picture clear of text", what);
    pics.push_back(b);
  }
  FCHECK(f.art.size() <= 1 && f.logos.size() <= 1, "%s: one picture, one logo", what);
  for (const Logo &l : f.logos) {
    const Box b{l.x, l.x + kLogoPx, l.y, l.y + kLogoPx - 1};
    FCHECK(b.x0 >= 0 && b.x1 <= o.w && b.y0 >= 0 && b.y1 < limit, "%s: the logo inside", what);
    FCHECK(airline_by_icao(l.icao) != nullptr, "%s: a logo to load", what);
    for (size_t i = 0; i < boxes.size(); i++)
      FCHECK((int) i == l.alt || apart(b, boxes[i]), "%s: the logo clear of \"%s\"", what, f.items[i].text.c_str());
    for (const Box &p : pics)
      FCHECK(apart(b, p), "%s: the logo clear of the picture", what);
    if (l.alt >= 0) {
      FCHECK((size_t) l.alt < f.items.size(), "%s: its code a label", what);
      const Box &c = boxes[(size_t) l.alt];
      FCHECK(c.x0 >= b.x0 && c.x1 <= b.x1 && c.y0 >= b.y0 && c.y1 <= b.y1, "%s: the code in the logo's box", what);
    }
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
        for (bool list : {false, true})
          for (uint8_t card = 0; card < n.n; card++) {
            FrameOpts o;
            o.w = w;
            o.ticker = ticker;
            o.card = card;
            o.list = list;
            if (metric)
              o.units = units_metric();
            char what[64];
            snprintf(what, sizeof(what), "%d%s%s%s card %u", w, ticker ? " ticker" : "", metric ? " metric" : "",
                     list ? " list" : "", card);
            const Frame f = flight_frame(n, &cache, o, 100);
            check_frame(f, o, what);
            if (!list || w >= 192)
              FCHECK(find_text(f, plane_label(n.planes[card])) != nullptr, "%s: callsign present", what);
            if (list && w < 192)
              FCHECK(f.art.empty() && f.logos.empty(), "%s: the list has no pictures", what);
            FCHECK(ticker == !f.ticker.empty(), "%s: ticker", what);
          }
  FrameOpts o;
  o.w = 128;
  Frame f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "JetBlue") != nullptr && find_text(f, "JFK-CDG") != nullptr, "the 128 card");
  FCHECK(find_text(f, "Airbus A321neo") != nullptr && find_text(f, "1.5 mi SE") != nullptr, "type and where");
  FCHECK(find_text(f, "1,525 ft") != nullptr && find_text(f, "203 mph") != nullptr, "altitude and speed");
  FCHECK(find_text(f, "Climbing") != nullptr && find_text(f, "LOT5N") == nullptr, "the card alone");
  FCHECK(find_text(f, "JFK-CDG")->color == kColRoute, "route in gold");
  o.list = true;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "Nearby") != nullptr && find_text(f, "23 planes") != nullptr, "the list's head");
  FCHECK(find_text(f, "JBU1407") != nullptr && find_text(f, "UAL2495") != nullptr && find_text(f, "JBU416") == nullptr,
         "six rows over the ticker");
  FCHECK(find_text(f, "A21N") != nullptr && find_text(f, "1,525") != nullptr && find_text(f, "1.5") != nullptr,
         "type, altitude and distance");
  o.ticker = false;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "JBU416") != nullptr, "a seventh row without it");
  o.w = 64;
  o.ticker = true;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "23 planes") != nullptr && find_text(f, "JBU1407") != nullptr && find_text(f, "A21N") == nullptr,
         "the one-panel list: callsign and distance");
  o.list = false;
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
  FCHECK(find_text(f, "JFK-CDG") != nullptr && find_text(f, "A21N") != nullptr && find_text(f, "203mph") != nullptr,
         "the 64 card");
  o.card = 2;
  f = flight_frame(n, &cache, o, 100);
  FCHECK(find_text(f, "B407") != nullptr && f.art.size() == 1 && f.art[0].kind == PlaneKind::HELI && f.logos.empty(),
         "no airline: a helicopter and its code");
  FCHECK(f.items[0].font == BoardFont::S16 && f.items[0].text == "N77NY", "the big callsign stays");
  // An airline missing from the table keeps its route.
  Nearby un = n;
  un.n = 1;
  un.planes[0] = n.planes[0];
  Plane &nk = un.planes[0];
  memset(nk.callsign, 0, sizeof(nk.callsign));
  strcpy(nk.callsign, "NKS123");
  strcpy(nk.type, "A20N");
  strcpy(nk.from, "DEN");
  strcpy(nk.to, "LGA");
  nk.flight[0] = nk.brand[0] = '\0';
  FCHECK(logo_airline(nk) == nullptr, "NKS isn't in the table");
  for (bool ticker : {true, false}) {
    FrameOpts ou;
    ou.w = 64;
    ou.ticker = ticker;
    f = flight_frame(un, nullptr, ou, 100);
    check_frame(f, ou, "unlisted airline");
    const Text *route = find_text(f, "DEN-LGA");
    FCHECK(route != nullptr && route->color == kColRoute && find_text(f, "NKS123") != nullptr && f.logos.empty(),
           "unlisted airline: callsign and route");
    FCHECK(f.art.size() == 1 && f.art[0].kind == PlaneKind::NARROW, "unlisted airline: its picture");
  }
  un.planes[0].from[0] = un.planes[0].to[0] = '\0';
  strcpy(un.planes[0].flight, "NK123");
  f = flight_frame(un, nullptr, o, 100);
  FCHECK(find_text(f, "NK123") != nullptr && find_text(f, "NK123")->color == kColDim, "or its flight number");

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
  g.planes[0].squawk = 1200;
  FrameOpts og;
  const Frame gen = flight_frame(g, nullptr, og, 100);
  FCHECK(gen.ticker.find("Emergency N3240J") != std::string::npos, "no code, just the word: %s", gen.ticker.c_str());
  g.planes[0].emergency = false;
  FCHECK(flight_frame(g, nullptr, og, 100).ticker.find("Emergency") == std::string::npos, "none");

  // Metric on one panel: 1,185 km/h fits as a number.
  Nearby fast = read(file, jfk(1));
  fast.planes[0].gs_kt = 640;
  FrameOpts om;
  om.w = 64;
  om.units = units_metric();
  const Frame ff = flight_frame(fast, nullptr, om, 100);
  FCHECK(find_text(ff, "1185") != nullptr && find_text(ff, "1185km/") == nullptr, "the speed whole");
  FCHECK(flight_frame(Nearby(), nullptr, o, 0).items.empty(), "no planes, no frame");
}

void test_notes() {
  for (int w : {64, 128, 192, 256})
    for (int k = 0; k < 4; k++) {
      FrameOpts o;
      o.w = w;
      const Frame f = note_frame((Note) k, o, to_nm(10, DistUnit::MILES));
      check_frame(f, o, "note");
      FCHECK(!f.items.empty(), "a note");
    }
  FrameOpts o;
  o.w = 64;
  Frame f = note_frame(Note::NO_HOME, o, 10);
  FCHECK(f.items.size() == 4 && f.items[0].text == "Set your", "wrapped for one panel: %zu", f.items.size());
  f = note_frame(Note::NONE_NEAR, o, to_nm(10, DistUnit::MILES));
  FCHECK(f.items.size() == 3 && f.items[0].text == "No planes" && f.items[2].text == "within 10 mi", "%s",
         f.items.back().text.c_str());
  FCHECK(f.items[0].color == kColValue && f.items[2].color == kColDim, "the radius dimmer");
  o.w = 128;
  f = note_frame(Note::NO_DATA, o, 10);
  FCHECK(f.items.size() == 1 && f.items[0].text == "Can't reach flight data", "one line on two panels");
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

// ---- Flightradar24 -------------------------------------------------------------

const char *kFr24File = "fixtures/flights_fr24_nyc.json";
const int64_t kFr24Now = 1794457500;  // the recording's timestamps plus a few seconds

Home nyc(float radius_nm = 25) {
  Home h;
  h.lat = 40.71;
  h.lon = -74.01;
  h.radius_nm = radius_nm;
  return h;
}

void test_fr24() {
  const std::string json = slurp(kFr24File);
  Nearby n;
  FCHECK(parse_fr24(json, nyc(), kFr24Now, n), "the feed parses");
  FCHECK(n.total == 34 && n.n == kNear && n.tier == Tier::AIR, "34 flights, the nearest 8: %u, %u", n.total, n.n);
  FCHECK(n.air == 28 && n.ground == 0, "28 within 25 nm: %u", n.air);
  const char *want[] = {"SWA1678", "N7QD", "N24NY", "N77NY", "JBU2128", "RPA4505", "C172", "JBU924"};
  for (size_t i = 0; i < kNear; i++)
    FCHECK_EQ(plane_label(n.planes[i]), want[i]);
  const Plane &p = n.planes[0];
  FCHECK_EQ(p.hex, "ac0fdc");
  FCHECK_EQ(p.flight, "WN1678");
  FCHECK_EQ(p.from, "DEN");
  FCHECK_EQ(p.to, "LGA");
  FCHECK_EQ(p.type, "B38M");
  FCHECK(p.alt_ft == 5450 && p.gs_kt == 277 && p.seen_s >= 3 && p.seen_s <= 5, "SWA1678 5450 ft, 277 kt");
  FCHECK(std::fabs(p.dist_nm - 1.40f) < 0.01f, "SWA1678 1.4 nm: %f", p.dist_nm);
  FCHECK(p.track >= 0 && !p.emergency && p.tier == Tier::AIR, "airborne");
  for (size_t i = 1; i < n.n; i++)
    FCHECK(n.planes[i - 1].dist_nm <= n.planes[i].dist_nm, "nearest first at %zu", i);
  // Every flight against the haversine and its own array.
  JsonDocument doc;
  deserializeJson(doc, json);
  int seen = 0;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    if (!kv.value().is<JsonArrayConst>())
      continue;
    Plane q;
    JsonArrayConst a = kv.value().as<JsonArrayConst>();
    if (!plane_from_fr24(a, nyc(), kFr24Now, q))
      continue;
    seen++;
    const double d = distance_nm(40.71, -74.01, a[1].as<double>(), a[2].as<double>());
    FCHECK(std::fabs(q.dist_nm - d) < 0.01, "%s distance", q.callsign);
    FCHECK(q.bearing >= 0 && q.bearing < 360, "%s bearing", q.callsign);
  }
  FCHECK(seen == 28, "28 kept one by one: %d", seen);

  // No route without both ends.
  const int eja = index_of(n, "EJA974");
  FCHECK(eja < 0, "EJA974 is farther than the eighth");
  Nearby wide;
  FCHECK(parse_fr24(json, nyc(30), kFr24Now, wide), "30 nm");
  Plane e;
  for (JsonPairConst kv : doc.as<JsonObjectConst>())
    if (kv.value().is<JsonArrayConst>() && strcmp(kv.value()[16] | "", "EJA974") == 0)
      plane_from_fr24(kv.value().as<JsonArrayConst>(), nyc(30), kFr24Now, e);
  FCHECK(e.from[0] == '\0' && e.to[0] == '\0' && strcmp(e.flight, "EJA974") == 0, "BCT and no destination: none");

  // Old positions, the ground and emergencies.
  FCHECK(parse_fr24(json, nyc(), kFr24Now + 120, n) && n.total == 34 && n.n == 0, "two minutes old: none");
  std::string gnd = std::regex_replace(json, std::regex("\"WN1678\",0,"), "\"WN1678\",1,");
  FCHECK(parse_fr24(gnd, nyc(1.5f), kFr24Now, n) && n.n == 1 && n.tier == Tier::GROUND &&
             n.planes[0].alt_ft == kNoAlt,
         "on the ground");
  std::string sq = std::regex_replace(json, std::regex("277,\"\""), "277,\"7700\"");
  FCHECK(parse_fr24(sq, nyc(), kFr24Now, n) && n.planes[0].emergency && n.planes[0].squawk == 7700, "7700");

  // The answer's shape, cut short or not the feed.
  FCHECK(!parse_fr24(json.substr(0, json.size() / 2), nyc(), kFr24Now, n), "a cut-off read fails");
  FCHECK(!parse_fr24(std::string("<html>blocked</html>"), nyc(), kFr24Now, n), "not JSON");
  FCHECK(parse_fr24(std::string("{\"full_count\":0,\"version\":4}"), nyc(), kFr24Now, n) && n.n == 0 &&
             n.total == 0,
         "an empty sky");
  const std::string extra = "{\"full_count\":13569,\"stats\":{\"a\":[1,{\"b\":\"]}\"}]},\"k\\\"ey\":\"x\"," + json.substr(1);
  FCHECK(parse_fr24(extra, nyc(), kFr24Now, n) && n.total == 34, "skips objects and strings: %u", n.total);
  FCHECK(parse_fr24(std::string("{\"x\":[\"A\",\"lat\"],\"version\":4}"), nyc(), kFr24Now, n) && n.total == 1 &&
             n.n == 0,
         "an array that isn't a flight");
  // Ten copies, flat memory: still the nearest eight.
  std::string big = "{";
  for (int i = 0; i < 10; i++)
    big += (i ? "," : "") + json.substr(1, json.rfind('}') - 1);
  big += "}";
  FCHECK(parse_fr24(big, nyc(), kFr24Now, n) && n.total == 340 && n.n == kNear, "340 in: %u", n.total);
  FCHECK_EQ(plane_label(n.planes[1]), "N7QD");

  // Its routes need no lookup; the card shows them.
  RouteCache cache;
  parse_fr24(json, nyc(), kFr24Now, n);
  FCHECK(route_asks(n, cache, 100).n == 0, "nothing to look up");
  FrameOpts o;
  const Frame f = flight_frame(n, &cache, o, 100);
  bool route = false, number = false;
  for (const Text &t : f.items) {
    route = route || (t.text == "DEN-LGA" && t.color == kColRoute);
    number = number || t.text == "WN1678";
  }
  FCHECK(route && number, "DEN-LGA and WN1678 on the card");
  FCHECK(f.ticker.find("SWA1678 Southwest, Boeing 737 MAX 8, DEN to LGA, 5,450 ft") != std::string::npos, "%s",
         f.ticker.c_str());
}

void test_fr24_url() {
  Home h;
  h.lat = 40.6413;
  h.lon = -73.7781;
  h.radius_nm = 10;
  const Bounds b = query_bounds(h);
  FCHECK(std::fabs(b.north - (40.64 + 11 / 60.0)) < 1e-9 && std::fabs(b.south - (40.64 - 11 / 60.0)) < 1e-9,
         "11 nm north and south of the rounded center");
  const double d = distance_nm(40.64, -73.78, 40.64, b.east);
  FCHECK(std::fabs(d - 11) < 0.05, "and east: %f", d);
  FCHECK_EQ(fr24_url(h),
            "https://data-cloud.flightradar24.com/zones/fcgi/feed.js?bounds=40.82,40.46,-74.02,-73.54&faa=1&satellite=1"
            "&mlat=1&flarm=1&adsb=1&gnd=0&air=1&vehicles=0&estimated=0&maxage=14400&gliders=0&stats=0");
  h.lat = 89.9;
  h.lon = 179.95;
  h.radius_nm = 250;
  const Bounds p = query_bounds(h);
  FCHECK(p.north == 90 && p.east == 180 && p.west == -180, "clamped at the pole: %f %f %f", p.north, p.east, p.west);
  FCHECK(!box_wraps(h), "the pole's box is the whole way round");
  Home labasa;
  labasa.lat = -16.43;
  labasa.lon = 179.38;
  labasa.radius_nm = 87;
  Home adak;
  adak.lat = 51.88;
  adak.lon = -176.64;
  adak.radius_nm = 217;
  FCHECK(box_wraps(labasa) && box_wraps(adak) && !box_wraps(nyc()), "across the 180th meridian");
  Feed f;
  FCHECK(feed_pick(f, false, 1000, false).source == Source::ADSBFI, "there, adsb.fi first");
  FCHECK(feed_pick(f, true, 1000, false).source == Source::LOCAL, "after the receiver");
  FCHECK_EQ(source_name(Source::FR24), "Flightradar24");
  FCHECK_EQ(source_key(Source::FR24), "fr24");
  FCHECK_EQ(source_key(Source::LOCAL), "receiver");
}

// ---- Settings --------------------------------------------------------------------

void test_prefs() {
  int32_t la = 0, lo = 0;
  FCHECK(parse_coords("40.6413,-73.7781", la, lo) && la == 406413 && lo == -737781, "lat,lon");
  FCHECK(parse_coords(" 40.64129, -73.77806 ", la, lo) && la == 406413 && lo == -737781, "rounded: %d %d", la, lo);
  FCHECK(parse_coords("51.5 -0.12", la, lo) && la == 515000 && lo == -1200, "a space between");
  FCHECK(parse_coords("+33.94,151", la, lo) && la == 339400 && lo == 1510000, "a plus sign, a whole number");
  FCHECK(parse_coords("-33.86785,151.20732", la, lo) && la == -338679 && lo == 1512073, "southern: %d", la);
  for (const char *bad : {"", "40.6", "91,0", "0,181", "40.6,-73.7,1", "forty,two", "40.6;-73.7", "0,0", "1234,5",
                          "40.6-73.7", "40.6,", ",-73.7"})
    FCHECK(!parse_coords(bad, la, lo), "refused: \"%s\"", bad);
  FCHECK(parse_coords("90,180", la, lo) && parse_coords("-90.0000,-180", la, lo), "the edges");
  FCHECK_EQ(coord_text(406413, -737781), "40.6413,-73.7781");
  FCHECK_EQ(coord_text(-1200, 5), "-0.1200,0.0005");
  FCHECK_EQ(coord_text(kNoCoord, kNoCoord), "");

  uint16_t r = 0;
  uint8_t u = 9;
  FCHECK(parse_radius("10", r, u) && r == 10 && u == (uint8_t) DistUnit::MILES, "miles by default");
  FCHECK(parse_radius("25km", r, u) && r == 25 && u == (uint8_t) DistUnit::KM, "km");
  FCHECK(parse_radius("999nm", r, u) && r == 200 && u == (uint8_t) DistUnit::NM, "capped");
  for (const char *bad : {"", "0", "km", "10 mi", "10ft", "1000", "-5"})
    FCHECK(!parse_radius(bad, r, u), "refused: \"%s\"", bad);
  FCHECK(to_nm(radius_max(DistUnit::MILES), DistUnit::MILES) <= kMaxRadiusNm &&
             to_nm(radius_max(DistUnit::KM), DistUnit::KM) <= kMaxRadiusNm,
         "every cap within the query's");

  Prefs p;
  FCHECK(!prefs_home(p).valid(), "no home at first");
  FCHECK(prefs_set(p, "flyhome", "40.71,-74.01") == espn::AppSet::CHANGED, "a home");
  FCHECK(prefs_set(p, "flyhome", "40.71, -74.01") == espn::AppSet::SAME, "the same home");
  FCHECK(prefs_set(p, "flyhome", "north") == espn::AppSet::REFUSED && p.lat_e4 == 407100, "a bad one kept out");
  const Home h = prefs_home(p);
  FCHECK(h.valid() && std::fabs(h.lat - 40.71) < 1e-9 && std::fabs(h.radius_nm - to_nm(10, DistUnit::MILES)) < 1e-4,
         "10 miles around it");
  FCHECK(prefs_set(p, "flyplace", "Manhattan, New York\x01") == espn::AppSet::CHANGED &&
             strcmp(p.place, "Manhattan, New York") == 0,
         "a place name, no control characters");
  std::string longer(60, 'a');
  longer.replace(46, 4, "\xC3\xA9\xC3\xA9");
  prefs_set(p, "flyplace", longer);
  FCHECK(strlen(p.place) == 46, "cut before a split letter: %zu", strlen(p.place));
  FCHECK(prefs_set(p, "flyradius", "25km") == espn::AppSet::CHANGED && p.radius == 25 && p.radius_unit == 1, "25 km");
  FCHECK(prefs_set(p, "flyreceiver", "192.168.1.50") == espn::AppSet::CHANGED, "a receiver");
  FCHECK(prefs_set(p, "flyreceiver", "my pi") == espn::AppSet::REFUSED, "no spaces");
  FCHECK(prefs_set(p, "flyreceiver", std::string(96, 'a')) == espn::AppSet::REFUSED, "96 is too long");
  FCHECK(prefs_set(p, "flyreceiver", "none") == espn::AppSet::CHANGED && p.receiver[0] == '\0', "none clears it");
  FCHECK(prefs_set(p, "flyhome", "none") == espn::AppSet::CHANGED && !prefs_home(p).valid() && p.place[0] == '\0',
         "none clears the home and its name");
  FCHECK(prefs_set(p, "bogus", "1") == espn::AppSet::REFUSED, "not its key");

  Prefs junk;
  memset(&junk, 0x7F, sizeof(junk));
  prefs_clean(junk);
  FCHECK(junk.lat_e4 == kNoCoord && junk.radius == kRadiusDefault && junk.radius_unit == 0 &&
             strlen(junk.place) == kPlaceMax && strlen(junk.receiver) == kReceiverMax,
         "a garbled blob cleaned");

  int us = -1, uk = -1, eu = -1;
  for (size_t i = 0; i < espn::kTimezoneCount; i++) {
    const std::string name = espn::kTimezones[i].name;
    if (name == "US Pacific")
      us = (int) i;
    if (name == "UK and Ireland")
      uk = (int) i;
    if (name == "Central Europe")
      eu = (int) i;
  }
  FCHECK(units_for_zone(us).dist == DistUnit::MILES && units_for_zone(us).alt == AltUnit::FEET, "US: miles, feet");
  FCHECK(units_for_zone(uk).speed == SpeedUnit::MPH, "UK: mph");
  FCHECK(units_for_zone(eu).dist == DistUnit::KM && units_for_zone(eu).alt == AltUnit::METERS, "Europe: metric");
  FCHECK(units_for_zone(-1).dist == DistUnit::KM, "unknown: metric");

  JsonDocument d;
  Prefs q;
  prefs_set(q, "flyhome", "40.71,-74.01");
  prefs_set(q, "flyplace", "Manhattan");
  prefs_state(q, units_for_zone(us), d.to<JsonObject>());
  std::string out;
  serializeJson(d, out);
  FCHECK_EQ(out, "{\"home\":\"40.7100,-74.0100\",\"place\":\"Manhattan\",\"radius\":10,\"radius_unit\":\"mi\","
                 "\"receiver\":\"\",\"units\":\"imperial\"}");
}

// ---- The display ------------------------------------------------------------------

struct Reads {
  FlightsApp app;
  int64_t now{kFr24Now};
  std::string last_url;
  // One read the way the firmware runs it.
  bool step(int status, const std::string &body, uint32_t retry_ms = 0) {
    espn::AppRequest r;
    if (!this->app.request(this->now, r))
      return false;
    this->last_url = r.url;
    bool ok = false;
    if (status == 200) {
      espn::AppTextStream in(body);
      ok = this->app.parse(in, this->now);
    }
    this->app.ended(espn::AppResult{status > 0 ? status : 0, espn::is_throttle_status(status), retry_ms});
    this->app.apply(ok, this->now);
    return true;
  }
  // Waits as long as poll_ms says, from this read's start.
  void wait() {
    const uint32_t ms = this->app.poll_ms(this->now);
    this->now += (ms + 999) / 1000;
  }
};

bool app_has(const espn::AppFrame &f, const std::string &text) {
  for (const espn::AppText &t : f.texts)
    if (t.text == text)
      return true;
  return false;
}

std::string frame_text(const espn::AppFrame &f) {
  std::string out;
  for (const espn::AppText &t : f.texts)
    out += t.text + "|";
  return out;
}

// Planespotters' answer as it comes, with made-up names
const char *const kSpotted =
    "{\"photos\":[{\"id\":\"1592852\",\"thumbnail\":{\"src\":\"https:\\/\\/t.plnspttrs.net\\/05072\\/"
    "1592852_29b08bc9ff_t.jpg\",\"size\":{\"width\":200,\"height\":133}},\"thumbnail_large\":{\"src\":"
    "\"https:\\/\\/t.plnspttrs.net\\/05072\\/1592852_29b08bc9ff_280.jpg\",\"size\":{\"width\":420,"
    "\"height\":280}},\"link\":\"https:\\/\\/www.planespotters.net\\/photo\\/1592852\\/n8766t?utm_source=api\","
    "\"photographer\":\"A. Spotter\"}]}";

void test_app() {
  const std::string fr24 = slurp(kFr24File);
  const std::string fi = slurp("fixtures/flights_adsbfi_jfk.json");
  Reads r;
  r.app.setup();
  espn::AppFrame f;
  r.app.frame(128, 64, r.now, f);
  FCHECK(frame_text(f).find("Set your") == 0, "no home: a note: %s", frame_text(f).c_str());
  espn::AppRequest req;
  FCHECK(!r.app.request(r.now, req), "nothing to read without a home");
  FCHECK(!r.app.lit(r.now), "no home, no planes to light it");
  FCHECK(r.app.set("flyhome", "40.71,-74.01") == espn::AppSet::CHANGED, "a home");
  r.app.set("flyradius", "28mi");
  espn::app_env().tz = 0;
  r.app.shown(true);
  f = espn::AppFrame();
  r.app.frame(128, 64, r.now, f);
  FCHECK(frame_text(f) == "Looking for planes|", "before the first read: %s", frame_text(f).c_str());
  FCHECK(r.app.poll_ms(r.now) == 0, "due at once");
  FCHECK(r.step(200, fr24) && r.last_url.find("data-cloud.flightradar24.com") != std::string::npos,
         "Flightradar24 first: %s", r.last_url.c_str());
  FCHECK(r.app.nearby().n == kNear && r.app.source() == Source::FR24, "eight planes from it");
  FCHECK(r.app.poll_ms(r.now) == kFr24PollMs, "next in 20 s: %u", r.app.poll_ms(r.now));
  FCHECK(r.app.lit(r.now) && !r.app.lit(r.now + FlightsApp::kOldS + 1), "planes keep Off or dim lit");
  {
    // The airline logo as an icon the board loads
    espn::AppFrame wide;
    r.app.frame(256, 64, r.now, wide);
    FCHECK(wide.icons.size() == 1 && wide.art.size() == 1, "a logo and a picture: %zu %zu", wide.icons.size(),
           wide.art.size());
    for (const espn::AppIcon &ic : wide.icons) {
      char url[160];
      const size_t n = ic.url != nullptr ? ic.url(ic.key, ic.kind, 0, url, sizeof url) : 0;
      FCHECK(n > 0 && std::string(url, n).find("https://") == 0, "its first source: %s", ic.key);
      FCHECK(ic.mark && ic.px == kLogoPx && ic.alt >= 0 && (size_t) ic.alt < wide.texts.size(),
             "a mark with its code meanwhile");
      FCHECK(ic.url(ic.key, ic.kind, 2, url, sizeof url) == 0 && ic.url("", 0, 0, url, sizeof url) == 0 &&
                 ic.url(ic.key, ic.kind, 0, url, 8) == 0,
             "two sources at most, none unknown or cut");
    }
  }
  {
    // A wide card drawn: its plane's photo comes next.
    espn::app_env().version = "2026.11.5";
    espn::AppRequest pr;
    FCHECK(r.app.request(r.now, pr) && pr.url == "https://api.planespotters.net/pub/photos/hex/ac0fdc",
           "Southwest's photo by its hex: %s", pr.url.c_str());
    FCHECK(pr.headers.size() == 1 && pr.headers[0].name == "User-Agent" &&
               pr.headers[0].value ==
                   "GameDayScoreboard/2026.11.5 (+https://github.com/ApolloAutomation/gameday-scoreboard-main)",
           "with a way to reach us");
    espn::AppTextStream in(kSpotted);
    FCHECK(r.app.parse(in, r.now), "its answer");
    r.app.ended(espn::AppResult{200, false, 0});
    r.app.apply(true, r.now);
    espn::AppFrame wide;
    r.app.frame(256, 64, r.now, wide);
    const espn::AppPhoto &ph = wide.photo;
    FCHECK(ph.url == "https://t.plnspttrs.net/05072/1592852_29b08bc9ff_280.jpg" && ph.x == kPhotoX &&
               ph.y == kPhotoY && ph.w == kPhotoW && ph.h == kPhotoH,
           "the photo in the picture's spot: %s", ph.url.c_str());
    FCHECK(wide.art.size() == 1, "the picture until it loads");
    std::string t = wide.ticker;
    t.insert(ph.at, ph.credit);
    FCHECK(t.rfind("27 planes nearby | SWA1678 Southwest", 0) == 0 &&
               t.find(" SW | Photo: A. Spotter | ") != std::string::npos,
           "the credit after the card's plane: %s", t.c_str());
    for (int w : {64, 128}) {
      std::string words[2];
      for (int s : {0, 8}) {
        espn::AppFrame g;
        r.app.shown(true);
        r.app.frame(w, 64, r.now, g);
        if (s > 0)
          r.app.frame(w, 64, r.now + s, g = espn::AppFrame());
        espn::app_frame_trim(g);
        const bool list = s > 0;
        FCHECK(g.photo.url.empty() == (w < 128) && (g.photo.w > 0) == (w >= 128 && !list),
               "%d wide, %s: %s %d", w, list ? "the list" : "the card", g.photo.url.c_str(), g.photo.w);
        words[list] = g.ticker;
        words[list].insert(g.photo.at, g.photo.credit);
      }
      // Credited on both screens, so the ticker never jumps
      FCHECK(words[0] == words[1] && (words[0].find("| Photo: A. Spotter |") == std::string::npos) == (w < 128),
             "%d wide: %s / %s", w, words[0].c_str(), words[1].c_str());
    }
    FCHECK(!r.app.request(r.now + 1, pr) || pr.url.find("planespotters") == std::string::npos,
           "one lookup a plane");
    JsonDocument d;
    r.app.state(d.to<JsonObject>(), r.now);
    FCHECK(d["nearest_hex"] == "ac0fdc" && d["nearest_reg"] == "N8766T", "the page's photo lookup");
  }
  FCHECK(!r.app.request(r.now + 5, req), "not before");
  // Whole seconds against millis: due must read 0, not 20 s.
  FCHECK(r.app.poll_ms(r.now + kFr24PollMs / 1000) == 0, "due at its time: %u",
         r.app.poll_ms(r.now + kFr24PollMs / 1000));

  // Each width: the card, then the list, then the card.
  for (int w : {64, 128}) {
    r.app.shown(true);
    f = espn::AppFrame();
    r.app.frame(w, 64, r.now, f);
    FCHECK(app_has(f, "SWA1678"), "%d: the card first", w);
    FCHECK(f.ticker.rfind("27 planes nearby | SWA1678", 0) == 0, "%d: the ticker: %s", w, f.ticker.c_str());
    f = espn::AppFrame();
    r.app.frame(w, 64, r.now + 3, f);
    FCHECK(app_has(f, "SWA1678") && !app_has(f, "N7QD"), "%d: still the card", w);
    f = espn::AppFrame();
    r.app.frame(w, 64, r.now + 8, f);
    FCHECK(frame_text(f).find("27 planes") != std::string::npos && frame_text(f).find("N7QD") != std::string::npos,
           "%d: then the list: %s", w, frame_text(f).c_str());
    f = espn::AppFrame();
    r.app.frame(w, 64, r.now + 16, f);
    FCHECK(app_has(f, "SWA1678") && !app_has(f, "N7QD"), "%d: then the card again", w);
  }
  for (int w : {192, 256}) {
    f = espn::AppFrame();
    r.app.frame(w, 64, r.now + 8, f);
    FCHECK(f.texts[0].text == "SWA1678" && frame_text(f).find("27 planes") != std::string::npos,
           "%d: card and list together: %s", w, frame_text(f).c_str());
  }
  JsonDocument d;
  r.app.state(d.to<JsonObject>(), r.now);
  FCHECK(d["status"] == "ok" && d["source"] == "fr24" && d["planes"] == 27 && d["nearest"] == "SWA1678" &&
             d["units"] == "imperial" && d["radius"] == 28 && d["fr24"].isNull(),
         "the page's view");

  // Two failures, then the backup; then Flightradar24 again.
  r.wait();
  FCHECK(r.step(503, ""), "a failure");
  r.wait();
  FCHECK(r.step(-1, "") && r.last_url.find("flightradar24") != std::string::npos, "tried again");
  r.wait();
  FCHECK(r.step(200, fi) && r.last_url.find("opendata.adsb.fi") != std::string::npos, "then adsb.fi: %s",
         r.last_url.c_str());
  FCHECK(r.app.source() == Source::ADSBFI, "planes from adsb.fi");
  // Its planes need routes: adsb.im's batch comes next.
  r.wait();
  espn::AppRequest rr;
  FCHECK(r.app.request(r.now, rr) && rr.url == kRoutesetUrl && rr.body.find("\"planes\"") != std::string::npos &&
             rr.headers.size() == 1 && rr.headers[0].value == "application/json",
         "the routes batch: %s", rr.url.c_str());
  r.app.ended(espn::AppResult{403, true, 0});
  r.app.apply(false, r.now);
  r.wait();
  FCHECK(r.app.request(r.now, rr) && rr.url.find("api.adsbdb.com/v0/callsign/") != std::string::npos,
         "adsbdb while adsb.im refuses: %s", rr.url.c_str());
  r.app.ended(espn::AppResult{404, false, 0});
  r.app.apply(false, r.now);
  int fr = 0;
  for (int i = 0; i < 40 && r.app.source() != Source::FR24; i++) {
    r.wait();
    espn::AppRequest q;
    if (!r.app.request(r.now, q))
      continue;
    const bool back = q.url.find("flightradar24") != std::string::npos;
    fr += back;
    espn::AppTextStream in(back ? fr24 : q.url.find("adsb.fi") != std::string::npos ? fi : std::string("[]"));
    const bool ok = q.url.find("adsbdb") == std::string::npos && r.app.parse(in, r.now);
    r.app.ended(espn::AppResult{ok ? 200 : 404, false, 0});
    r.app.apply(ok, r.now);
  }
  FCHECK(fr == 1 && r.app.source() == Source::FR24, "back on Flightradar24 after its wait");

  // A refusal moves on at once and honors Retry-After.
  r.wait();
  FCHECK(r.step(429, "", 600000), "refused");
  r.wait();
  FCHECK(r.step(200, fi) && r.last_url.find("adsb.fi") != std::string::npos, "adsb.fi at once");
  const uint32_t ms = (uint32_t) ((uint64_t) r.now * 1000u);
  FCHECK(r.app.feed().wait[(size_t) Source::FR24].left(ms) >= 590000, "Retry-After honored");

  // A read that never came back counts as a failure.
  Reads lost;
  lost.app.setup();
  lost.app.set("flyhome", "40.71,-74.01");
  espn::AppRequest q;
  FCHECK(lost.app.request(lost.now, q), "a read");
  lost.now += 60;
  FCHECK(!lost.app.request(lost.now, q) && lost.app.poll_ms(lost.now) > 60000, "then a wait");
  FCHECK(lost.app.feed().wait[(size_t) Source::FR24].run.streak == 1, "counted");

  // The receiver first; a setting change drops the read.
  Reads rx;
  rx.app.setup();
  rx.app.set("flyhome", "40.64,-73.78");
  rx.app.set("flyradius", "30");
  rx.app.set("flyreceiver", "192.168.1.50");
  FCHECK(rx.step(200, fi) && rx.last_url == "http://192.168.1.50/tar1090/data/aircraft.json", "the receiver: %s",
         rx.last_url.c_str());
  FCHECK(rx.app.source() == Source::LOCAL && rx.app.nearby().n == kNear, "its planes");
  FCHECK(rx.app.set("flyplace", "JFK") == espn::AppSet::CHANGED && rx.app.nearby().n == kNear, "a name keeps them");
  FCHECK(rx.app.set("flyradius", "5") == espn::AppSet::CHANGED && rx.app.nearby().n == 0, "a radius drops them");
  FCHECK(rx.app.poll_ms(rx.now) == 0, "and reads at once");
  rx.now += 600;
  f = espn::AppFrame();
  rx.app.frame(64, 64, rx.now, f);
  FCHECK(frame_text(f) == "Looking for|planes|", "%s", frame_text(f).c_str());

  // A corrected receiver is read at once.
  Reads fix;
  fix.app.setup();
  fix.app.set("flyhome", "40.64,-73.78");
  fix.app.set("flyreceiver", "192.168.1.50");
  for (int i = 0; i < 2000 && fix.app.feed().wait[(size_t) Source::LOCAL].run.streak < 4; i++) {
    espn::AppRequest fq;
    if (fix.app.request(fix.now, fq)) {
      const bool rx_read = fq.url.find("192.168.1.50") != std::string::npos;
      espn::AppTextStream in(fr24);
      const bool ok = !rx_read && fix.app.parse(in, fix.now);
      fix.app.ended(espn::AppResult{rx_read ? 404 : 200, false, 0});
      fix.app.apply(ok, fix.now);
    }
    fix.now += 1;
  }
  FCHECK(fix.app.feed().wait[(size_t) Source::LOCAL].run.streak >= 4 &&
             fix.app.feed().wait[(size_t) Source::LOCAL].waiting((uint32_t) ((uint64_t) fix.now * 1000u)),
         "the wrong address waits");
  fix.app.set("flyreceiver", "http://192.168.1.50/skyaware/data/aircraft.json");
  FCHECK(fix.step(200, fi) && fix.last_url.find("skyaware") != std::string::npos,
         "the new one now: %s", fix.last_url.c_str());

  // Off screen for a while: the page hears it's old.
  d.clear();
  fix.app.state(d.to<JsonObject>(), fix.now);
  FCHECK(d["status"] == "ok", "fresh");
  d.clear();
  fix.app.state(d.to<JsonObject>(), fix.now + FlightsApp::kOldS + 1);
  FCHECK(d["status"] == "old", "old");

  // Across the 180th meridian: adsb.fi, not a cut box.
  Reads far;
  far.app.setup();
  far.app.set("flyhome", "-16.43,179.38");
  far.app.set("flyradius", "100");
  FCHECK(far.app.request(far.now, req) && req.url.find("adsb.fi") != std::string::npos, "%s", req.url.c_str());
  d.clear();
  far.app.state(d.to<JsonObject>(), far.now);
  FCHECK(d["fr24"] == "skip", "the page hears Flightradar24 is skipped");

  // An empty sky, and no answers at all.
  Reads none;
  none.app.setup();
  none.app.set("flyhome", "40.71,-74.01");
  espn::app_env().tz = 255;  // no zone: metric
  none.step(200, "{\"full_count\":1,\"version\":4}");
  f = espn::AppFrame();
  none.app.frame(128, 64, none.now, f);
  FCHECK(frame_text(f) == "No planes nearby|within 16 km|", "%s", frame_text(f).c_str());
  FCHECK(!none.app.lit(none.now), "an empty sky lets it dim");
  for (int i = 0; i < 6; i++) {
    none.wait();
    none.step(-1, "");
  }
  f = espn::AppFrame();
  none.app.frame(128, 64, none.now, f);
  FCHECK(frame_text(f) == "No planes nearby|within 16 km|", "the last answer while it is fresh");
  none.now += FlightsApp::kOldS;
  f = espn::AppFrame();
  none.app.frame(128, 64, none.now, f);
  FCHECK(frame_text(f) == "Can't reach flight data|", "%s", frame_text(f).c_str());
  d.clear();
  none.app.state(d.to<JsonObject>(), none.now);
  FCHECK(d["status"] == "failing" && d["units"] == "metric", "the page hears it");
  espn::app_env().tz = 0;
}

}

// Planespotters' photos: keys, URLs, answers, the cache.
void test_photo_parts() {
  Plane p;
  strcpy(p.hex, "ac0fdc");
  strcpy(p.reg, "N8766T");
  char key[9];
  FCHECK(photo_key(p, key) && std::string(key) == "ac0fdc", "by hex: %s", key);
  FCHECK_EQ(photo_url(key), "https://api.planespotters.net/pub/photos/hex/ac0fdc");
  p.anon = true;
  FCHECK(photo_key(p, key) && std::string(key) == "N8766T", "a made-up hex: by registration");
  FCHECK_EQ(photo_url(key), "https://api.planespotters.net/pub/photos/reg/N8766T");
  strcpy(p.reg, "g-euus");
  FCHECK(photo_key(p, key) && std::string(key) == "G-EUUS", "upper case: %s", key);
  FCHECK_EQ(photo_url("ABCDEF"), "https://api.planespotters.net/pub/photos/reg/ABCDEF");
  for (const char *bad : {"", "N", "N12 34", "N1/2", "N1?x"}) {
    strcpy(p.reg, bad);
    FCHECK(!photo_key(p, key) && key[0] == '\0', "no key from \"%s\"", bad);
  }
  // Too long for the field: dropped, never cut
  for (const char *reg : {"N123456789", "RA-890012", "RA-89001"}) {
    const bool fits = strlen(reg) < sizeof(p.reg);
    JsonDocument d;
    deserializeJson(d, std::string("{\"hex\":\"~abc123\",\"seen\":1,\"r\":\"") + reg + "\"}");
    FCHECK(plane_from_json(d.as<JsonObjectConst>(), Home(), p) && (std::string(p.reg) == reg) == fits &&
               (p.reg[0] == '\0') == !fits && photo_key(p, key) == fits,
           "%s from a receiver: \"%s\"", reg, p.reg);
    deserializeJson(d, std::string("[\"\",40.7,-73.9,0,3000,200,\"\",\"\",\"B738\",\"") + reg + "\"," +
                           std::to_string(kFr24Now) + ",\"\",\"\",\"\",0,0,\"\"]");
    FCHECK(plane_from_fr24(d.as<JsonArrayConst>(), nyc(), kFr24Now, p) && (std::string(p.reg) == reg) == fits &&
               (p.reg[0] == '\0') == !fits && photo_key(p, key) == fits,
           "%s from Flightradar24: \"%s\"", reg, p.reg);
  }
  p.anon = false;
  for (const char *hex : {"", "abc12", "00000g", "000000"}) {
    strcpy(p.hex, hex);
    FCHECK(!hex_usable(p), "not a hex: \"%s\"", hex);
  }
  FCHECK_EQ(photo_agent(""), "GameDayScoreboard/1.0 (+https://github.com/ApolloAutomation/gameday-scoreboard-main)");

  // What Planespotters answers
  PlanePhoto ph;
  FCHECK(parse_photo(kSpotted, ph) && ph.state == PhotoState::KNOWN, "a photo");
  FCHECK_EQ(ph.url, "https://t.plnspttrs.net/05072/1592852_29b08bc9ff_280.jpg");
  FCHECK_EQ(ph.by, "A. Spotter");
  FCHECK(parse_photo("{\"photos\":[]}", ph) && ph.state == PhotoState::NONE && ph.url[0] == '\0', "none");
  FCHECK(parse_photo("{\"photos\":[{\"thumbnail\":{\"src\":\"https://t.plnspttrs.net/1/2_t.jpg\"}}]}", ph) &&
             ph.state == PhotoState::KNOWN && std::string(ph.url) == "https://t.plnspttrs.net/1/2_t.jpg" &&
             ph.by[0] == '\0',
         "the small one when it's all there is");
  FCHECK(parse_photo("{\"photos\":[{\"thumbnail_large\":{\"src\":\"http://x.net/a.jpg\"}},"
                     "{\"thumbnail_large\":{\"src\":\"https://x.net/b c.jpg\"}},"
                     "{\"thumbnail_large\":{\"src\":\"https://x.net/c.jpg\"},\"photographer\":\"C\"}]}",
                     ph) &&
             std::string(ph.url) == "https://x.net/c.jpg" && std::string(ph.by) == "C",
         "the first one fit to download: %s", ph.url);
  const std::string longurl = "https://t.plnspttrs.net/" + std::string(kPhotoUrlMax, 'a') + ".jpg";
  FCHECK(parse_photo("{\"photos\":[{\"thumbnail_large\":{\"src\":\"" + longurl + "\"}}]}", ph) &&
             ph.state == PhotoState::NONE,
         "too long to keep");
  // A name cut on a whole letter, control characters out
  FCHECK(parse_photo("{\"photos\":[{\"thumbnail\":{\"src\":\"https://a.b/c.jpg\"},\"photographer\":"
                     "\"Bj\\u00f6rn\\tSt\\u00e5hl AAAAAAAAAAAAAAAAAAAAAAAAAAAA\"}]}",
                     ph),
         "a long name");
  FCHECK_EQ(ph.by, "Bj\xc3\xb6rnSt\xc3\xa5hl " + std::string(18, 'A'));
  FCHECK(parse_photo("{\"photos\":[{\"thumbnail\":{\"src\":\"https://a.b/c.jpg\"},\"photographer\":\"" +
                         std::string(30, 'A') + "\\u00f6\"}]}",
                     ph) &&
             std::string(ph.by) == std::string(30, 'A'),
         "not half a letter: %s", ph.by);
  for (const char *bad : {"{}", "[]", "{\"photos\":{}}", "{\"error\":\"no\"}", "nope", ""})
    FCHECK(!parse_photo(bad, ph), "unreadable: %s", bad);

  // The cache: photos a day, misses half that
  PhotoCache c;
  const uint32_t t0 = 1000000;
  FCHECK(c.wants("ac0fdc", t0) && !c.wants("", t0) && !c.wants(nullptr, t0), "wants what it lacks");
  PlanePhoto &k = c.put("ac0fdc", t0);
  FCHECK(k.state == PhotoState::NONE, "a miss until told otherwise");
  k.state = PhotoState::KNOWN;
  c.put("N7QD", t0);
  FCHECK(!c.wants("ac0fdc", t0 + kPhotoKnownS - 1) && c.wants("ac0fdc", t0 + kPhotoKnownS), "a photo for a day");
  FCHECK(!c.wants("N7QD", t0 + kPhotoNoneS - 1) && c.wants("N7QD", t0 + kPhotoNoneS), "a miss for half of one");
  FCHECK(c.find("ac0fdc", t0 + 10) != nullptr && c.find("ac0fdc", t0 + kPhotoKnownS) == nullptr, "found while fresh");
  for (int i = 0; i < (int) kPhotoCache; i++) {
    char hex[9];
    snprintf(hex, sizeof hex, "a%05d", i);
    c.put(hex, t0 + 100 + i);
    c.find("ac0fdc", t0 + 100 + i);
  }
  FCHECK(c.find("ac0fdc", t0 + 200) != nullptr && c.find("a00000", t0 + 200) == nullptr && !c.wants("a00001", t0 + 200),
         "full: the least used goes");
}

// The recording, its times moved on by shift seconds.
std::string fr24_later(const std::string &json, int64_t shift) {
  JsonDocument d;
  deserializeJson(d, json);
  for (JsonPair kv : d.as<JsonObject>())
    if (kv.value().is<JsonArray>())
      kv.value()[10] = kv.value()[10].as<int64_t>() + shift;
  std::string out;
  serializeJson(d, out);
  return out;
}

// Lookups: wide cards only, once a plane, with waits.
void test_photo_reads() {
  const std::string fr24 = slurp(kFr24File);
  Reads r;
  r.app.setup();
  r.app.set("flyhome", "40.71,-74.01");
  r.app.set("flyradius", "28mi");
  espn::app_env().tz = 0;
  r.app.shown(true);
  FCHECK(r.step(200, fr24), "planes");
  // The panel a second at a time, lookups answered
  std::vector<int64_t> asked;
  int status = 503;
  std::string body;
  uint32_t retry_ms = 0;
  // Until secs pass or `until` lookups were made
  auto run = [&](int64_t secs, int w, size_t until = SIZE_MAX) {
    for (int64_t end = r.now + secs; r.now < end && asked.size() < until; r.now++) {
      espn::AppFrame f;
      r.app.frame(w, 64, r.now, f);
      espn::AppRequest q;
      if (!r.app.request(r.now, q))
        continue;
      const bool photo = q.url.find("api.planespotters.net") != std::string::npos;
      if (photo)
        asked.push_back(r.now);
      espn::AppTextStream in(photo ? body : fr24_later(fr24, r.now - kFr24Now));
      const int st = photo ? status : q.url.find("flightradar24") != std::string::npos ? 200 : 404;
      const bool ok = st == 200 && r.app.parse(in, r.now);
      r.app.ended(espn::AppResult{st, espn::is_throttle_status(st), photo ? retry_ms : 0});
      r.app.apply(ok, r.now);
    }
  };
  run(120, 64);
  FCHECK(asked.empty(), "64 wide: none");
  const int64_t wide = r.now;
  run(5, 128);
  FCHECK(asked.size() == 1 && asked[0] - wide <= 1, "128 wide: at once");
  // A server error: a wait, then again
  run(60, 128, 2);
  FCHECK(asked.size() == 2 && asked[1] - asked[0] >= 15 && asked[1] - asked[0] <= 31, "again after %d s",
         asked.size() > 1 ? (int) (asked[1] - asked[0]) : -1);
  // Refused: Retry-After kept
  status = 429;
  retry_ms = 900000;
  r.now++;
  run(1200, 128, 3);
  run(1200, 128, 4);
  FCHECK(asked.size() == 4 && asked[3] - asked[2] >= 900, "Retry-After kept: %d s",
         asked.size() > 3 ? (int) (asked[3] - asked[2]) : -1);
  // No photo: a miss, not asked again for hours
  status = 200;
  body = "{\"photos\":[]}";
  retry_ms = 0;
  const size_t n = asked.size();
  run(3600, 128);
  FCHECK(asked.size() <= n + 1, "a miss once: %zu", asked.size() - n);
  espn::AppFrame g;
  r.app.frame(256, 64, r.now, g);
  FCHECK(g.photo.url.empty() && g.art.size() == 1, "the picture, no photo: %s %zu", g.photo.url.c_str(),
         g.art.size());
  FCHECK(r.app.photos().slots[0].state == PhotoState::NONE, "kept as a miss");
  // Off screen: none until a wide card is drawn again
  Reads s;
  s.app.setup();
  s.app.set("flyhome", "40.71,-74.01");
  s.app.set("flyradius", "28mi");
  s.app.shown(true);
  FCHECK(s.step(200, fr24), "planes");
  s.app.frame(192, 64, s.now, g);
  s.app.shown(false);
  espn::AppRequest q;
  FCHECK(!s.app.request(s.now + 1, q) || q.url.find("planespotters") == std::string::npos, "off screen: none");
}

int run_flight_tests(int &total_checks) {
  test_fr24();
  test_fr24_url();
  test_prefs();
  test_app();
  test_photo_parts();
  test_photo_reads();
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
