// Display reads as start_app_job_ and apply_app_job_ run them.
#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "app_f1.h"
#include "app_flights.h"
#include "app_markets.h"
#include "app_weather.h"

namespace {

int checks = 0;
int failures = 0;

#define QCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

bool has(const std::string &s, const char *p) { return s.find(p) != std::string::npos; }

uint32_t g_ms = 0;
uint32_t ms_now() { return g_ms; }
struct tm local_of(int64_t t) {
  const time_t x = (time_t) (t - 8 * 3600);
  struct tm out {};
  gmtime_r(&x, &out);
  return out;
}

struct Answer {
  int status{200};
  std::string body;
  uint32_t took_ms{700};
};
using Net = std::function<Answer(const espn::AppRequest &, int64_t)>;

Answer fail(int status, uint32_t took_ms = 300) {
  Answer a;
  a.status = status;
  a.took_ms = took_ms;
  return a;
}
Answer file(const char *name) {
  Answer a;
  a.body = slurp(std::string("fixtures/") + name);
  return a;
}

const uint32_t kMinute = 60, kHour = 3600;

// millis() wraps an hour in; epoch seconds lag 400 ms.
struct Run {
  espn::App *a{nullptr};
  bool on{true};
  int64_t epoch0{0};
  uint32_t ms0{0xFFFFFFFFu - kHour * 1000 + 1};
  // The clock stepped by SNTP after this many seconds
  uint32_t step_at{0};
  int64_t step_s{0};
  // Every read's start, in seconds since ms0, by host
  std::map<std::string, std::vector<uint32_t>> reads;
  std::map<std::string, bool> host_failed;  // every answer a failure
  std::vector<uint32_t> all;
  uint32_t last_ms{0};
  espn::HostWait wait;
  uint32_t rnd{12345};

  uint32_t elapsed_s() const { return (g_ms - this->ms0) / 1000; }
  int64_t epoch() const {
    const int64_t t = this->epoch0 + (int64_t) ((g_ms - this->ms0 + 400) / 1000);
    return this->step_at != 0 && this->elapsed_s() >= this->step_at ? t + this->step_s : t;
  }

  // As start_app_job_ and apply_app_job_, in 100 ms loop passes.
  void go(uint32_t seconds, const Net &net) {
    g_ms = this->ms0;
    const uint32_t end = this->ms0 + seconds * 1000;
    int64_t drawn = 0;
    while ((int32_t) (end - g_ms) > 0) {
      const int64_t tnow = this->epoch();
      if (this->on && tnow != drawn) {
        espn::AppFrame fr;
        this->a->frame(128, 64, tnow, fr);
        drawn = tnow;
      }
      espn::AppRequest req;
      if (!espn::app_read_due(*this->a, this->on, false, this->wait, this->last_ms, g_ms, tnow) ||
          !this->a->request(tnow, req)) {
        g_ms += 100;
        continue;
      }
      this->last_ms = g_ms | 1;
      const std::string host = espn::url_host(req.url);
      this->reads[host].push_back(this->elapsed_s());
      this->all.push_back(this->elapsed_s());
      const Answer ans = net(req, tnow);
      bool ok = false;
      if (ans.status == 200) {
        JsonDocument filter;
        if (this->a->filter(filter)) {
          JsonDocument doc;
          ok = !deserializeJson(doc, ans.body, DeserializationOption::Filter(filter),
                                DeserializationOption::NestingLimit(40)) &&
               this->a->parse_doc(doc.as<JsonObjectConst>(), tnow);
        } else {
          espn::AppTextStream in(ans.body);
          ok = this->a->parse(in, tnow);
        }
      }
      auto known = this->host_failed.find(host);
      const bool failed = ans.status != 200 && ans.status != 404;
      this->host_failed[host] = (known == this->host_failed.end() || known->second) && failed;
      g_ms += ans.took_ms;
      const bool refused = espn::is_throttle_status(ans.status);
      this->a->ended(espn::AppResult{ans.status, refused, 0});
      if (ok) {
        this->wait.clear();
      } else {
        this->rnd = this->rnd * 1664525u + 1013904223u;
        espn::app_read_failed(this->a, this->wait, refused, 0, this->rnd, g_ms);
      }
      this->a->apply(ok, tnow);
    }
  }

  static size_t most_in(const std::vector<uint32_t> &t, uint32_t window) {
    size_t most = 0;
    for (size_t i = 0, j = 0; i < t.size(); i++) {
      while (t[i] - t[j] >= window)
        j++;
      most = std::max(most, i - j + 1);
    }
    return most;
  }
  // Reads of host from..to seconds in.
  size_t count(const char *host, uint32_t from, uint32_t to) const {
    auto it = this->reads.find(host);
    if (it == this->reads.end())
      return 0;
    return (size_t) std::count_if(it->second.begin(), it->second.end(),
                                  [&](uint32_t t) { return t >= from && t < to; });
  }

  // No storm, no stall, failing hosts under fail_per_h.
  void check(const char *name, uint32_t seconds, double fail_per_h) const {
    QCHECK(!this->all.empty(), "%s: no reads at all", name);
    uint32_t prev = 0, gap = 0;
    for (uint32_t t : this->all) {
      gap = std::max(gap, t - prev);
      prev = t;
    }
    gap = std::max(gap, seconds - prev);
    QCHECK(gap <= kHour + kMinute, "%s: no read for %u s", name, (unsigned) gap);
    for (const auto &kv : this->reads) {
      const size_t minute = most_in(kv.second, kMinute);
      QCHECK(minute <= 6, "%s: %s read %zu times in a minute", name, kv.first.c_str(), minute);
      const size_t hour = most_in(kv.second, kHour);
      QCHECK(hour <= 200, "%s: %s read %zu times in an hour", name, kv.first.c_str(), hour);
      auto f = this->host_failed.find(kv.first);
      if (f != this->host_failed.end() && f->second)
        QCHECK(hour <= fail_per_h, "%s: %s failed %zu times in an hour", name, kv.first.c_str(), hour);
    }
  }
};

// Each case: 3 h, millis() wrapping an hour in.
const uint32_t kRun = 3 * kHour;

void test_weather() {
  const int64_t kNow = 1794630600;
  Net good = [](const espn::AppRequest &r, int64_t) {
    if (has(r.url, "air-quality"))
      return file("weather_aq_tahoe.json");
    if (has(r.url, "api.open-meteo.com"))
      return file("weather_om_tahoe.json");
    if (has(r.url, "api.met.no"))
      return file("met_vegas.json");
    if (has(r.url, "weather.gov"))
      return file("weather_nws_tahoe.json");
    return file("weather_rainviewer.json");
  };
  struct Case {
    const char *name;
    bool on;
    double fail_per_h;
    Net net;
  };
  const Case cases[] = {
      {"Weather, every host answers", true, 0, good},
      {"Weather, Open-Meteo's forecast 500s", true, 15,
       [&](const espn::AppRequest &r, int64_t n) {
         return has(r.url, "api.open-meteo.com/v1/forecast") ? fail(500) : good(r, n);
       }},
      {"Weather, the forecast refused by both", true, 15,
       [&](const espn::AppRequest &r, int64_t n) {
         return has(r.url, "api.open-meteo.com/v1/forecast") || has(r.url, "met.no") ? fail(429) : good(r, n);
       }},
      {"Weather, nothing answers", true, 15, [](const espn::AppRequest &, int64_t) { return fail(0, 10000); }},
      {"Weather, junk from every host", true, 15,
       [](const espn::AppRequest &, int64_t) {
         Answer a;
         a.body = "{\"hello\":1}";
         return a;
       }},
      {"Weather off screen, warnings on", false, 0, good},
      {"Weather off screen, the NWS 503s", false, 15,
       [&](const espn::AppRequest &r, int64_t n) { return has(r.url, "weather.gov") ? fail(503) : good(r, n); }},
  };
  for (const Case &c : cases) {
    wx::WeatherApp w;
    w.set("wxticker", c.on ? "0" : "1");
    w.shown(c.on);
    Run run;
    run.a = &w;
    run.on = c.on;
    run.epoch0 = kNow;
    run.go(kRun, c.net);
    run.check(c.name, kRun, c.fail_per_h);
  }

  // Back after an hour of nothing: the forecast comes back.
  wx::WeatherApp w;
  w.shown(true);
  Run run;
  run.a = &w;
  run.epoch0 = kNow;
  run.go(kRun, [&](const espn::AppRequest &r, int64_t n) {
    return run.elapsed_s() < kHour ? fail(0, 10000) : good(r, n);
  });
  run.check("Weather, back after an hour", kRun, 1e9);
  // From MET at first: Open-Meteo waits out its own backoff.
  const size_t back = run.count("api.open-meteo.com", kHour, kHour + 15 * kMinute) +
                      run.count("api.met.no", kHour, kHour + 15 * kMinute);
  QCHECK(back >= 1, "Weather: no forecast read once back");
  QCHECK(run.count("api.open-meteo.com", 2 * kHour, kRun) >= 5, "Weather: forecast %zu reads in the last hour",
         run.count("api.open-meteo.com", 2 * kHour, kRun));

  // The clock stepped back, then forward, by an hour.
  for (int64_t step : {-3600, 3600}) {
    wx::WeatherApp w2;
    w2.shown(true);
    Run r2;
    r2.a = &w2;
    r2.epoch0 = kNow;
    r2.step_at = 90 * kMinute;
    r2.step_s = step;
    r2.go(kRun, good);
    r2.check(step < 0 ? "Weather, clock back an hour" : "Weather, clock on an hour", kRun, 0);
  }
}

std::string fr24_at(const std::string &json, int64_t shift) {
  JsonDocument d;
  deserializeJson(d, json);
  for (JsonPair kv : d.as<JsonObject>())
    if (kv.value().is<JsonArray>())
      kv.value()[10] = kv.value()[10].as<int64_t>() + shift;
  std::string out;
  serializeJson(d, out);
  return out;
}

void test_flights() {
  const int64_t kNow = 1794457500;
  const std::string fr24 = slurp("fixtures/flights_fr24_nyc.json");
  Net good = [&](const espn::AppRequest &r, int64_t n) {
    Answer a;
    if (has(r.url, "flightradar24"))
      a.body = fr24_at(fr24, n - kNow);
    else if (has(r.url, "adsb.fi"))
      a = file("flights_adsbfi_jfk.json");
    else if (has(r.url, "adsb.lol"))
      a = file("flights_adsblol_jfk.json");
    else if (has(r.url, "adsb.im"))
      a.body = "[]";
    else if (has(r.url, "adsbdb"))
      a.status = 404;
    else
      a.body = "{\"photos\":[]}";
    return a;
  };
  struct Case {
    const char *name;
    double fail_per_h;
    Net net;
  };
  const Case cases[] = {
      {"Flight Tracker, every host answers", 0, good},
      {"Flight Tracker, Flightradar24 refuses", 15,
       [&](const espn::AppRequest &r, int64_t n) { return has(r.url, "flightradar24") ? fail(403) : good(r, n); }},
      {"Flight Tracker, Flightradar24 and adsb.im refuse", 15,
       [&](const espn::AppRequest &r, int64_t n) {
         return has(r.url, "flightradar24") || has(r.url, "adsb.im") ? fail(403) : good(r, n);
       }},
      {"Flight Tracker, Flightradar24 and adsb.fi time out", 15,
       [&](const espn::AppRequest &r, int64_t n) {
         return has(r.url, "flightradar24") || has(r.url, "adsb.fi") ? fail(0, 10000) : good(r, n);
       }},
      {"Flight Tracker, nothing answers", 15, [](const espn::AppRequest &, int64_t) { return fail(0, 10000); }},
  };
  for (const Case &c : cases) {
    flights::FlightsApp f;
    f.setup();
    f.set("flyhome", "40.71,-74.01");
    f.shown(true);
    Run run;
    run.a = &f;
    run.epoch0 = kNow;
    run.go(kRun, c.net);
    run.check(c.name, kRun, c.fail_per_h);
  }

  flights::FlightsApp f;
  f.setup();
  f.set("flyhome", "40.71,-74.01");
  f.shown(true);
  Run run;
  run.a = &f;
  run.epoch0 = kNow;
  run.go(kRun, [&](const espn::AppRequest &r, int64_t n) {
    return run.elapsed_s() < kHour ? fail(0, 10000) : good(r, n);
  });
  run.check("Flight Tracker, back after an hour", kRun, 1e9);
  QCHECK(run.count("data-cloud.flightradar24.com", kHour, kHour + 15 * kMinute) >= 1,
         "Flight Tracker: no Flightradar24 read once back");
  QCHECK(run.count("data-cloud.flightradar24.com", 2 * kHour, kRun) >= 150,
         "Flight Tracker: %zu Flightradar24 reads in the last hour",
         run.count("data-cloud.flightradar24.com", 2 * kHour, kRun));

  for (int64_t step : {-3600, 3600}) {
    flights::FlightsApp f2;
    f2.setup();
    f2.set("flyhome", "40.71,-74.01");
    f2.shown(true);
    Run r2;
    r2.a = &f2;
    r2.epoch0 = kNow;
    r2.step_at = 90 * kMinute;
    r2.step_s = step;
    r2.go(kRun, good);
    r2.check(step < 0 ? "Flight Tracker, clock back an hour" : "Flight Tracker, clock on an hour", kRun, 0);
    QCHECK(r2.count("data-cloud.flightradar24.com", 2 * kHour, kRun) >= 150,
           "Flight Tracker, clock stepped %lld s: %zu reads in the last hour", (long long) step,
           r2.count("data-cloud.flightradar24.com", 2 * kHour, kRun));
  }
}

void test_markets() {
  const int64_t kNow = 1794456439;
  Net good = [](const espn::AppRequest &r, int64_t) {
    if (has(r.url, "yahoo"))
      return file("markets_yahoo_spark.json");
    if (has(r.url, "kraken"))
      return file("markets_kraken_ticker.json");
    return file("markets_nasdaq_watchlist.json");
  };
  struct Case {
    const char *name;
    double fail_per_h;
    Net net;
  };
  const Case cases[] = {
      {"Stocks & Crypto, Yahoo answers", 0, good},
      {"Stocks & Crypto, Yahoo refuses", 15,
       [&](const espn::AppRequest &r, int64_t n) { return has(r.url, "yahoo") ? fail(429) : good(r, n); }},
      // A backup that fails without refusing is read each minute.
      {"Stocks & Crypto, only Nasdaq answers", 61,
       [&](const espn::AppRequest &r, int64_t n) { return has(r.url, "nasdaq") ? good(r, n) : fail(500); }},
      {"Stocks & Crypto, nothing answers", 15, [](const espn::AppRequest &, int64_t) { return fail(0, 10000); }},
  };
  for (const Case &c : cases) {
    espn::MarketsApp m;
    m.setup();
    m.shown(true);
    Run run;
    run.a = &m;
    run.epoch0 = kNow;
    run.go(kRun, c.net);
    run.check(c.name, kRun, c.fail_per_h);
  }

  espn::MarketsApp m;
  m.setup();
  m.shown(true);
  Run run;
  run.a = &m;
  run.epoch0 = kNow;
  run.go(kRun, [&](const espn::AppRequest &r, int64_t n) {
    return run.elapsed_s() < kHour ? fail(0, 10000) : good(r, n);
  });
  run.check("Stocks & Crypto, back after an hour", kRun, 1e9);
  QCHECK(run.count("query1.finance.yahoo.com", 2 * kHour, kRun) >= 50,
         "Stocks & Crypto: %zu Yahoo reads in the last hour", run.count("query1.finance.yahoo.com", 2 * kHour, kRun));
}

void test_f1() {
  const int64_t kNow = 1794630600;
  Net good = [](const espn::AppRequest &r, int64_t) {
    if (has(r.url, "scoreboard/header"))
      return file("f1_header_vegas_pre.json");
    if (has(r.url, "next.json"))
      return file("jolpica_next.json");
    if (has(r.url, "driverStandings"))
      return file("jolpica_driver_standings.json");
    if (has(r.url, "constructorStandings"))
      return file("jolpica_constructor_standings.json");
    if (has(r.url, "results"))
      return file("jolpica_last_results.json");
    if (has(r.url, "/circuits/"))
      return file("f1_circuit_vegas.json");
    if (has(r.url, "/athletes/"))
      return file("f1_athlete_verstappen.json");
    if (has(r.url, "open-meteo"))
      return file("openmeteo_vegas.json");
    if (has(r.url, "met.no"))
      return file("met_vegas.json");
    return file("racing_calendar_f1_2026.json");
  };
  struct Case {
    const char *name;
    double fail_per_h;
    Net net;
  };
  const Case cases[] = {
      {"F1, every host answers", 0, good},
      {"F1, Jolpica 500s", 15,
       [&](const espn::AppRequest &r, int64_t n) { return has(r.url, "jolpi.ca") ? fail(500) : good(r, n); }},
      {"F1, nothing answers", 15, [](const espn::AppRequest &, int64_t) { return fail(0, 10000); }},
  };
  for (const Case &c : cases) {
    espn::F1App f;
    f.shown(true);
    Run run;
    run.a = &f;
    run.epoch0 = kNow;
    run.go(kRun, c.net);
    run.check(c.name, kRun, c.fail_per_h);
  }
}

}  // namespace

int run_app_poll_tests(int &total_checks) {
  espn::AppEnv &env = espn::app_env();
  const espn::AppEnv saved = env;
  env.local = local_of;
  env.ms = ms_now;
  env.tz = 4;
  env.version = "2026.11.6";
  // Weather's home is the Flight Tracker's.
  espn::AppSlots &slots = espn::app_slots();
  auto *home = static_cast<flights::FlightsApp *>(slots.get(espn::Display::FLIGHTS));
  if (home == nullptr) {
    home = new flights::FlightsApp();
    home->setup();
    espn::app_register(slots, home);
  }
  home->set("flyhome", "38.9399,-119.9772");
  test_weather();
  test_flights();
  test_markets();
  test_f1();
  home->set("flyhome", "none");
  env = saved;
  total_checks += checks;
  return failures;
}
