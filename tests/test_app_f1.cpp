// The F1 display: its reads, failover, rotation and screens.
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "app_f1.h"
#include "board_layout.h"
#include "panel_text.h"

using namespace espn;

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

#define PCHECK_EQ(a, b)                                                                               \
  do {                                                                                                \
    checks++;                                                                                         \
    const std::string va = (a), vb = (b);                                                             \
    if (va != vb) {                                                                                   \
      failures++;                                                                                     \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                                 \
  } while (0)

std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

size_t glyphs(const std::string &s) {
  size_t n = 0;
  for (char c : s)
    if (((unsigned char) c & 0xC0) != 0x80)
      n++;
  return n;
}

int64_t at(const char *iso) { return parse_iso8601_z(iso); }

const int kWidths[] = {64, 128, 192, 256};

// A stand-in network: fixtures by URL, some hosts down.
struct Net {
  std::string header = "fixtures/f1_header_vegas_pre.json";
  std::string next_body;  // set: next.json's answer
  std::set<std::string> down;
  int status = 500;
  bool refused = false;
  std::vector<AppRequest> asked;
};

std::string fixture_for(const std::string &url, const Net &net) {
  auto has = [&url](const char *s) { return url.find(s) != std::string::npos; };
  if (has("scoreboard/header"))
    return net.header;
  if (has("next.json"))
    return "fixtures/jolpica_next.json";
  if (has("driverStandings"))
    return "fixtures/jolpica_driver_standings.json";
  if (has("constructorStandings"))
    return "fixtures/jolpica_constructor_standings.json";
  if (has("last/results"))
    return "fixtures/jolpica_last_results.json";
  if (has("circuits/617"))
    return "fixtures/f1_circuit_interlagos.json";
  if (has("circuits/"))
    return "fixtures/f1_circuit_vegas.json";
  if (has("athletes/"))
    return "fixtures/f1_athlete_verstappen.json";
  if (has("open-meteo"))
    return "fixtures/openmeteo_vegas.json";
  if (has("api.met.no"))
    return "fixtures/met_vegas.json";
  if (has("scoreboard?dates"))
    return "fixtures/racing_calendar_f1_2026.json";
  return "";
}

// The body read as the firmware reads it.
bool read_body(F1App &a, const std::string &body, int64_t now) {
  JsonDocument filter;
  if (!a.filter(filter)) {
    AppTextStream in(body);
    return a.parse(in, now);
  }
  JsonDocument doc;
  return !deserializeJson(doc, body, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40)) &&
         a.parse_doc(doc.as<JsonObjectConst>(), now);
}

// One due read; false when none.
bool serve(F1App &a, int64_t now, Net &net) {
  AppRequest rq;
  if (!a.request(now, rq))
    return false;
  net.asked.push_back(rq);
  if (net.down.count(url_host(rq.url)) != 0) {
    a.ended(AppResult{net.status, net.refused, 0});
    a.apply(false, now);
    return true;
  }
  const bool next = !net.next_body.empty() && rq.url.find("next.json") != std::string::npos;
  const bool ok = read_body(a, next ? net.next_body : slurp(fixture_for(rq.url, net)), now);
  a.ended(AppResult{200, false, 0});
  a.apply(ok, now);
  return true;
}

// Every read due now.
void settle(F1App &a, int64_t now, Net &net) {
  for (int i = 0; i < 30 && a.poll_ms(now) == 0 && serve(a, now, net); i++) {
  }
}

std::string header_of(const AppRequest &rq, const char *name) {
  for (const AppHeader &h : rq.headers)
    if (h.name == name)
      return h.value;
  return "";
}

// Every cell inside its leaderboard column.
void check_fits(const AppFrame &f, int w, const char *what) {
  const BoardLines &b = f.rows;
  const bool wide = w >= 128;
  PCHECK(b.name.size() <= 6 && b.pos.size() == b.name.size() && b.name_color.size() == b.name.size(),
         "%s at %d: rows", what, w);
  for (size_t i = 0; i < b.name.size(); i++) {
    const size_t name_max = wide ? 11 : b.value_narrow ? 7 : 10;
    PCHECK(glyphs(b.pos[i]) <= 2, "%s at %d: pos '%s'", what, w, b.pos[i].c_str());
    PCHECK(glyphs(b.name[i]) <= name_max, "%s at %d: name '%s'", what, w, b.name[i].c_str());
    PCHECK(glyphs(b.value[i]) <= 3 && (wide || b.value_narrow || b.value[i].empty()), "%s at %d: value '%s'", what,
           w, b.value[i].c_str());
    PCHECK(glyphs(b.detail[i]) <= 6 && (wide || b.detail[i].empty()), "%s at %d: detail '%s'", what, w,
           b.detail[i].c_str());
  }
  if (wide) {
    const size_t room = (size_t) (w - 2) / 5;
    const size_t used = glyphs(b.title) + (b.status.empty() ? 0 : glyphs(b.status) + 1);
    PCHECK(used <= room, "%s at %d: title '%s' and status '%s'", what, w, b.title.c_str(), b.status.c_str());
  } else {
    PCHECK(glyphs(b.status) <= 12, "%s at %d: status '%s'", what, w, b.status.c_str());
  }
  for (const AppPoint &p : f.line.pts)
    PCHECK(p.x >= 0 && p.x < w && p.y >= 8 && p.y < 56, "%s at %d: point %d,%d", what, w, p.x, p.y);
  PCHECK(!f.ticker.empty(), "%s at %d: a ticker", what, w);
  PCHECK(app_frame_board(f) || !f.message.empty(), "%s at %d: rows or words", what, w);
}

// The frame of screen s, found by stepping the clock.
bool frame_of(const F1App &a, F1Screen s, int64_t from, int w, AppFrame &out) {
  for (int64_t t = from; t < from + 120; t++) {
    if (a.screen(t) != s)
      continue;
    out = AppFrame{};
    a.frame(w, 64, t, out);
    return true;
  }
  return false;
}

void test_helpers() {
  PCHECK_EQ(f1_met_url(361750, -1151364),
            "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=36.1750&lon=-115.1364");
  PCHECK_EQ(f1_met_url(-237011, 5), "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=-23.7011&lon=0.0005");
  PCHECK_EQ(f1_met_agent("2026.11.2"), "GameDayScoreboard/2026.11.2 github.com/ApolloAutomation/gameday-scoreboard-main");
  PCHECK(met_weather_code("clearsky_night") == 0 && met_weather_code("fair_day") == 1 &&
             met_weather_code("partlycloudy_polartwilight") == 2 && met_weather_code("cloudy") == 3 &&
             met_weather_code("fog") == 45,
         "skies");
  PCHECK(met_weather_code("lightrain") == 61 && met_weather_code("rain") == 63 && met_weather_code("heavyrain") == 65,
         "rain");
  PCHECK(met_weather_code("lightrainshowers_day") == 80 && met_weather_code("heavyrainshowers_night") == 82, "showers");
  PCHECK(met_weather_code("lightsnow") == 71 && met_weather_code("heavysnowshowers_day") == 86, "snow");
  PCHECK(met_weather_code("sleet") == 67 && met_weather_code("lightssleetshowersandthunder") == 95, "sleet, thunder");
  PCHECK(met_weather_code("rainandthunder") == 95 && met_weather_code("nonsense") == 255, "thunder, unknown");
  PCHECK(!f1_weather_word(met_weather_code("heavyrain")).empty(), "a word for each");

  PCHECK(f1_units_for_tz(0).fahrenheit && f1_units_for_tz(6).mph, "US zones");
  PCHECK(!f1_units_for_tz(7).fahrenheit && !f1_units_for_tz(7).mph && !f1_units_for_tz(13).fahrenheit,
         "Canada and UTC are metric");
  PCHECK(f1_units_for_tz(10).mph && !f1_units_for_tz(10).fahrenheit, "the UK: mph and miles, Celsius");
  PCHECK(!f1_units_for_tz(200).fahrenheit, "out of range");

  PCHECK_EQ(f1_cell("VER", "+13.07", 10), "VER +13.07");
  PCHECK_EQ(f1_cell("NOR", "390", 10), "NOR    390");
  PCHECK_EQ(f1_cell("Air", "16\xC2\xB0" "C", 10), "Air   16\xC2\xB0" "C");
  PCHECK_EQ(f1_cell("Antonelli", "+13.07", 10), "Ant +13.07");
  PCHECK_EQ(f1_cell("VER", "", 10), "VER");
  PCHECK_EQ(f1_cell("VER", "0123456789AB", 10), "0123456789");
  PCHECK_EQ(f1_age_text(20), "1m");
  PCHECK_EQ(f1_age_text(45 * 60), "45m");
  PCHECK_EQ(f1_age_text(5 * 3600 + 10), "5h");
  PCHECK_EQ(f1_age_text(3 * 86400), "3d");
}

void test_short_name() {
  PCHECK_EQ(f1_short_name("Spa-Francorchamps", 12), "Spa");
  PCHECK_EQ(f1_short_name("Hermanos Rodriguez", 12), "Hermanos");
  PCHECK_EQ(f1_short_name("Gilles Villeneuve", 12), "Gilles");
  PCHECK_EQ(f1_short_name("Red Bull Ring", 12), "Red Bull");
  PCHECK_EQ(f1_short_name("Enzo e Dino Ferrari", 12), "Enzo e Dino");
  PCHECK_EQ(f1_short_name("Las Vegas", 12), "Las Vegas");
  for (const CircuitInfo &c : kCircuits) {
    const std::string n = f1_short_name(c.name, 12);
    PCHECK(glyphs(n) <= 12 && std::string(c.name).compare(0, n.size(), n) == 0, "%s: %s", c.name, n.c_str());
    PCHECK(n.size() == strlen(c.name) || c.name[n.size()] == ' ' || c.name[n.size()] == '-', "%s: whole words: %s",
           c.name, n.c_str());
  }
}

void test_met() {
  // 05:04 picks the 05:00 hour, the nearest not ahead.
  F1Weather w;
  AppTextStream v(slurp("fixtures/met_vegas.json"));
  PCHECK(parse_met_weather(v, at("2026-11-12T05:04:00Z"), w) == 1, "Las Vegas parses");
  PCHECK(w.valid && w.at == at("2026-11-12T05:00:00Z"), "the 05:00 hour");
  PCHECK(w.temp_c10 == 163 && w.wind_kmh10 == 54 && w.code == 0 && w.precip_mm10 == 0 && w.rain_pct == -1,
         "16.3 C, 1.5 m/s, clear: %d %d %d", w.temp_c10, w.wind_kmh10, w.code);
  // Before the first hour, the first.
  AppTextStream early(slurp("fixtures/met_vegas.json"));
  PCHECK(parse_met_weather(early, at("2026-11-11T00:00:00Z"), w) == 1 && w.at == at("2026-11-12T04:00:00Z") &&
             w.temp_c10 == 168,
         "the first hour");
  // Long after, the last it looks at.
  AppTextStream late(slurp("fixtures/met_vegas.json"));
  PCHECK(parse_met_weather(late, at("2026-12-01T00:00:00Z"), w) == 1 && w.at == at("2026-11-14T03:00:00Z"),
         "48 hours on at most");

  F1Weather m;
  AppTextStream mon(slurp("fixtures/met_monaco.json"));
  PCHECK(parse_met_weather(mon, at("2026-11-12T05:30:00Z"), m) == 1, "Monaco parses");
  PCHECK(m.code == 65 && m.precip_mm10 == 24 && m.temp_c10 == 145 && m.wind_kmh10 == 191,
         "heavy rain, 2.4 mm, 14.5 C, 5.3 m/s: %d %d %d %d", m.code, m.precip_mm10, m.temp_c10, m.wind_kmh10);
  const F1Units metric{false, false};
  const F1Frame f = f1_weather_frame(m, "Monaco", metric, 128);
  PCHECK(f.n >= 3 && f.status == "Rain", "the core's frame takes it: %s", f.status.c_str());

  F1Weather none;
  for (const char *bad : {"", "{\"type\":\"Feature\"}", "[1,2,3]", "{\"properties\":{\"timeseries\":[]}}",
                          "{\"properties\":{\"timeseries\":[{\"time\":\"x\"}]}}", "<html>not json</html>"}) {
    AppTextStream b(bad);
    PCHECK(parse_met_weather(b, 0, none) == -1 && !none.valid, "not a forecast: %s", bad);
  }
}

void test_header_schedule() {
  for (const char *file : {"fixtures/f1_header_vegas_pre.json", "fixtures/racing_header_f1_live.json",
                           "fixtures/racing_header_f1_sprint.json"}) {
    JsonDocument filter, doc;
    espn::detail::fill_f1_live_filter(filter);
    const std::string json = slurp(file);
    PCHECK(!deserializeJson(doc, json, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40)),
           "%s reads", file);
    F1Live live;
    PCHECK(espn::detail::f1_live_from_doc(doc.as<JsonObjectConst>(), live) == 1, "%s parses", file);
    F1Schedule s;
    PCHECK(f1_header_schedule(doc.as<JsonObjectConst>(), live, s) == 1 && s.valid, "%s: a schedule", file);
    for (size_t i = 1; i < s.n; i++)
      PCHECK(s.sessions[i - 1].start <= s.sessions[i].start, "%s: by start", file);
    PCHECK(s.has_pos && circuit_for(s) != nullptr, "%s: its circuit, %s", file, s.circuit);
    if (strstr(file, "vegas") != nullptr) {
      PCHECK(s.n == 5 && s.sessions[4].kind == RaceKind::RACE && s.sessions[4].start == at("2026-11-22T04:00:00Z"),
             "Vegas: five, the race last");
      PCHECK(s.sessions[0].kind == RaceKind::PRACTICE && s.sessions[0].number == 1 &&
                 s.sessions[3].kind == RaceKind::QUALIFYING,
             "FP1 first, then qualifying");
      PCHECK(std::string(s.circuit_id) == "vegas" && s.season == 2026, "Jolpica's id, the season");
    }
    if (strstr(file, "sprint") != nullptr)
      PCHECK(s.n == 3 && s.sessions[1].kind == RaceKind::SPRINT_QUALIFYING && s.sessions[2].kind == RaceKind::SPRINT,
             "a sprint weekend's sessions");
  }
  F1Live none;
  F1Schedule s;
  JsonDocument empty;
  PCHECK(f1_header_schedule(empty.as<JsonObjectConst>(), none, s) == 0 && !s.valid, "no weekend");

  GolfNext n;
  n.valid = true;
  n.name = "Mexico City Grand Prix";
  n.start_epoch = at("2026-10-30T18:30:00Z");
  n.end_epoch = at("2026-11-01T20:00:00Z");
  const F1Schedule c = f1_calendar_schedule(n);
  PCHECK(c.valid && c.n == 2 && c.sessions[0].kind == RaceKind::PRACTICE && c.sessions[1].kind == RaceKind::RACE &&
             c.sessions[1].start == n.end_epoch,
         "the calendar's weekend");
  PCHECK(c.has_pos && std::string(c.circuit_id) == "rodriguez", "its circuit by name");
  PCHECK(!f1_calendar_schedule(GolfNext{}).valid, "none");
}

void test_host() {
  F1Host h;
  const int64_t t = 1000000;
  PCHECK(h.may_read(t) && !h.backup(), "fresh");
  PCHECK(!h.fail(t, false, 0, 7, true) && h.fails == 1 && h.wait_until > t && !h.may_read(t), "one failure waits");
  PCHECK(!h.fail(h.wait_until, false, 0, 7, true) && !h.backup(), "two");
  int64_t now = h.wait_until;
  PCHECK(h.fail(now, false, 0, 7, true) && h.backup() && h.out_until >= now + 30 * 60, "the third fails over");
  PCHECK(!h.may_read(now + 60) && h.may_read(h.out_until), "tried again after the wait");
  now = h.out_until;
  PCHECK(!h.fail(now, false, 0, 7, true) && h.out_until >= now + 60 * 60, "a failed try waits twice as long");
  h.ok();
  PCHECK(!h.backup() && h.fails == 0 && h.may_read(now), "an answer goes back");

  F1Host r;
  PCHECK(r.fail(t, true, 0, 7, true) && r.backup() && r.refused, "a refusal fails over at once");
  F1Host ra;
  ra.fail(t, true, 45 * 60 * 1000, 7, true);
  PCHECK(ra.out_until >= t + 45 * 60, "Retry-After is honored: %lld", (long long) (ra.out_until - t));
  F1Host e;
  for (int i = 0; i < 5; i++)
    e.fail(t, i == 4, 0, 7, false);
  PCHECK(!e.backup() && e.wait_until > t, "no backup: waits only");
}

void test_reads_and_screens() {
  app_env().tz = 13;  // the panel in UTC
  app_env().version = "2026.11.2";
  F1App a;
  Net net;
  const int64_t t0 = at("2026-11-12T04:00:00Z");
  PCHECK(a.own_backoff() && a.id() == Display::F1, "its own waits");
  PCHECK(a.poll_ms(t0) == 0, "everything due at first");

  AppFrame empty;
  a.frame(128, 64, t0, empty);
  PCHECK(!app_frame_board(empty) && empty.message == "F1", "nothing yet: the name");

  settle(a, t0, net);
  PCHECK(net.asked.size() == 8, "eight reads: %zu", net.asked.size());
  const char *order[] = {"scoreboard/header", "next.json", "driverStandings", "open-meteo", "circuits/4250",
                         "athletes/", "last/results", "constructorStandings"};
  for (size_t i = 0; i < 8 && i < net.asked.size(); i++)
    PCHECK(net.asked[i].url.find(order[i]) != std::string::npos, "read %zu: %s", i, net.asked[i].url.c_str());
  for (const AppRequest &rq : net.asked) {
    const std::string host = url_host(rq.url);
    const std::string ua = header_of(rq, "User-Agent");
    if (host == "api.jolpi.ca" || host == "api.open-meteo.com")
      PCHECK(ua == kF1UserAgent, "%s sends its agent", rq.url.c_str());
    else
      PCHECK(ua.empty() && rq.body.empty(), "%s keeps the panel's", rq.url.c_str());
  }
  AppRequest idle;
  PCHECK(a.poll_ms(t0) > 0 && !a.request(t0, idle), "nothing due now");
  // An hour on, the weather again; standings within six hours.
  Net later = net;
  later.asked.clear();
  settle(a, t0 + 3600, later);
  bool hourly = false;
  for (const AppRequest &rq : later.asked)
    hourly = hourly || rq.url.find("open-meteo") != std::string::npos;
  PCHECK(hourly, "hourly weather");

  F1Screen list[8];
  const size_t n = a.rotation(t0, list, 8);
  PCHECK(n == 6 && list[0] == F1Screen::NEXT && list[1] == F1Screen::CIRCUIT && list[2] == F1Screen::WEATHER &&
             list[5] == F1Screen::LAST_RACE,
         "six cards: %zu", n);
  // 8 to 10 seconds a card.
  int changes = 0;
  for (int64_t t = t0; t < t0 + 58; t++)
    changes += a.screen(t) != a.screen(t + 1) ? 1 : 0;
  PCHECK(changes == 6, "each card once a cycle: %d", changes);
  // Redrawn as the card changes, not every second.
  for (int64_t t = t0; t < t0 + 58; t++) {
    AppFrame f;
    a.frame(128, 64, t, f);
    uint32_t left = 0;
    a.screen(t, &left);
    PCHECK(a.redraw_ms() == left * 1000 && left >= 1 && left <= 10, "redraw with the card: %u", a.redraw_ms());
  }
  PCHECK(!a.busy(t0), "not busy days out");

  for (int w : kWidths)
    for (F1Screen s : {F1Screen::NEXT, F1Screen::CIRCUIT, F1Screen::WEATHER, F1Screen::DRIVERS,
                       F1Screen::CONSTRUCTORS, F1Screen::LAST_RACE}) {
      AppFrame f;
      PCHECK(frame_of(a, s, t0, w, f), "screen %d found", (int) s);
      check_fits(f, w, "card");
      PCHECK(app_frame_board(f), "screen %d at %d is a board", (int) s, w);
    }

  AppFrame f;
  frame_of(a, F1Screen::NEXT, t0, 128, f);
  PCHECK_EQ(f.rows.name[0], "Practice 1");
  PCHECK_EQ(f.rows.value[0], "Fri");
  PCHECK_EQ(f.rows.detail[0], "12:30a");
  PCHECK_EQ(f.rows.name[4], "Race");
  PCHECK_EQ(f.rows.status, "7d 20h");
  PCHECK(f.rows.name_color[0] == 0xFFD000 && f.rows.name_color[1] == 0xFFFFFF, "the next in gold");
  PCHECK(f.ticker.find("Las Vegas Grand Prix") != std::string::npos &&
             f.ticker.find("Weather at Las Vegas") != std::string::npos &&
             f.ticker.find("Drivers:") != std::string::npos && f.ticker.find("Constructors:") != std::string::npos &&
             f.ticker.find("Last race") != std::string::npos,
         "one ticker for the rotation: %s", f.ticker.c_str());
  PCHECK(!f.spans.empty(), "names in team colors");
  const std::string marked = esphome::gameday::ticker_markup(esphome::gameday::panel_text(f.ticker), f.spans);
  PCHECK(marked.find('`') != std::string::npos, "marked up: %s", marked.c_str());
  AppFrame g;
  frame_of(a, F1Screen::DRIVERS, t0, 128, g);
  PCHECK(g.ticker == f.ticker, "the same ticker on every card");

  frame_of(a, F1Screen::NEXT, t0, 64, f);
  PCHECK_EQ(f.rows.pos[0], "Fr");
  PCHECK_EQ(f.rows.name[0], "FP1 12:30a");
  PCHECK_EQ(f.rows.name[3], "Qual 4:00a");
  PCHECK(!f.rows.value_narrow, "names take the value's room");

  frame_of(a, F1Screen::WEATHER, t0, 128, f);
  PCHECK_EQ(f.rows.name[0], "Clear sky");
  PCHECK_EQ(f.rows.name[1], "Air temp");
  PCHECK_EQ(f.rows.detail[1], "16\xC2\xB0" "C");
  PCHECK_EQ(f.rows.detail[3], "10km/h");
  frame_of(a, F1Screen::WEATHER, t0, 64, f);
  PCHECK_EQ(f.rows.name[0], "Air   16\xC2\xB0" "C");
  PCHECK_EQ(f.rows.name[2], "Wind 10kph");
  app_env().tz = 0;  // the panel in US Eastern
  frame_of(a, F1Screen::WEATHER, t0, 128, f);
  PCHECK_EQ(f.rows.detail[1], "61\xC2\xB0" "F");
  PCHECK_EQ(f.rows.detail[3], "6 mph");
  frame_of(a, F1Screen::NEXT, t0, 128, f);
  PCHECK_EQ(f.rows.value[0], "Fri");  // still UTC: the host has no zone
  app_env().tz = 13;

  frame_of(a, F1Screen::CIRCUIT, t0, 128, f);
  PCHECK(f.line.pts.size() >= 20 && f.line.pts.size() <= kAppLinePoints, "the outline: %zu", f.line.pts.size());
  for (const AppPoint &p : f.line.pts)
    PCHECK(p.x >= 74 && p.x <= 126, "right of the facts: %d", p.x);
  PCHECK(f.line.pts.front() == f.line.pts.back(), "closed");
  PCHECK_EQ(f.rows.name[0], "6.116 km");  // ESPN's length over F1DB's
  PCHECK_EQ(f.rows.name[3], "Rec 1:33.36");
  PCHECK_EQ(f.rows.name[4], "VER 2025");
  frame_of(a, F1Screen::CIRCUIT, t0, 64, f);
  PCHECK(f.rows.name.empty() && f.line.pts.size() >= 20 && !f.rows.status.empty(), "one panel: the outline alone");
  frame_of(a, F1Screen::CIRCUIT, t0, 256, f);
  int16_t lo = 255, hi = 0;
  for (const AppPoint &p : f.line.pts) {
    lo = std::min(lo, p.x);
    hi = std::max(hi, p.x);
  }
  PCHECK(lo >= 138 && hi - lo > 60, "a bigger outline four wide: %d-%d", lo, hi);

  frame_of(a, F1Screen::DRIVERS, t0, 64, f);
  PCHECK(f.rows.name[0].size() == 10 && f.rows.name[0].substr(0, 3) == "ANT", "code, then points: %s",
         f.rows.name[0].c_str());
  frame_of(a, F1Screen::CONSTRUCTORS, t0, 128, f);
  for (const std::string &name : f.rows.name)
    PCHECK(glyphs(name) <= 11, "a team's name fits: %s", name.c_str());
  frame_of(a, F1Screen::LAST_RACE, t0, 128, f);
  PCHECK(f.rows.status == "Last race" && !f.rows.detail[0].empty(), "the last race: %s", f.rows.detail[0].c_str());

  JsonDocument doc;
  a.state(doc.to<JsonObject>());
  PCHECK(std::string(doc["schedule"] | "") == "jolpica" && std::string(doc["weather"] | "") == "open-meteo" &&
             doc["standings_at"].as<int64_t>() == t0,
         "its state");

  // Busy from the schedule alone, through the night clock.
  const int64_t fp1 = at("2026-11-20T00:30:00Z");
  PCHECK(a.busy(fp1 - 5 * 60) && a.busy(fp1 + 60 * 60) && !a.busy(fp1 + 2 * 3600), "busy for practice 1");
  PCHECK(a.busy(at("2026-11-22T06:30:00Z")) && !a.busy(at("2026-11-22T07:30:00Z")), "busy three hours for the race");
}

void test_live() {
  app_env().tz = 13;
  F1App a;
  Net net;
  net.header = "fixtures/racing_header_f1_live.json";
  const int64_t t = at("2026-10-25T21:00:00Z");
  settle(a, t, net);
  F1Screen list[8];
  PCHECK(a.rotation(t, list, 8) == 1 && list[0] == F1Screen::LIVE && a.screen(t) == F1Screen::LIVE,
         "on track: the order alone");
  PCHECK(a.busy(t), "busy on track");
  AppFrame once;
  a.frame(128, 64, t, once);
  PCHECK(a.redraw_ms() == 60000, "the order alone redraws on reads");
  PCHECK(a.poll_ms(t + 1) > 0 && a.poll_ms(t + 1) <= 30000, "30 seconds on track: %u", a.poll_ms(t + 1));
  for (int w : kWidths) {
    AppFrame f;
    a.frame(w, 64, t, f);
    check_fits(f, w, "live");
    PCHECK(f.rows.name.size() == 6, "six rows at %d", w);
  }
  AppFrame f;
  a.frame(128, 64, t, f);
  PCHECK_EQ(f.rows.status, "Lap 34/56");
  PCHECK_EQ(f.rows.name[0], "Antonelli");
  PCHECK_EQ(f.rows.value[0], "1");
  PCHECK_EQ(f.rows.detail[1], "+13.07");
  PCHECK(f.rows.name_color[0] == 0x00D2BE, "Mercedes' color");
  PCHECK(f.ticker.find("lap 34") != std::string::npos && f.spans[0].text == "Antonelli", "the order's ticker: %s",
         f.ticker.c_str());
  f = AppFrame{};
  a.frame(64, 64, t, f);
  PCHECK_EQ(f.rows.name[0].substr(0, 3), "ANT");
  PCHECK_EQ(f.rows.name[1], "LEC +13.07");
  // Codes from the standings, read after the header.
  const F1Live &live = a.live();
  const std::string standings = slurp("fixtures/jolpica_driver_standings.json");
  F1Standings st;
  PCHECK(parse_f1_standings(standings, true, st) == 1, "standings");
  int matched = 0;
  for (size_t i = 0; i < live.snap.race.rows.size(); i++)
    for (size_t k = 0; k < st.n; k++)
      if (std::string(live.snap.race.rows[i].name) == st.rows[k].name) {
        PCHECK(std::string(live.cars[i].code) == st.rows[k].code, "%s's code", st.rows[k].name);
        matched++;
      }
  PCHECK(matched >= 5, "codes matched: %d", matched);

  // An edge's older copy keeps the order, twice at most.
  std::string older = slurp("fixtures/racing_header_f1_live.json");
  const size_t lap = older.find("\"period\":34");
  PCHECK(lap != std::string::npos, "the live lap");
  older.replace(lap, 11, "\"period\":32");
  for (int i = 0; i < 3; i++) {
    AppRequest orq;
    PCHECK(a.request(t + 31 + i * 31, orq), "a header read %d", i);
    PCHECK(read_body(a, older, t + 31 + i * 31), "parses %d", i);
    a.ended(AppResult{200, false, 0});
    a.apply(true, t + 31 + i * 31);
    AppFrame of;
    a.frame(128, 64, t + 31 + i * 31, of);
    PCHECK_EQ(of.rows.status, i < 2 ? "Lap 34/56" : "Lap 32/56");
  }

  std::string red = slurp("fixtures/racing_header_f1_live.json");
  const size_t at_period = red.find("\"period\":34");
  PCHECK(at_period != std::string::npos, "the live session");
  red.insert(at_period, "\"flag\":\"RED\",");
  F1App b;
  AppRequest rq;
  PCHECK(b.request(t, rq), "a header read");
  PCHECK(read_body(b, red, t), "parses");
  b.apply(true, t);
  AppFrame rf;
  b.frame(128, 64, t, rf);
  PCHECK(rf.rows.status == "Red flag" && !rf.spans.empty() && rf.spans[0].text == "red flag" &&
             rf.ticker.find("red flag") != std::string::npos,
         "red flag: %s", rf.ticker.c_str());

  // The final: its order stays a day in the rotation.
  F1App c;
  Net fin;
  fin.header = "fixtures/racing_header_f1_final.json";
  const int64_t after = at("2026-10-26T02:00:00Z");
  settle(c, after, fin);
  PCHECK(c.rotation(after, list, 8) >= 2 && list[0] == F1Screen::LIVE, "the final's order, then the rest");
  PCHECK(c.rotation(at("2026-10-27T02:00:00Z"), list, 8) >= 1 && list[0] != F1Screen::LIVE, "gone a day on");
  // An older live copy after the flag doesn't bring it back.
  AppRequest crq;
  PCHECK(c.request(after + 3600, crq), "a header read after the flag");
  PCHECK(read_body(c, slurp("fixtures/racing_header_f1_live.json"), after + 3600), "parses");
  c.ended(AppResult{200, false, 0});
  c.apply(true, after + 3600);
  PCHECK(c.live().snap.race.phase == RacePhase::FINAL && !c.busy(after + 3600), "still the final");
}

void test_failover() {
  app_env().tz = 13;
  app_env().version = "2026.11.2";
  const int64_t t0 = at("2026-11-12T04:00:00Z");
  {
    // Jolpica keeps failing: the header's weekend, the standings kept.
    F1App a;
    Net net;
    settle(a, t0, net);
    net.down.insert("api.jolpi.ca");
    int64_t now = t0 + 7 * 3600;
    int jolpica = 0;
    for (int i = 0; i < 40 && !a.host(F1App::HOST_JOLPICA).backup(); i++) {
      int64_t when;
      a.next_read(now, when);
      now = when > now ? when : now;
      const size_t before = net.asked.size();
      serve(a, now, net);
      if (net.asked.size() > before && url_host(net.asked.back().url) == "api.jolpi.ca")
        jolpica++;
    }
    PCHECK(a.host(F1App::HOST_JOLPICA).backup() && jolpica == 3, "three failures fail over: %d", jolpica);
    PCHECK(std::string(a.schedule(now).race).find("Heineken") != std::string::npos, "the header's schedule: %s",
           a.schedule(now).race);
    AppFrame f;
    PCHECK(frame_of(a, F1Screen::DRIVERS, now, 128, f), "the standings stay");
    PCHECK(f.rows.status.find("ago") != std::string::npos && f.rows.status_color == 0xFFA000, "with their age: %s",
           f.rows.status.c_str());
    PCHECK(f.ticker.find("ago)") != std::string::npos, "the ticker says so");
    frame_of(a, F1Screen::DRIVERS, now, 64, f);
    PCHECK(f.rows.status.find("Drivers ") == 0 && glyphs(f.rows.status) <= 12, "one panel: %s", f.rows.status.c_str());
    JsonDocument doc;
    a.state(doc.to<JsonObject>());
    PCHECK(std::string(doc["schedule"] | "") == "espn", "the state says so");
    // No Jolpica read until the wait is over.
    const int64_t back = a.host(F1App::HOST_JOLPICA).out_until;
    PCHECK(back >= now + 30 * 60, "half an hour out");
    Net quiet = net;
    quiet.asked.clear();
    for (int64_t t = now; t < back; t += 300)
      settle(a, t, quiet);
    for (const AppRequest &rq : quiet.asked)
      PCHECK(url_host(rq.url) != "api.jolpi.ca", "no Jolpica read while out: %s", rq.url.c_str());
    // Then a try; an answer brings it back.
    net.down.clear();
    settle(a, back, net);
    PCHECK(!a.host(F1App::HOST_JOLPICA).backup(), "back to Jolpica");
    PCHECK(std::string(a.schedule(back).race) == "Las Vegas Grand Prix", "its schedule again");
  }
  {
    // A refusal fails over at once.
    F1App a;
    Net net;
    net.down.insert("api.jolpi.ca");
    net.status = 429;
    net.refused = true;
    settle(a, t0, net);
    PCHECK(a.host(F1App::HOST_JOLPICA).backup(), "429: the backup");
    PCHECK(a.schedule(t0).valid, "a schedule from the header");
  }
  {
    // After the race, ESPN's calendar names the next weekend.
    F1App a;
    Net net;
    net.header = "fixtures/racing_header_f1_final.json";
    net.down.insert("api.jolpi.ca");
    net.status = 403;
    net.refused = true;
    const int64_t t = at("2026-10-27T12:00:00Z");
    settle(a, t, net);
    bool calendar = false;
    for (const AppRequest &rq : net.asked)
      calendar = calendar || rq.url.find("scoreboard?dates=") != std::string::npos;
    PCHECK(calendar, "the calendar read");
    const F1Schedule &s = a.schedule(t);
    PCHECK(s.valid && std::string(s.race).find("Mexico") != std::string::npos && s.has_pos, "the next weekend: %s",
           s.race);
  }
  {
    // Open-Meteo keeps failing: MET Norway, then back.
    F1App a;
    Net net;
    net.down.insert("api.open-meteo.com");
    int64_t now = t0;
    for (int i = 0; i < 60 && !a.weather_backup(); i++) {
      int64_t when;
      a.next_read(now, when);
      now = when > now ? when : now;
      serve(a, now, net);
    }
    PCHECK(a.host(F1App::HOST_OPEN_METEO).backup() && a.weather_backup(), "MET Norway's weather");
    const AppRequest &met = net.asked.back();
    PCHECK(met.url.find("api.met.no") != std::string::npos &&
               header_of(met, "User-Agent") ==
                   "GameDayScoreboard/2026.11.2 github.com/ApolloAutomation/gameday-scoreboard-main",
               "MET's agent: %s", met.url.c_str());
    AppFrame f;
    PCHECK(frame_of(a, F1Screen::WEATHER, now, 128, f), "the weather card");
    PCHECK(f.rows.name[0] == "Clear sky" && f.rows.detail[1].find("\xC2\xB0" "C") != std::string::npos, "from MET: %s %s",
           f.rows.name[0].c_str(), f.rows.detail[1].c_str());
    check_fits(f, 128, "MET weather");
    // Its hour up, Open-Meteo is asked again.
    net.down.clear();
    const int64_t back = a.host(F1App::HOST_OPEN_METEO).out_until;
    settle(a, back + 3600, net);
    PCHECK(!a.weather_backup() && !a.host(F1App::HOST_OPEN_METEO).backup(), "Open-Meteo again");
  }
  {
    // ESPN failing waits on ESPN only.
    F1App a;
    Net net;
    net.down.insert("site.api.espn.com");
    settle(a, t0, net);
    PCHECK(a.host(F1App::HOST_ESPN).fails == 1 && !a.host(F1App::HOST_ESPN).backup(), "the header waits");
    PCHECK(a.host(F1App::HOST_JOLPICA).fails == 0 && a.schedule(t0).valid, "Jolpica still read");
    AppFrame f;
    a.frame(128, 64, t0, f);
    PCHECK(app_frame_board(f), "the cards without ESPN");
  }
}

void test_off_season() {
  app_env().tz = 13;
  F1App a;
  Net net;
  net.header = "fixtures/racing_header_f1_final.json";
  net.next_body = R"({"MRData":{"RaceTable":{"season":"2026","Races":[]}}})";
  int64_t now = at("2026-12-20T00:00:00Z");
  const int64_t end = now + 24 * 3600;
  settle(a, now, net);
  const size_t first = net.asked.size();
  while (now < end) {
    int64_t when;
    if (a.next_read(now, when) == F1App::Read::NONE || when >= end)
      break;
    now = when > now ? when : now;
    serve(a, now, net);
  }
  int standings = 0, weather = 0;
  for (size_t i = first; i < net.asked.size(); i++) {
    const std::string &u = net.asked[i].url;
    standings += u.find("driverStandings") != std::string::npos;
    weather += u.find("open-meteo") != std::string::npos || u.find("met.no") != std::string::npos;
  }
  PCHECK(standings <= 4, "standings every 6 hours off season: %d", standings);
  PCHECK(weather == 0, "no weather off season: %d", weather);
}

void test_nothing() {
  F1App a;
  Net net;
  net.down = {"site.api.espn.com", "api.jolpi.ca", "api.open-meteo.com", "api.met.no"};
  int64_t now = at("2026-11-12T04:00:00Z");
  for (int i = 0; i < 40; i++) {
    int64_t when;
    if (a.next_read(now, when) == F1App::Read::NONE)
      break;
    now = when > now ? when : now;
    serve(a, now, net);
  }
  AppFrame f;
  a.frame(64, 64, now, f);
  PCHECK(!app_frame_board(f) && f.message == "F1" && f.ticker == "Can't reach the F1 schedule", "says why: %s",
         f.ticker.c_str());
  PCHECK(a.poll_ms(now) > 0, "and waits");
}

}

int run_app_f1_tests(int &total_checks) {
  test_helpers();
  test_short_name();
  test_met();
  test_header_schedule();
  test_host();
  test_reads_and_screens();
  test_live();
  test_failover();
  test_off_season();
  test_nothing();
  app_env() = AppEnv{};
  total_checks += checks;
  return failures;
}
