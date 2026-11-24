// Every request line and User-Agent fit the HTTP send buffer.
#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "app_f1.h"
#include "app_flights.h"
#include "app_markets.h"
#include "app_weather.h"
#include "espn_parse.h"
#include "f1.h"
#include "flights.h"
#include "markets.h"
#include "poll_timing.h"
#include "source_hockeytech.h"
#include "source_mlbstats.h"
#include "weather.h"

using espn::f1_met_agent;

namespace {

int checks = 0;
int failures = 0;

#define RCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

const int64_t kNow = 1794770400;

// http_request's buffer_size_tx, 512 when the YAML leaves it out.
int yaml_tx_size(bool &found_file) {
  std::ifstream f("../firmware/gameday-common.yaml");
  found_file = (bool) f;
  std::string line;
  bool in = false;
  while (std::getline(f, line)) {
    if (line.rfind("http_request:", 0) == 0) {
      in = true;
      continue;
    }
    if (!in || line.empty() || line[0] == '#')
      continue;
    if (line[0] != ' ')
      break;
    const size_t at = line.find("buffer_size_tx:");
    if (at != std::string::npos && line.find_first_not_of(' ') == at)
      return atoi(line.c_str() + at + strlen("buffer_size_tx:"));
  }
  return 512;
}

int g_tx = 512;

struct Spare {
  std::string what;
  int left{1 << 30};
};
std::vector<Spare> g_spare;

// The least room each named request had
void note(const char *what, int left) {
  for (Spare &s : g_spare)
    if (s.what == what) {
      if (left < s.left)
        s.left = left;
      return;
    }
  g_spare.push_back({what, left});
}

void fits(const char *what, const std::string &url, const std::string &agent, const char *method = "GET") {
  const size_t host = url.find("://");
  const size_t slash = url.find('/', host == std::string::npos ? 0 : host + 3);
  const std::string path = slash == std::string::npos ? "/" : url.substr(slash);
  const int line = (int) (strlen(method) + 1 + path.size() + strlen(" HTTP/1.1\r\n"));
  const int header = (int) (strlen("User-Agent: ") + agent.size() + strlen("\r\n"));
  // ESP-IDF 5.5.6 sends nothing unless line + header + 3 <= buffer
  const int left = g_tx - (line + header + 3);
  note(what, left);
  RCHECK(left >= 0, "%s misses the %d-byte send buffer by %d bytes (request line %d, User-Agent line %d): %s", what,
         g_tx, -left, line, header, url.c_str());
}

std::string agent_of(const espn::AppRequest &r) {
  for (const espn::AppHeader &h : r.headers)
    if (h.name == "User-Agent")
      return h.value;
  return espn::kUserAgent;
}

uint32_t ms_now() { return 0; }
struct tm local_of(int64_t t) {
  const time_t x = (time_t) t;
  struct tm out {};
  gmtime_r(&x, &out);
  return out;
}

int32_t e4(double v) { return (int32_t) (v * 10000 + (v < 0 ? -0.5 : 0.5)); }

void weather(flights::FlightsApp &home, const std::string &version) {
  static const double kHomes[][2] = {{38.9399, -119.9772}, {37.9887, -84.4777}, {-33.8688, 151.2093},
                                     {-47.6062, -122.3321}, {-89.9999, -179.9999}, {5.0, 5.0}};
  for (const auto &h : kHomes) {
    char coords[48];
    snprintf(coords, sizeof coords, "%.4f,%.4f", h[0], h[1]);
    home.set("flyhome", coords);
    std::unique_ptr<wx::WeatherApp> a(new wx::WeatherApp());
    a->shown(true);
    espn::AppRequest req;
    RCHECK(a->request(kNow, req) && req.url.find("/v1/forecast?") != std::string::npos, "a forecast read for %s",
           coords);
    const std::string agent = agent_of(req);
    fits("Weather forecast", req.url, agent);
    const int32_t la = e4(h[0]), lo = e4(h[1]);
    fits("Weather air quality", wx::air_url(la, lo), agent);
    fits("Weather alerts", wx::alerts_url(la, lo), agent);
    fits("Weather MET Norway", wx::met_url(la, lo), agent);
    fits("Weather radar list", "https://api.rainviewer.com/public/weather-maps.json", agent);
    wx::Radar r;
    r.valid = true;
    r.n = 1;
    snprintf(r.host, sizeof r.host, "https://tilecache.rainviewer.com");
    snprintf(r.path[0], wx::kFramePath, "/v2/radar/1794770400");
    fits("Weather radar tile", wx::rainviewer_url(r, 0, la, lo), agent);
    fits("Weather radar (IEM)", wx::iem_url(kNow, la, lo, 128, 120), agent);
    fits("F1 track weather", espn::f1_weather_url(la, lo), espn::f1_met_agent(version));
    fits("F1 track weather (MET)", espn::f1_met_url(la, lo), espn::f1_met_agent(version));
    flights::Home fh;
    fh.lat = (float) h[0];
    fh.lon = (float) h[1];
    fh.radius_nm = 250;
    fits("Flights fr24", flights::fr24_url(fh), espn::kUserAgent);
    fits("Flights adsb.fi", flights::adsbfi_url(fh), flights::caller_agent(version));
    fits("Flights adsb.lol", flights::adsblol_url(fh), flights::caller_agent(version));
  }
}

void others(const std::string &version) {
  using namespace espn;
  const std::string ua = kUserAgent;
  fits("ESPN team", team_url(League::NFL, 12), ua);
  fits("ESPN team (cup)", team_url(League::EPL, 360, "uefa.europa.conf"), ua);
  fits("ESPN schedule", schedule_url(League::EPL, 360), ua);
  fits("ESPN event", event_url(League::MCBB, "401700000"), ua);
  fits("ESPN event (cup)", event_url(League::EPL, "740000", "uefa.europa.conf"), ua);
  fits("ESPN scoreboard", scoreboard_url(League::NCAA, 80, kNow), ua);
  fits("ESPN scan", scan_url(League::MCBB, kNow), ua);
  fits("ESPN team day", team_day_url(League::MCBB, 50, kNow), ua);
  fits("ESPN summary", summary_url(League::NBA, "401800000"), ua);
  fits("ESPN header", header_url(League::NASCAR), ua);
  fits("ESPN calendar", golf_calendar_url(League::PGA, kNow, true), ua);
  fits("ESPN logo", team_logo_url(League::NCAA, 2633, "TENN"), ua);
  fits("MLB team", mlb_team_url(4904, kNow), ua);
  fits("MLB minor league team", mlb_far_url(4904, 11, kNow), ua);
  fits("MLB game", mlb_game_url("7777777"), ua);
  fits("MLB day", mlb_day_url(kNow), ua);
  fits("MLB logo", mlb_logo_url(4904), ua);
  ht_set_feed(League::AHL, "50c2cd9b5e18e390", "ahl");
  ht_set_feed(League::ECHL, "2c2b89ea7345cae8", "echl");
  fits("HockeyTech team", ht_team_url(League::AHL, 384), ua);
  fits("HockeyTech game", ht_game_url(League::AHL, "1027999"), ua);
  fits("HockeyTech day", ht_day_url(League::ECHL, kNow), ua);
  fits("HockeyTech logo", ht_logo_url(League::AHL, 384), ua);

  // The longest symbols each source takes
  MarketBlob y{}, n{};
  for (const char *s : {"A=B=C=D=EF", "A=B=C=D=EG", "A=B=C=D=EH", "A=B=C=D=EI", "PEPE", "POL", "SUI", "TON"}) {
    MarketSymbol m;
    if (parse_market_symbol(s, MarketKind::NONE, m))
      market_put(y, 0, m);
  }
  for (const char *s : {"ABCDEFGHIJ", "ABCDEFGHIK", "ABCDEFGHIL", "ABCDEFGHIM", "ABCDEFGHIN", "ABCDEFGHIO",
                        "ABCDEFGHIP", "ABCDEFGHIQ"}) {
    MarketSymbol m;
    if (parse_market_symbol(s, MarketKind::STOCK, m))
      market_put(n, 0, m);
  }
  fits("Stocks Yahoo", market_spark_url(y), kMarketsUserAgent);
  fits("Stocks Yahoo chart", market_chart_url(y.syms[0]), kMarketsUserAgent);
  fits("Stocks Kraken", market_kraken_url(y), kMarketsUserAgent);
  fits("Stocks Nasdaq watchlist", market_nasdaq_url(n), kMarketsUserAgent);
  char icon[200];
  for (uint8_t k = 0; k < 4; k++) {
    size_t len = market_logo_url("ABCDEFGHIJ", (uint8_t) MarketKind::STOCK, k, icon, sizeof icon);
    if (len)
      fits("Stocks icon", std::string(icon, len), ua);
    len = market_logo_url("PEPE", (uint8_t) MarketKind::CRYPTO, k, icon, sizeof icon);
    if (len)
      fits("Coin icon", std::string(icon, len), ua);
  }

  fits("Flights routes", flights::kRoutesetUrl, flights::caller_agent(version), "POST");
  fits("Flights adsbdb", flights::adsbdb_url("UAL12345"), flights::caller_agent(version));
  for (F1Read r : {F1Read::NEXT, F1Read::DRIVERS, F1Read::CONSTRUCTORS, F1Read::LAST_RESULTS})
    fits("F1 Jolpica", f1_jolpica_url(r), espn::f1_met_agent(version));
  fits("F1 circuit", f1_circuit_url(123), ua);
  fits("F1 athlete", f1_athlete_url(4665), ua);
}

}  // namespace

int run_request_size_tests(int &total_checks) {
  bool found = false;
  g_tx = yaml_tx_size(found);
  RCHECK(found, "reads ../firmware/gameday-common.yaml");
  espn::AppEnv &env = espn::app_env();
  const espn::AppEnv saved = env;
  env.local = local_of;
  env.ms = ms_now;
  espn::AppSlots &slots = espn::app_slots();
  auto *home = static_cast<flights::FlightsApp *>(slots.get(espn::Display::FLIGHTS));
  if (home == nullptr) {
    home = new flights::FlightsApp();
    home->setup();
    espn::app_register(slots, home);
  }
  // A release's version and a beta's longer one
  for (const char *version : {"2026.12.10", "2026.12.10-beta.999"}) {
    env.version = version;
    weather(*home, version);
    others(version);
  }
  home->set("flyhome", "none");
  home->set("flyplace", "");
  env = saved;
  if (getenv("REQUEST_SIZE_REPORT") != nullptr)
    for (const Spare &s : g_spare)
      printf("  %-26s %4d of %d bytes to spare\n", s.what.c_str(), s.left, g_tx);
  total_checks += checks;
  return failures;
}
