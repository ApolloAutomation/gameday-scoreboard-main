// Memory each ESPN read takes, for the CI budget (firmware/budget.json).
// Runs every parser the panel uses over a fixture like the read it stands for
// and prints, per case, as JSON:
//   json   the most bytes the JSON documents (filter and parse) held at once,
//          through the allocator the firmware points at PSRAM
//   heap   the most bytes everything else held at once (the result vectors
//          and strings), which on the panel come out of internal RAM
// These are host figures (64-bit pointers, glibc), larger than the panel's,
// and the same on every run of the same code, so CI can compare them with a
// budget and with the base branch. scripts/size_budget.py reads the output.
//   make -C tests heap
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "espn_parse_impl.h"
#include "props.h"
#include "watch.h"
#include "source_hockeytech.h"
#include "source_mlbstats.h"
#include "source_markets.h"

using namespace espn;

// ---- byte counting -------------------------------------------------------------

// Every block carries its size in front, so a free knows what it gives back.
static const size_t kHead = alignof(std::max_align_t);

struct Counter {
  size_t live = 0;
  size_t peak = 0;
  void add(size_t n) {
    live += n;
    if (live > peak)
      peak = live;
  }
  void sub(size_t n) { live -= n; }
  void reset() { peak = live; }
};

static Counter heap_count;  // operator new
static Counter json_count;  // the JSON allocator

static void *counted_alloc(Counter &c, size_t n) {
  char *p = static_cast<char *>(malloc(n + kHead));
  if (p == nullptr)
    return nullptr;
  *reinterpret_cast<size_t *>(p) = n;
  c.add(n);
  return p + kHead;
}

static void counted_free(Counter &c, void *ptr) {
  if (ptr == nullptr)
    return;
  char *p = static_cast<char *>(ptr) - kHead;
  c.sub(*reinterpret_cast<size_t *>(p));
  free(p);
}

static void *counted_realloc(Counter &c, void *ptr, size_t n) {
  if (ptr == nullptr)
    return counted_alloc(c, n);
  char *p = static_cast<char *>(ptr) - kHead;
  size_t old = *reinterpret_cast<size_t *>(p);
  char *q = static_cast<char *>(realloc(p, n + kHead));
  if (q == nullptr)
    return nullptr;
  *reinterpret_cast<size_t *>(q) = n;
  c.sub(old);
  c.add(n);
  return q + kHead;
}

void *operator new(size_t n) {
  void *p = counted_alloc(heap_count, n);
  if (p == nullptr)
    throw std::bad_alloc();
  return p;
}
void *operator new[](size_t n) { return operator new(n); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return counted_alloc(heap_count, n); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return counted_alloc(heap_count, n); }
void operator delete(void *p) noexcept { counted_free(heap_count, p); }
void operator delete[](void *p) noexcept { counted_free(heap_count, p); }
void operator delete(void *p, size_t) noexcept { counted_free(heap_count, p); }
void operator delete[](void *p, size_t) noexcept { counted_free(heap_count, p); }

struct JsonCounter : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return counted_alloc(json_count, n); }
  void deallocate(void *p) override { counted_free(json_count, p); }
  void *reallocate(void *p, size_t n) override { return counted_realloc(json_count, p, n); }
};

// ---- cases ---------------------------------------------------------------------

static std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static int failures = 0;
static bool first = true;

// Runs one read with the input already in memory (the panel streams it, so
// the input is not counted) and prints its two peaks.
template<typename F> static void measure(const char *name, F read) {
  heap_count.reset();
  json_count.reset();
  size_t heap_before = heap_count.live, json_before = json_count.live;
  bool ok = read();
  if (!ok) {
    fprintf(stderr, "heap_budget: %s did not parse\n", name);
    failures++;
  }
  printf("%s\n  \"%s\": {\"json\": %zu, \"heap\": %zu}", first ? "" : ",", name, json_count.peak - json_before,
         heap_count.peak - heap_before);
  first = false;
}

// The live rotation's scan of one league's day: live games, finals and the
// games still to come, as GamedayComponent::scan_live_() reads it.
static void scan(const char *name, const std::string &file) {
  std::string json = slurp("fixtures/" + file);
  measure(name, [&] {
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    return parse_live_games(json, live, &next, &finals, &pending);
  });
}

// A single-event game poll. The tracked team is the event's first competitor.
static void event(const char *name, const std::string &file, Sport sport) {
  std::string json = slurp("fixtures/" + file);
  JsonDocument doc;
  deserializeJson(doc, json);
  std::string event_id = doc["id"] | "";
  uint32_t team = (uint32_t) atol(doc["competitions"][0]["competitors"][0]["id"] | "0");
  doc.clear();
  doc.shrinkToFit();
  measure(name, [&] {
    GameSnapshot g;
    return parse_event(json, sport, team, g, event_id);
  });
}

// A box score read for player props: eight players watched.
static void box_read(const char *name, const std::string &file, Sport sport, std::initializer_list<uint32_t> ids) {
  std::string json = slurp("fixtures/" + file);
  PlayerWatch w;
  for (uint32_t id : ids)
    if (w.n < PlayerWatch::kMax)
      w.ids[w.n++] = id;
  measure(name, [&] {
    BoxCutter cut;
    StringReader in(json, &cut);
    std::vector<BoxLine> lines;
    return parse_box(in, sport, w, lines) && !lines.empty();
  });
}

// Every game marked on, so each is read in full.
static void watch_day(const char *name, const std::string &file, League league) {
  std::string json;
  {
    JsonDocument doc;
    deserializeJson(doc, slurp("fixtures/" + file), DeserializationOption::NestingLimit(40));
    for (JsonObject ev : doc["events"].as<JsonArray>())
      ev["status"]["type"]["state"] = "in";
    serializeJson(doc, json);
  }
  measure(name, [&] {
    WatchJob job;
    job.league = (uint8_t) league;
    job.ctx.live = true;
    job.ctx.mask = kAlertsAll;
    return watch_read_day(json, job) && !job.fresh.empty();
  });
}

// ids: the favorites, those in the game first.
static void event_players(const char *name, const std::string &file, Sport sport,
                          std::initializer_list<uint32_t> ids) {
  std::string json = slurp("fixtures/" + file);
  JsonDocument doc;
  deserializeJson(doc, json);
  std::string event_id = doc["id"] | "";
  uint32_t team = (uint32_t) atol(doc["competitions"][0]["competitors"][0]["id"] | "0");
  doc.clear();
  doc.shrinkToFit();
  PlayerWatch w;
  for (uint32_t id : ids)
    if (w.n < PlayerWatch::kMax)
      w.ids[w.n++] = id;
  measure(name, [&] {
    GameSnapshot g;
    return parse_event(json, sport, team, g, event_id, &w);
  });
}

int main() {
  JsonCounter json_alloc;
  set_json_allocator(&json_alloc);
  printf("{");

  scan("scan_ncaa_live", "scoreboard_ncaa_live.json");
  scan("scan_ncaa_day", "day_ncaa_20261017.json");
  scan("scan_nba_day", "day_nba_20261015.json");
  scan("scan_mlb_day", "day_mlb_20261012.json");
  scan("scan_nhl_day", "day_nhl_20261009.json");
  scan("scan_soccer_day", "day_bra_20261012.json");

  // A college football game poll reads the whole day's scoreboard.
  {
    std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
    measure("poll_ncaa_scoreboard", [&] {
      GameSnapshot g;
      return parse_scoreboard(json, std::string("401858433"), 68, g);
    });
  }
  event("poll_nfl_event", "event_nfl_pre.json", Sport::FOOTBALL);
  event("poll_mlb_event", "event_mlb_top.json", Sport::BASEBALL);
  event("poll_nba_event", "event_nba_live.json", Sport::BASKETBALL);
  event("poll_nhl_event", "event_nhl_live.json", Sport::HOCKEY);
  event("poll_soccer_event", "event_soccer_second_half.json", Sport::SOCCER);
  // Worst case: eight favorites, as many leaders as ESPN lists.
  event_players("poll_ncaa_event_players", "event_ncaa_live.json", Sport::FOOTBALL,
                {4870883, 5122157, 4879999, 5079684, 1, 2, 3, 4});
  event_players("poll_mlb_event_players", "event_mlb_hr.json", Sport::BASEBALL,
                {4917849, 40960, 4867424, 33912, 40576, 1, 2, 3});
  event_players("poll_nba_event_players", "event_nba_live.json", Sport::BASKETBALL,
                {4396909, 4017837, 5106060, 4594268, 3032976, 4432816, 4431671, 1});
  event_players("poll_soccer_event_players", "event_soccer_second_half.json", Sport::SOCCER,
                {199680, 205448, 1, 2, 3, 4, 5, 6});

  // Finals have the most rows a box score has.
  box_read("box_nfl_final", "summary_nfl_final.json", Sport::FOOTBALL,
           {4360310, 4882093, 4432620, 4686361, 3043078, 3916433, 4038441, 3116365});
  box_read("box_nba_live", "summary_nba_live.json", Sport::BASKETBALL,
           {4431680, 4433137, 4592691, 4701230, 1, 2, 3, 4});
  box_read("box_mlb_final", "summary_mlb_final.json", Sport::BASEBALL,
           {5149101, 5080761, 4917869, 35013, 33481, 40810, 42584, 4679983});
  box_read("box_nhl_final", "summary_nhl_final.json", Sport::HOCKEY,
           {3114732, 4319858, 4587845, 4024833, 4587943, 3114766, 4755690, 4272888});
  watch_day("watch_nfl_day", "scoreboard_nfl.json", League::NFL);
  watch_day("watch_mlb_day", "day_mlb_20261012.json", League::MLB);
  watch_day("watch_soccer_day", "day_bra_20261012.json", League::MLS);
  watch_day("watch_ncaa_conference", "day_ncaa_20261017.json", League::NCAA);

  // A golf tour's board: the scoreboard header, and after a final the
  // season calendar for the next event.
  {
    std::string json = slurp("fixtures/golf_header_pga_suspended.json");
    measure("poll_golf_header", [&] {
      GameSnapshot g;
      return parse_golf(json, g) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/golf_calendar_pga_2026.json");
    measure("golf_calendar", [&] {
      GolfNext n;
      return parse_golf_calendar(json, parse_iso8601_z("2026-11-09T12:00Z"), std::string("401850979"), n) == 1;
    });
  }
  // A racing series' board: the header of a finished F1 weekend, the largest
  // (five sessions of 22 drivers), and after the race the season calendar.
  {
    std::string json = slurp("fixtures/racing_header_f1_final.json");
    measure("poll_racing_header", [&] {
      GameSnapshot g;
      return parse_racing(json, g) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/racing_calendar_nascar_2026.json");
    measure("racing_calendar", [&] {
      GolfNext n;
      return parse_racing_calendar(json, parse_iso8601_z("2026-11-02T12:00Z"), std::string("202611010020"), 0, n) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/team_nfl_dal.json");
    measure("team_nfl", [&] {
      Schedule s;
      return parse_team(json, s);
    });
  }
  // Largest team read: an MLB record has 100+ stats.
  {
    std::string json = slurp("fixtures/team_mlb_mil_live.json");
    measure("team_mlb", [&] {
      Schedule s;
      return parse_team(json, s);
    });
  }
  {
    std::string json = slurp("fixtures/day_ncaa_20261017.json");
    measure("team_day_ncaa", [&] {
      Schedule s;
      return parse_team_game(json, 251, std::string(), s) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/schedule_nfl_dal.json");
    measure("schedule_nfl", [&] {
      std::vector<Upcoming> up;
      return parse_upcoming(json, 6, 4, up);
    });
  }
  // A college team's keeps each opponent's rank.
  {
    std::string json = slurp("fixtures/schedule_ncaa_iowa_tbd.json");
    measure("schedule_ncaa", [&] {
      std::vector<Upcoming> up;
      return parse_upcoming(json, 2294, 4, up, true);
    });
  }

  // Minor league baseball: each read at its largest.
  {
    // Every game unstarted, so the Up next list fills.
    std::string json = slurp("fixtures/mlbstats_team_dur_202604.json");
    for (size_t at = json.find("\"codedGameState\":\"F\""); at != std::string::npos;
         at = json.find("\"codedGameState\":\"F\"", at))
      json[at + 18] = 'S';
    measure("team_milb", [&] {
      Schedule s;
      std::vector<Upcoming> up;
      return parse_mlb_team(json, 234, parse_iso8601_z("2026-04-06T12:00Z"), s, &up, 4) >= 0;
    });
  }
  {
    std::string json = slurp("fixtures/mlbstats_far_dur.json");
    measure("team_far_milb", [&] {
      Schedule s;
      return parse_mlb_team(json, 234, parse_iso8601_z("2026-11-08T23:00Z"), s) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/mlbstats_game_postponed.json");
    measure("poll_milb_game", [&] {
      GameSnapshot g;
      return parse_mlb_game(json, 234, parse_iso8601_z("2026-04-11T05:00Z"), g);
    });
  }
  {
    std::string json = slurp("fixtures/mlbstats_day_milb_20260814.json");
    measure("scan_milb_day", [&] {
      std::vector<LiveGame> live;
      std::vector<Final> finals;
      std::vector<Pending> pending;
      int64_t next = 0;
      return parse_mlb_day(json, live, &next, &finals, &pending);
    });
  }

  // AHL and ECHL: each read at its largest.
  {
    std::string json = slurp("fixtures/ht_team_ahl_syr.json");
    measure("team_hockeytech", [&] {
      Schedule s;
      std::vector<Upcoming> up;
      return parse_ht_team(json, 324, parse_iso8601_z("2026-11-09T01:30Z"), s, &up, 4) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/ht_game_echl_final_so.json");
    measure("poll_hockeytech_game", [&] {
      GameSnapshot g;
      return parse_ht_game(json, League::ECHL, 107, g);
    });
  }
  {
    std::string json = slurp("fixtures/ht_day_ahl_20261107.json");
    measure("scan_hockeytech_day", [&] {
      std::vector<LiveGame> live;
      std::vector<Final> finals;
      std::vector<Pending> pending;
      int64_t next = 0;
      return parse_ht_day(json, live, &next, &finals, &pending);
    });
  }

  // Eight symbols in one Yahoo read, then the fallbacks.
  {
    MarketBlob list{};
    for (const char *t : {"AAPL", "SPY", "^GSPC", "BRK.B", "BTC", "ETH", "DOGE", "SHIB"}) {
      MarketSymbol s{};
      parse_market_symbol(t, MarketKind::NONE, s);
      market_put(list, 0, s);
    }
    const int64_t now = parse_iso8601_z("2026-11-12T04:07Z");
    std::string json = slurp("fixtures/markets_yahoo_spark.json");
    measure("markets_yahoo_spark", [&] {
      MarketQuote q[kMarketSlots];
      return parse_yahoo_quotes(json, list, q) == 8;
    });
    std::string kraken = slurp("fixtures/markets_kraken_ticker.json");
    measure("markets_kraken", [&] {
      MarketQuote q[kMarketSlots];
      return parse_kraken_quotes(kraken, list, now, q) == 4;
    });
    std::string nasdaq = slurp("fixtures/markets_nasdaq_watchlist.json");
    measure("markets_nasdaq", [&] {
      MarketQuote q[kMarketSlots];
      return parse_nasdaq_quotes(nasdaq, list, now, q) == 3;
    });
  }

  printf("\n}\n");
  set_json_allocator(nullptr);
  return failures == 0 ? 0 : 1;
}
