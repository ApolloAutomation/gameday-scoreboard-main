// Host tests for the ESPN parser and game logic. Build with `make` in tests/.
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "alerts.h"
#include "auto_update.h"
#include "badge.h"
#include "board_layout.h"
#include "captive.h"
#include "demo.h"
#include "espn_parse.h"
#include "espn_parse_impl.h"
#include "favorites.h"
#include "follow.h"
#include "guard.h"
#include "health.h"
#include "hotspot.h"
#include "idle.h"
#include "logo_lru.h"
#include "offseason.h"
#include "panel_text.h"
#include "recap.h"
#include "scroll_speed.h"
#include "setup_qr.h"
#include "poll_timing.h"
#include "startup.h"
#include "timezones.h"
#include "update_mirror.h"
#include "wifi_history.h"
#include "../components/crash_report/capture.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    checks++;                                                                  \
    auto va = (a);                                                             \
    auto vb = (b);                                                             \
    if (!(va == vb)) {                                                         \
      failures++;                                                              \
      std::ostringstream os;                                                   \
      os << va << " != " << vb;                                                \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str()); \
    }                                                                          \
  } while (0)

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Fixture facts (recorded 2026-09-05, see fixtures/README.md)
static const char *kLiveEvent = "401858433";  // BOIS @ ORE, 15:00 4th, ORE ball
static const char *kPostEvent = "401858432";  // BALL @ OSU, Final 3-56
static const char *kNflEvent = "401872656";   // SEA vs NE, 9/9 8:20 PM EDT
static const uint32_t kBoise = 68, kOregon = 2483, kBallState = 2050, kOhioState = 194, kSeahawks = 26, kPatriots = 17;

static void test_urls() {
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6"));
  // Monday night kickoff 00:20Z on 9/15 is still 9/14 in the US
  CHECK_EQ(scoreboard_url(League::NFL, 0, parse_iso8601_z("2026-09-15T00:20Z")), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=20260914"));
  // 2026-09-05T19:30Z is Sep 5 in the US; 2026-09-06T02:00Z is still Sep 5 Eastern
  CHECK_EQ(scoreboard_url(League::NCAA, 1, parse_iso8601_z("2026-09-05T19:30Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=1&dates=20260905"));
  CHECK_EQ(scoreboard_url(League::NCAA, 9, parse_iso8601_z("2026-09-06T02:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=9&dates=20260905"));
  CHECK_EQ(event_url(League::NFL, "401872999"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard/401872999"));
  CHECK_EQ(event_url(League::NCAA, "401871053"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard/401871053"));
  CHECK_EQ(dark_logo("https://a.espncdn.com/i/teamlogos/ncaa/500/68.png"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/68.png&w=64&h=64"));
  CHECK_EQ(dark_logo("https://a.espncdn.com/i/teamlogos/nfl/500/scoreboard/sea.png"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/sea.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NFL, 6, "DAL"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/dal.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NCAA, 228, "CLEM"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/228.png&w=64&h=64"));
  CHECK_EQ(dark_logo(""), std::string(""));
  // The bench's unreachable root reaches every URL, and the default comes back
  set_site_root("https://bench.invalid/sports/");
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://bench.invalid/sports/football/nfl/teams/6"));
  set_site_root("https://site.api.espn.com/apis/site/v2/sports/");
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6"));
}

static void test_iso() {
  CHECK_EQ(parse_iso8601_z("1970-01-01T00:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-09-05T19:30Z"), (int64_t) 1788636600);
  CHECK_EQ(parse_iso8601_z("2026-09-10T00:20:00Z"), (int64_t) 1788999600);
  CHECK_EQ(parse_iso8601_z("garbage"), (int64_t) 0);
  // HTTP Date headers, as ESPN sends them.
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 23:39:29 GMT"), (int64_t) 1792280369);
  CHECK_EQ(parse_http_date("Sat, 05 Sep 2026 19:30:00 GMT"), (int64_t) 1788636600);
  CHECK_EQ(parse_http_date("Sun, 01 Mar 2048 00:00:00 GMT"), (int64_t) 2466633600);
  CHECK_EQ(parse_http_date(""), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 23:39:29 UTC"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Okt 2026 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 7 Oct 2026 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 24:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2O26 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Thu, 01 Jan 1970 00:00:00 GMT"), (int64_t) 0);  // outside 2020 to 2099
  CHECK_EQ(parse_http_date("Saturday, 17-Oct-26 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat Oct 17 23:39:29 2026"), (int64_t) 0);
  // Fields too long or out of range read as no date instead of overflowing
  // (found by fuzz_espn).
  CHECK_EQ(parse_iso8601_z("2026-10-17T999999999:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-999999999-17T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-2147483647T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T19:999999999Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2147483647-10-17T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-13-01T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T24:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T-1:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("1969-12-31T23:59Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("9999-12-31T23:59:59Z"), (int64_t) 253402300799);
}

static void test_team_parse() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_bc.json"), s));
  CHECK(s.valid);
  CHECK_EQ(s.event_id, std::string("401856777"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-09-05T19:30Z"));
  CHECK_EQ(s.group, (uint32_t) 1);
  CHECK_EQ(s.team_color, std::string("8c2232"));
  CHECK_EQ(s.team_record, std::string("0-0"));
  CHECK_EQ(s.opp_id, (uint32_t) 2132);  // Cincinnati, the other side of the next game
  CHECK_EQ(s.opp_abbr, std::string("CIN"));

  Schedule d;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), d));
  CHECK_EQ(d.event_id, std::string("401872930"));
  CHECK_EQ(d.team_color, std::string("002a5c"));
  CHECK_EQ(d.team_record, std::string("2-1"));
  CHECK_EQ(d.opp_id, (uint32_t) 19);
  CHECK_EQ(d.opp_abbr, std::string("NYG"));

  // No next game: no opponent.
  Schedule none;
  CHECK(parse_team_str(R"({"team":{"id":"6","color":"002a5c","nextEvent":[]}})", none));
  CHECK(none.event_id.empty());
  CHECK_EQ(none.opp_id, (uint32_t) 0);

  Schedule bad;
  CHECK(!parse_team_str("{not json", bad));
  CHECK(!bad.valid);
}

static void test_scoreboard_live() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kLiveEvent, kBoise, s));
  CHECK(s.valid);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(s.team_abbr, std::string("BOIS"));
  CHECK_EQ(s.opp_abbr, std::string("ORE"));
  CHECK_EQ(s.team_id, kBoise);
  CHECK_EQ(s.opp_id, kOregon);
  CHECK_EQ(s.team_score, 24);
  CHECK_EQ(s.opp_score, 24);
  CHECK_EQ(s.possession, 2);  // Oregon has the ball
  CHECK_EQ(s.team_timeouts, 1);
  CHECK_EQ(s.opp_timeouts, 1);
  CHECK_EQ(s.down_distance, std::string("3rd & 10 at BOIS 25"));
  CHECK_EQ(s.short_down_distance, std::string("3rd & 10"));
  CHECK(!s.is_red_zone);
  CHECK_EQ(s.short_detail, std::string("15:00 - 4th"));
  CHECK_EQ(s.period, 4);
  CHECK_EQ(s.team_color, std::string("0033a0"));
  CHECK_EQ(s.opp_color, std::string("00934b"));
  CHECK_EQ(s.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/68.png&w=64&h=64"));
  CHECK_EQ(s.opp_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/2483.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NCAA, s.team_id, "BOIS"), s.team_logo);
  CHECK_EQ(team_logo_url(League::NCAA, s.opp_id, s.opp_abbr.c_str()), s.opp_logo);
  CHECK(s.last_play.rfind("End of 3rd quarter", 0) == 0);
  CHECK(!s.completed);

  // Same event from Oregon's side flips everything
  GameSnapshot o;
  CHECK(parse_scoreboard_str(json, kLiveEvent, kOregon, o));
  CHECK_EQ(o.team_abbr, std::string("ORE"));
  CHECK_EQ(o.possession, 1);

  // Event present but we are not in it
  GameSnapshot x;
  CHECK(!parse_scoreboard_str(json, kLiveEvent, kSeahawks, x));
  // Event missing
  CHECK(!parse_scoreboard_str(json, "1", kBoise, x));
}

// The single-game document (event_url), recorded 2026-10-13 during FIU @ JXST.
static void test_event_live() {
  std::string json = slurp("fixtures/event_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_event_str(json, Sport::FOOTBALL, 2229, s, "401871053"));  // FIU's side
  CHECK(s.valid);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(s.team_abbr, std::string("FIU"));
  CHECK_EQ(s.opp_abbr, std::string("JXST"));
  CHECK_EQ(s.team_score, 24);
  CHECK_EQ(s.opp_score, 27);
  CHECK_EQ(s.possession, 1);  // FIU has the ball
  CHECK_EQ(s.team_timeouts, 0);
  CHECK_EQ(s.opp_timeouts, 1);
  CHECK_EQ(s.down_distance, std::string("3rd & 4 at FIU 31"));
  CHECK_EQ(s.short_detail, std::string("0:33 - 4th"));
  CHECK_EQ(s.team_record, std::string("3-2"));
  CHECK_EQ(s.tv, std::string("ESPNU"));
  CHECK_EQ(s.venue, std::string("AmFirst Stadium"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-14T00:00Z"));
  CHECK(s.last_play.find("J.Kohl pass complete") != std::string::npos);

  GameSnapshot x;
  CHECK(!parse_event_str(json, Sport::FOOTBALL, kSeahawks, x, "401871053"));  // not our game
  CHECK(!parse_event_str(json, Sport::FOOTBALL, 2229, x, "401871054"));       // some other event
  CHECK(!parse_event_str("{\"code\":404,\"message\":\"No event found\"}", Sport::FOOTBALL, 2229, x, "401871053"));
  CHECK(!parse_event_str("{not json", Sport::FOOTBALL, 2229, x, "401871053"));

  GameSnapshot chi;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_pre.json"), Sport::FOOTBALL, 3, chi, "401872999"));
  CHECK(chi.state == GameState::PRE);
  CHECK_EQ(chi.opp_abbr, std::string("ATL"));
  CHECK_EQ(chi.odds, std::string("CHI -3"));
  CHECK_EQ(chi.over_under, std::string("49.5"));
  CHECK_EQ(chi.tv, std::string("Fox"));

  GameSnapshot sea;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_super_bowl.json"), Sport::FOOTBALL, 26, sea, "401772988"));
  CHECK(sea.completed);
  CHECK(sea.team_winner);
  CHECK_EQ(sea.game_name, std::string("Super Bowl LX"));
}

// The over/under as ESPN sends it, and values no total can have. A huge one
// was undefined behavior in the cast to int (found by fuzz_espn); it now reads
// as no over/under at all. Everything that read before reads the same.
static void test_over_under_values() {
  const std::string json = slurp("fixtures/event_nfl_pre.json");
  const std::string from = "\"overUnder\":49.5";
  CHECK(json.find(from) != std::string::npos);
  auto with = [&](const char *value) {
    std::string doc = json;
    doc.replace(doc.find(from), from.size(), std::string("\"overUnder\":") + value);
    GameSnapshot s;
    CHECK(parse_event_str(doc, Sport::FOOTBALL, 3, s, "401872999"));
    return s.over_under;
  };
  CHECK_EQ(with("49.5"), std::string("49.5"));
  CHECK_EQ(with("47"), std::string("47"));
  CHECK_EQ(with("2.5"), std::string("2.5"));
  CHECK_EQ(with("228.5"), std::string("228.5"));
  CHECK_EQ(with("2164294967296.5"), std::string(""));
  CHECK_EQ(with("1e300"), std::string(""));
  CHECK_EQ(with("-1e300"), std::string(""));
  CHECK_EQ(with("-9999.5"), std::string("-9999.5"));
  CHECK_EQ(with("9999.5"), std::string("9999.5"));
  CHECK_EQ(with("10000"), std::string(""));
  CHECK_EQ(with("-10000"), std::string(""));
  CHECK_EQ(with("0"), std::string("0"));
  CHECK_EQ(with("\"49.5\""), std::string("49.5"));  // ArduinoJson reads a number in a string
  CHECK_EQ(with("null"), std::string(""));
  // The ticker leaves the part out rather than showing "O/U " with nothing.
  std::string doc = json;
  doc.replace(doc.find(from), from.size(), "\"overUnder\":2164294967296.5");
  GameSnapshot s;
  CHECK(parse_event_str(doc, Sport::FOOTBALL, 3, s, "401872999"));
  CHECK(status_text(s, TickerOptions{}, "Sun 1:00 PM").find("O/U") == std::string::npos);
}

// Documents with parts missing, empty or null, in every sport: each parser
// says no rather than inventing a game, and a game with only some fields
// still reads and draws.
static void test_odd_documents() {
  const Sport sports[] = {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER};
  for (Sport sport : sports) {
    GameSnapshot x;
    CHECK(!parse_event_str(R"({"id":"1"})", sport, 3, x, "1"));
    CHECK(!parse_event_str(R"({"id":"1","competitions":[]})", sport, 3, x, "1"));
    CHECK(!parse_event_str(R"({"id":"1","competitions":null})", sport, 3, x, "1"));
    CHECK(!parse_event_str(R"({"id":"1","competitions":[{"competitors":[]}]})", sport, 3, x, "1"));
    CHECK(!parse_event_str(R"({"id":"1","competitions":[{"competitors":null}]})", sport, 3, x, "1"));
    CHECK(!parse_event_str(R"({"id":"1","competitions":[{"competitors":[{"id":"4"},{"id":"5"}]}]})", sport, 3, x,
                           "1"));  // not our game
    CHECK(!x.valid);
    // Our side and nothing else: no status, no score, no opponent.
    GameSnapshot g;
    CHECK(parse_event_str(
        R"({"id":"1","status":null,"competitions":[{"situation":null,"odds":null,"competitors":[{"id":"3","score":null,"team":null},{"homeAway":"away"}]}]})",
        sport, 3, g, "1"));
    CHECK(g.valid);
    CHECK(g.state == GameState::PRE);
    CHECK_EQ(g.team_score, 0);
    CHECK_EQ(g.opp_id, (uint32_t) 0);
    CHECK(g.opp_abbr.empty());
    CHECK(g.over_under.empty());
    TickerOptions all;
    status_text(g, all, "");
    clock_text(g);
    ha_status_text(g, "");
  }
  GameSnapshot x;
  CHECK(!parse_scoreboard_str(R"({"events":null})", "1", 3, x));
  CHECK(!parse_scoreboard_str(R"({"events":[{"id":"1","competitions":[]}]})", "1", 3, x));
  std::vector<LiveGame> live;
  int64_t next = 5;
  std::string scan = R"({"events":[{"status":{"type":{"state":"in"}},"competitions":[]}]})";
  CHECK(parse_live_games(scan, live, &next));
  CHECK(live.empty());  // a live game with no id or sides is not followed
  CHECK_EQ(next, (int64_t) 0);
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(R"({"events":[]})", 3, 4, up));
  CHECK(up.empty());
  Schedule s;
  CHECK(parse_team_str(R"({"team":{}})", s));
  CHECK(s.valid);
  CHECK(s.event_id.empty());
  CHECK(!parse_team_str(R"({"code":404})", s));
}

static bool same_snapshot(const GameSnapshot &a, const GameSnapshot &b) {
  return a.valid == b.valid && a.state == b.state && a.event_id == b.event_id && a.kickoff_epoch == b.kickoff_epoch &&
         a.completed == b.completed && a.short_detail == b.short_detail && a.display_clock == b.display_clock &&
         a.period == b.period && a.team_abbr == b.team_abbr && a.opp_abbr == b.opp_abbr && a.team_id == b.team_id &&
         a.opp_id == b.opp_id && a.team_score == b.team_score && a.opp_score == b.opp_score &&
         a.team_record == b.team_record && a.opp_record == b.opp_record && a.team_color == b.team_color &&
         a.opp_color == b.opp_color && a.team_logo == b.team_logo && a.opp_logo == b.opp_logo &&
         a.team_winner == b.team_winner && a.possession == b.possession && a.team_timeouts == b.team_timeouts &&
         a.opp_timeouts == b.opp_timeouts && a.last_play == b.last_play && a.down_distance == b.down_distance &&
         a.short_down_distance == b.short_down_distance && a.is_red_zone == b.is_red_zone && a.odds == b.odds &&
         a.over_under == b.over_under && a.tv == b.tv && a.venue == b.venue && a.game_name == b.game_name;
}

// ESPN's single-game document is the game's entry from the day's scoreboard on
// its own (checked against 48 live, upcoming and final games on 2026-10-13).
// So every game in every scoreboard fixture, cut out on its own, must read the
// same through parse_event as through parse_scoreboard, from both sides.
static void test_event_matches_scoreboard() {
  const char *files[] = {"fixtures/scoreboard_ncaa_live.json",          "fixtures/scoreboard_nfl.json",
                         "fixtures/scoreboard_ncaa_conf_titles.json",   "fixtures/scoreboard_ncaa_cfp_quarterfinals.json",
                         "fixtures/scoreboard_ncaa_cfp_final.json",     "fixtures/scoreboard_nfl_wild_card.json",
                         "fixtures/scoreboard_nfl_super_bowl.json"};
  int compared = 0;
  for (const char *file : files) {
    std::string json = slurp(file);
    JsonDocument doc;
    CHECK(!deserializeJson(doc, json, DeserializationOption::NestingLimit(40)));
    for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
      std::string id = ev["id"] | "";
      std::string one;
      serializeJson(ev, one);
      for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
        uint32_t team = (uint32_t) atol(c["id"] | "0");
        GameSnapshot from_board, alone;
        bool ok_board = parse_scoreboard_str(json, id, team, from_board);
        bool ok_alone = parse_event_str(one, Sport::FOOTBALL, team, alone, id);
        CHECK_EQ(ok_alone, ok_board);
        if (!same_snapshot(alone, from_board)) {
          failures++;
          printf("FAIL %s: event %s team %u reads differently on its own\n", file, id.c_str(), (unsigned) team);
        }
        compared++;
      }
    }
  }
  CHECK(compared > 200);
}

static void test_scoreboard_post() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, s));
  CHECK(s.state == GameState::POST);
  CHECK(s.completed);
  CHECK_EQ(s.team_score, 3);
  CHECK_EQ(s.opp_score, 56);
  CHECK(!s.team_winner);
  CHECK_EQ(s.team_record, std::string("0-1"));
  CHECK_EQ(s.opp_record, std::string("1-0"));
  CHECK_EQ(s.short_detail, std::string("Final"));
  GameSnapshot w;
  CHECK(parse_scoreboard_str(json, kPostEvent, kOhioState, w));
  CHECK(w.team_winner);
}

static void test_scoreboard_pre_nfl() {
  std::string json = slurp("fixtures/scoreboard_nfl.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kNflEvent, kPatriots, s));
  CHECK(s.state == GameState::PRE);
  CHECK_EQ(s.team_abbr, std::string("NE"));
  CHECK_EQ(s.opp_abbr, std::string("SEA"));
  CHECK_EQ(s.odds, std::string("SEA -3.5"));
  CHECK_EQ(s.over_under, std::string("44.5"));
  CHECK_EQ(s.tv, std::string("NBC"));
  CHECK_EQ(s.venue, std::string("Lumen Field"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-09-10T00:20Z"));
  CHECK_EQ(s.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/ne.png&w=64&h=64"));
  // The logo cache keys on the URL: the one built before the scoreboard
  // arrives must be the one the scoreboard gives, or it downloads twice.
  CHECK_EQ(team_logo_url(League::NFL, s.team_id, "NE"), s.team_logo);
  CHECK_EQ(team_logo_url(League::NFL, s.opp_id, s.opp_abbr.c_str()), s.opp_logo);
  CHECK_EQ(s.possession, 0);
}

static GameSnapshot live(int us, int them, GameState st = GameState::IN) {
  GameSnapshot s;
  s.valid = true;
  s.state = st;
  s.completed = st == GameState::POST;
  s.event_id = "1";
  s.team_abbr = "DAL";
  s.opp_abbr = "PHI";
  s.team_score = us;
  s.opp_score = them;
  s.team_color = "002a5c";
  s.opp_color = "004c54";
  return s;
}

static void test_splash() {
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(0, 0), live(7, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(0, 0), live(8, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(7, 0), live(10, 0), true).text, std::string("FIELD GOAL!"));
  CHECK_EQ(decide_splash(live(6, 0), live(7, 0), true).text, std::string("EXTRA POINT!"));
  // Two points are a conversion unless something says safety: the play, or
  // a read before from the middle of a drive (alerts.h).
  CHECK_EQ(decide_splash(live(6, 0), live(8, 0), true).text, std::string("2-PT!"));
  GameSnapshot td = live(6, 0);
  td.last_play = "Pass short left to D.Moore for 11 yards, TOUCHDOWN.";
  CHECK_EQ(decide_splash(td, live(8, 0), true).text, std::string("2-PT!"));
  GameSnapshot drive = live(6, 0);
  drive.possession = 2;
  drive.down_distance = "3rd & 9 at PHI 3";
  CHECK_EQ(decide_splash(drive, live(8, 0), true).text, std::string("SAFETY!"));
  CHECK_EQ(decide_splash(live(0, 0), live(5, 0), true).text, std::string("SCORE!"));
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true).color, (uint32_t) 0x002a5c);
  // Opponent
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), true).text, std::string("PHI TOUCHDOWN"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 3), true).text, std::string("PHI FIELD GOAL"));
  CHECK_EQ(decide_splash(live(0, 6), live(0, 7), true).text, std::string("PHI EXTRA POINT"));
  CHECK_EQ(decide_splash(live(0, 6), live(0, 8), true).text, std::string("PHI 2-PT"));
  GameSnapshot safety_them = live(0, 8);
  safety_them.last_play = "D.Prescott sacked in End Zone for -7 yards, SAFETY.";
  CHECK_EQ(decide_splash(live(0, 6), safety_them, true).text, std::string("PHI SAFETY"));
  GameSnapshot td_them = live(0, 6);
  td_them.last_play = "J.Hurts up the middle for 1 yard, TOUCHDOWN.";
  CHECK_EQ(decide_splash(td_them, live(0, 8), true).text, std::string("PHI 2-PT"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), true).color, (uint32_t) 0x004c54);
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), false).text, std::string(""));
  // Both scored between polls: ours wins
  CHECK_EQ(decide_splash(live(0, 0), live(7, 3), true).text, std::string("TOUCHDOWN!"));
  // No splash cases
  CHECK_EQ(decide_splash(GameSnapshot{}, live(7, 0), true).text, std::string(""));  // boot mid-game
  CHECK_EQ(decide_splash(live(0, 0, GameState::PRE), live(7, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(7, 0), live(7, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(7, 0), live(0, 0), true).text, std::string(""));  // correction
  GameSnapshot other = live(0, 0);
  other.event_id = "2";
  CHECK_EQ(decide_splash(other, live(7, 0), true).text, std::string(""));
  // Victory
  CHECK_EQ(decide_splash(live(21, 20), live(21, 20, GameState::POST), true).text, std::string("DAL WINS!"));
  CHECK_EQ(decide_splash(live(20, 21), live(20, 21, GameState::POST), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(21, 20, GameState::POST), live(21, 20, GameState::POST), true).text, std::string(""));
  // Suspended or canceled mid-game: post, but no winner
  GameSnapshot halted = live(21, 20, GameState::POST);
  halted.completed = false;
  halted.short_detail = "Suspended";
  CHECK_EQ(decide_splash(live(21, 20), halted, true).text, std::string(""));
  CHECK_EQ(decide_splash(live(20, 21), halted, true, true).text, std::string(""));
}

// The victory splash follows the same rules in every sport: the game went
// from live to over between two reads of it, completed, with a winner.
static void test_win_splash_every_sport() {
  for (Sport sport : {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER}) {
    auto at = [sport](int us, int them, GameState st, bool completed) {
      GameSnapshot s = live(us, them, st);
      s.sport = sport;
      s.completed = completed;
      return s;
    };
    const GameSnapshot was = at(3, 3, GameState::IN, false);
    // Basketball says FINAL when there is no win to celebrate (alerts.h).
    const std::string no_win = sport == Sport::BASKETBALL ? "FINAL" : "";
    CHECK_EQ(decide_splash(was, at(4, 3, GameState::POST, true), true).text, std::string("DAL WINS!"));
    CHECK_EQ(decide_splash(was, at(4, 3, GameState::POST, true), false).color, (uint32_t) 0x002a5c);
    CHECK_EQ(decide_splash(was, at(3, 4, GameState::POST, true), true).text, no_win);
    CHECK_EQ(decide_splash(was, at(3, 4, GameState::POST, true), false, true).text, std::string("PHI WINS!"));
    CHECK_EQ(decide_splash(was, at(3, 4, GameState::POST, true), false, true).color, (uint32_t) 0x004c54);
    CHECK_EQ(decide_splash(was, at(3, 3, GameState::POST, true), true, true).text, no_win);  // a draw
    CHECK_EQ(decide_splash(was, at(3, 4, GameState::POST, true), true, false, kAlertsAll & ~alert_bit(Alert::FINAL)).text,
             std::string(""));
    // Suspended or canceled: post, never completed, no winner
    CHECK_EQ(decide_splash(was, at(4, 3, GameState::POST, false), true).text, std::string(""));
    CHECK_EQ(decide_splash(was, at(3, 4, GameState::POST, false), true, true).text, std::string(""));
    // Already over at the read before, after a boot, or another game
    CHECK_EQ(decide_splash(at(4, 3, GameState::POST, true), at(4, 3, GameState::POST, true), true).text,
             std::string(""));
    CHECK_EQ(decide_splash(GameSnapshot{}, at(4, 3, GameState::POST, true), true).text, std::string(""));
    GameSnapshot other = at(4, 3, GameState::POST, true);
    other.event_id = "2";
    CHECK_EQ(decide_splash(was, other, true).text, std::string(""));
  }
}

// A live document with its two competitors' scores replaced, in the order
// the document lists them.
static std::string with_scores(std::string doc, const char *first, const char *second) {
  const std::string key = "\"score\":\"";
  size_t at = 0;
  for (const char *v : {first, second}) {
    at = doc.find(key, at);
    CHECK(at != std::string::npos);
    if (at == std::string::npos)
      return doc;
    at += key.size();
    doc.replace(at, doc.find('"', at) - at, v);
    at += strlen(v);
  }
  return doc;
}

// Scores are atoi of ESPN's text, so a broken document can carry any int.
// One live game read twice with its scores jumping between the extremes:
// working out what was scored never overflows, a drop is a correction with
// no splash, and a gain too big to be real gets the plainest splash, the
// same one a jump from 0 to the largest int gets.
static void test_splash_extreme_scores() {
  const char *kMax = "2147483647", *kMin = "-2147483648", *kZero = "0";
  const char *values[] = {kMax, kMin, kZero};
  struct Live {
    Sport sport;
    const char *file;
    uint32_t first;  // the document's first competitor, followed here
    const char *event;
  };
  const Live games[] = {
      {Sport::FOOTBALL, "fixtures/event_ncaa_live.json", 55, "401871053"},            // JXST, then FIU
      {Sport::BASEBALL, "fixtures/event_mlb_top.json", 15, "401908016"},              // ATL, then LAD
      {Sport::SOCCER, "fixtures/event_soccer_second_half.json", 6079, "401841253"},  // BRA, then MIR
      {Sport::HOCKEY, "fixtures/event_nhl_live.json", 28, "401892455"},               // WPG, then COL
      {Sport::BASKETBALL, "fixtures/event_nba_live.json", 11, "401914123"},           // IND, then MIN
  };
  for (const Live &g : games) {
    const std::string doc = slurp(g.file);
    GameSnapshot reads[3][3];
    for (int us = 0; us < 3; us++)
      for (int them = 0; them < 3; them++) {
        CHECK(parse_event_str(with_scores(doc, values[us], values[them]), g.sport, g.first, reads[us][them], g.event));
        CHECK_EQ(reads[us][them].team_score, atoi(values[us]));
        CHECK_EQ(reads[us][them].opp_score, atoi(values[them]));
      }
    for (const auto &row0 : reads)
      for (const GameSnapshot &before : row0)
        for (const auto &row1 : reads)
          for (const GameSnapshot &after : row1)
            for (int flags = 0; flags < 4; flags++) {
              const Splash s = decide_splash(before, after, (flags & 1) != 0, (flags & 2) != 0);
              if (after.team_score <= before.team_score && after.opp_score <= before.opp_score)
                CHECK_EQ(s.text, std::string(""));
            }
  }
  // read(game, us, them) indexes games and values: 0 the largest int, 1 the
  // smallest, 2 zero.
  auto read = [&](int game, int us, int them) {
    GameSnapshot s;
    const Live &g = games[game];
    CHECK(parse_event_str(with_scores(slurp(g.file), values[us], values[them]), g.sport, g.first, s, g.event));
    return s;
  };
  // Football: up from the smallest to the largest is a score, the other way
  // a correction (it used to wrap to 1 and splash "EXTRA POINT!").
  CHECK_EQ(decide_splash(read(0, 1, 2), read(0, 0, 2), true).text, std::string("SCORE!"));
  CHECK_EQ(decide_splash(read(0, 2, 2), read(0, 0, 2), true).text, std::string("SCORE!"));
  CHECK_EQ(decide_splash(read(0, 1, 2), read(0, 0, 2), true, true).text, std::string("JXST SCORE"));
  CHECK_EQ(decide_splash(read(0, 0, 2), read(0, 1, 2), true).text, std::string(""));
  CHECK_EQ(decide_splash(read(0, 0, 2), read(0, 2, 2), true).text, std::string(""));
  CHECK_EQ(decide_splash(read(0, 2, 1), read(0, 2, 0), true).text, std::string("FIU SCORE"));
  CHECK_EQ(decide_splash(read(0, 2, 1), read(0, 2, 0), false).text, std::string(""));
  CHECK_EQ(decide_splash(read(0, 2, 0), read(0, 2, 1), true).text, std::string(""));
  // Baseball: the count tops out at the largest int.
  CHECK_EQ(decide_splash(read(1, 1, 2), read(1, 0, 2), true).text, std::string("2147483647 RUNS!"));
  CHECK_EQ(decide_splash(read(1, 2, 2), read(1, 0, 2), true).text, std::string("2147483647 RUNS!"));
  CHECK_EQ(decide_splash(read(1, 0, 2), read(1, 1, 2), true).text, std::string(""));
  CHECK_EQ(decide_splash(read(1, 2, 1), read(1, 2, 0), true).text, std::string("LAD 2147483647 RUNS"));
  CHECK_EQ(decide_splash(read(1, 2, 0), read(1, 2, 1), true).text, std::string(""));
  // Soccer and hockey only compare: a goal up, nothing down.
  for (int game : {2, 3}) {
    CHECK_EQ(decide_splash(read(game, 1, 2), read(game, 0, 2), true).text, std::string("GOAL!"));
    CHECK_EQ(decide_splash(read(game, 0, 2), read(game, 1, 2), true).text, std::string(""));
  }
  // Basketball splashes only the win.
  CHECK_EQ(decide_splash(read(4, 1, 2), read(4, 0, 2), true).text, std::string(""));
}

static void test_status_text() {
  TickerOptions all;
  GameSnapshot s = live(24, 24);
  s.down_distance = "3rd & 10 at BOIS 25";
  s.short_detail = "15:00 - 4th";
  s.last_play = "End of 3rd quarter.";
  CHECK_EQ(status_text(s, all, ""), std::string("3rd & 10 at BOIS 25 | 15:00 - 4th | End of 3rd quarter."));
  TickerOptions no_clock = all;
  no_clock.clock = false;
  CHECK_EQ(status_text(s, no_clock, ""), std::string("3rd & 10 at BOIS 25 | End of 3rd quarter."));
  TickerOptions none;
  none.clock = none.down_distance = none.last_play = false;
  CHECK_EQ(status_text(s, none, ""), std::string("15:00 - 4th"));
  // Baseball's and soccer's switches don't touch football's.
  TickerOptions others_off = all;
  others_off.baseball_count = others_off.soccer_stats = false;
  CHECK_EQ(status_text(s, others_off, ""), status_text(s, all, ""));
  // Long play is truncated
  s.last_play = std::string(200, 'x');
  TickerOptions only_play;
  only_play.clock = only_play.down_distance = false;
  std::string t = status_text(s, only_play, "");
  CHECK_EQ(t.size(), (size_t) 160);
  CHECK(t.substr(157) == "...");
  // Halftime
  GameSnapshot h = live(14, 7);
  h.short_detail = "Halftime";
  h.team_record = "2-0";
  h.opp_record = "1-1";
  CHECK_EQ(status_text(h, all, ""), std::string("Halftime | DAL 2-0 | PHI 1-1"));
  // Pre
  GameSnapshot p = live(0, 0, GameState::PRE);
  p.odds = "SEA -3.5";
  p.over_under = "44.5";
  p.tv = "NBC";
  p.venue = "Lumen Field";
  CHECK_EQ(status_text(p, all, "Wed 7:20 PM"), std::string("Wed 7:20 PM | SEA -3.5 | O/U 44.5 | NBC | Lumen Field"));
  TickerOptions no_odds = all;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, "Wed 7:20 PM"), std::string("Wed 7:20 PM | Lumen Field"));
  // A named game leads, and a venue that repeats the name is dropped
  p.game_name = "Super Bowl LX";
  CHECK_EQ(status_text(p, no_odds, "Sun 5:30 PM"), std::string("Super Bowl LX | Sun 5:30 PM | Lumen Field"));
  p.game_name = p.venue = "Rose Bowl";
  CHECK_EQ(status_text(p, no_odds, "Thu 3:00 PM"), std::string("Rose Bowl | Thu 3:00 PM"));
  // Post
  GameSnapshot f = live(21, 20, GameState::POST);
  f.team_record = "3-0";
  f.opp_record = "2-1";
  CHECK_EQ(status_text(f, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  f.short_detail = "Final/OT";
  CHECK_EQ(status_text(f, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  // Postponed or canceled: post too, but never called a final
  GameSnapshot pp = live(0, 0, GameState::POST);
  pp.completed = false;
  pp.short_detail = "Postponed";
  pp.team_record = "3-0";
  pp.opp_record = "2-1";
  CHECK_EQ(status_text(pp, all, ""), std::string("Postponed | DAL 3-0 | PHI 2-1"));
  pp.short_detail = "Canceled";
  CHECK_EQ(status_text(pp, none, ""), std::string("Canceled | DAL 3-0 | PHI 2-1"));
  pp.short_detail = "";
  CHECK_EQ(status_text(pp, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  GameSnapshot nf;
  CHECK_EQ(status_text(nf, all, ""), std::string("No upcoming game"));
}

// The words for an empty board. "No upcoming game" (the empty answer) is only
// right once ESPN said so; until then a failed read is ESPN out of reach.
static void test_no_game_text() {
  // No clock yet: nothing can have been read, whatever else is true
  CHECK_EQ(no_game_text(false, false, false, 0), std::string("Getting ready"));
  CHECK_EQ(no_game_text(false, true, true, 5), std::string("Getting ready"));
  // Clock set, first read still out
  CHECK_EQ(no_game_text(true, false, false, 0), std::string(""));
  // Reads failing, ESPN never answered for this team: after boot or a team change
  CHECK_EQ(no_game_text(true, false, false, 1), std::string("Can't reach ESPN"));
  CHECK_EQ(no_game_text(true, false, false, 7), std::string("Can't reach ESPN"));
  // ESPN said there is no game: stays so when later reads fail (the ticker adds the age)
  CHECK_EQ(no_game_text(true, false, true, 0), std::string(""));
  CHECK_EQ(no_game_text(true, false, true, 3), std::string(""));
  // Live modes keep their own "Looking for a live game"
  CHECK_EQ(no_game_text(true, true, false, 1), std::string(""));
  CHECK_EQ(no_game_text(true, true, false, 3), std::string(""));
  // A sign-in Wi-Fi says so in place of all of them, live modes too
  CHECK_EQ(no_game_text(false, false, false, 0, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, false, false, 2, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, true, false, 1, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, false, false, 2, false), std::string("Can't reach ESPN"));
}

// The plain HTTP read of ESPN after joining Wi-Fi (captive.h). Only a clear
// portal answer is a sign-in network; anything ESPN could have sent is not.
static void test_captive() {
  // ESPN today: 200, JSON, "{}"
  CHECK(classify_probe(200, "", "application/json;charset=UTF-8", "{}") == Probe::OPEN);
  CHECK(classify_probe(200, "", "", "  \r\n[1]") == Probe::OPEN);
  CHECK(classify_probe(200, "", "application/json", "") == Probe::OPEN);
  CHECK(classify_probe(200, "", "", "\xEF\xBB\xBF{\"a\":1}") == Probe::OPEN);
  // ESPN's JSON error for a URL it no longer knows still means ESPN answered
  CHECK(classify_probe(404, "", "application/json", "{\"code\":404}") == Probe::OPEN);
  // Portals: a redirect to their sign-in page, over HTTP or HTTPS
  CHECK(classify_probe(302, "http://192.168.10.1:8002/index.php?zone=guest", "text/html", "") == Probe::CAPTIVE);
  CHECK(classify_probe(302, "https://login.hotelwifi.example/portal?mac=aa:bb", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(307, "HTTPS://Splash.Example.NET/", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(301, "http://user@portal.example/x", "", "") == Probe::CAPTIVE);
  // or their page in ESPN's place, or the status made for it
  CHECK(classify_probe(200, "", "text/html", "<!DOCTYPE html><html><head><title>Sign in") == Probe::CAPTIVE);
  CHECK(classify_probe(200, "", "", "\n  <html>") == Probe::CAPTIVE);
  CHECK(classify_probe(200, "", "text/html; charset=utf-8", "") == Probe::CAPTIVE);
  CHECK(classify_probe(511, "", "text/html", "<html>") == Probe::CAPTIVE);
  CHECK(classify_probe(511, "", "", "") == Probe::CAPTIVE);
  // ESPN sending plain HTTP on to HTTPS one day is still ESPN
  CHECK(classify_probe(301, "https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6/record", "", "") ==
        Probe::OPEN);
  CHECK(classify_probe(302, "https://secure.espn.com:443/x", "", "") == Probe::OPEN);
  CHECK(classify_probe(302, "https://espn.com", "", "") == Probe::OPEN);
  // but a host that only ends in the same letters is not
  CHECK(classify_probe(302, "http://notespn.com/login", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(302, "http://espn.com.portal.example/login", "", "") == Probe::CAPTIVE);
  // Proves nothing: no answer, a relative or missing redirect, an error page
  // (ESPN's own Akamai "Access Denied" is HTML), an empty or odd body
  CHECK(classify_probe(0, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(-1, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "/login", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(403, "", "text/html", "<HTML><HEAD>\n<TITLE>Access Denied</TITLE>") == Probe::UNKNOWN);
  CHECK(classify_probe(503, "", "text/html", "<html>") == Probe::UNKNOWN);
  CHECK(classify_probe(200, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(200, "", "text/plain", "OK") == Probe::UNKNOWN);
  CHECK(classify_probe(204, "", "", "") == Probe::UNKNOWN);

  // When to probe: once per join
  CHECK(probe_due(false, false, false, 0));
  CHECK(probe_due(false, true, true, 0));
  CHECK(!probe_due(true, false, false, 10 * 60 * 1000));
  // again every minute on a sign-in network
  CHECK(!probe_due(true, true, false, kProbeCaptiveRecheckMs - 1));
  CHECK(probe_due(true, true, false, kProbeCaptiveRecheckMs));
  // every two while reads fail on one that looked open
  CHECK(!probe_due(true, false, true, kProbeFailingRecheckMs - 1));
  CHECK(probe_due(true, false, true, kProbeFailingRecheckMs));
}

// The ticker's speed (scroll_speed.h): Scroll Speed 1 to 10 as pixels per
// second, and the scroll time that keeps a line of any width at that pace.
static void test_ticker_speed() {
  using esphome::gameday::ticker_px_per_s;
  using esphome::gameday::ticker_scroll_ms;
  CHECK(std::fabs(ticker_px_per_s(1) - 8.0f) < 0.01f);
  CHECK(std::fabs(ticker_px_per_s(10) - 32.0f) < 0.01f);
  // A new panel's 5: about 15 px/s, three letters of the 5 px font a second
  CHECK(ticker_px_per_s(5) > 14.0f && ticker_px_per_s(5) < 16.0f);
  // Every step faster than the last, by the same ratio
  for (int s = 1; s < 10; s++) {
    CHECK(ticker_px_per_s(s + 1) > ticker_px_per_s(s));
    CHECK(std::fabs(ticker_px_per_s(s + 1) / ticker_px_per_s(s) - ticker_px_per_s(2) / ticker_px_per_s(1)) < 0.001f);
  }
  // Out of range and NaN clamp instead of running away
  CHECK(ticker_px_per_s(0) == ticker_px_per_s(1));
  CHECK(ticker_px_per_s(42) == ticker_px_per_s(10));
  CHECK(ticker_px_per_s(NAN) == ticker_px_per_s(1));
  // 120 px of line and a 15 px gap at 1 (8 px/s): 135 px in 16.875 s
  CHECK_EQ(ticker_scroll_ms(120, 15, 1), (uint32_t) 16875);
  // The same line at 10 (32 px/s): 4.2 s, never a blur
  CHECK_EQ(ticker_scroll_ms(120, 15, 10), (uint32_t) 4219);
  // Twice the width takes twice the time at the same setting
  CHECK(ticker_scroll_ms(240, 0, 5) > 2 * ticker_scroll_ms(120, 0, 5) - 2);
  CHECK(ticker_scroll_ms(240, 0, 5) < 2 * ticker_scroll_ms(120, 0, 5) + 2);
  CHECK_EQ(ticker_scroll_ms(0, 0, 5), (uint32_t) 1);
}

// The boot screen's QR codes (setup_qr.h): a Wi-Fi join code for the
// panel's open hotspot, read by the iPhone and Android cameras, and the
// device page by IP. No app in either.
static void test_setup_qr() {
  using esphome::gameday::device_page_qr;
  using esphome::gameday::wifi_join_qr;
  CHECK_EQ(wifi_join_qr("Game Day 2f6a70"), std::string("WIFI:T:nopass;S:Game Day 2f6a70;;"));
  // The Wi-Fi QR format's specials, each escaped with a backslash
  CHECK_EQ(wifi_join_qr("a;b,c:d\\e\"f"), std::string("WIFI:T:nopass;S:a\\;b\\,c\\:d\\\\e\\\"f;;"));
  CHECK_EQ(wifi_join_qr("Café's"), std::string("WIFI:T:nopass;S:Café's;;"));
  CHECK_EQ(wifi_join_qr(""), std::string("WIFI:T:nopass;S:;;"));
  // 33 characters: LVGL's version 3 at M, 29x29 at 1 px per module on its 33 px canvas
  CHECK_EQ(wifi_join_qr("Game Day 2f6a70").size(), (size_t) 33);

  CHECK_EQ(device_page_qr("192.168.1.50"), std::string("http://192.168.1.50"));
  CHECK_EQ(device_page_qr(""), std::string(""));
  // A long address, 192.168.100.200, makes 22 characters, within version 2 at M (26)
  CHECK_EQ(device_page_qr("192.168.100.200").size(), (size_t) 22);
  // A typical home address stays within version 2 at error correction Q (20 bytes).
  CHECK(device_page_qr("192.168.29.10").size() <= 20);
}

// The ticker's address line after a join: the IP first, then the .local
// name, for 30 seconds from when it started, across the millis() wrap.
static void test_join_ticker() {
  using esphome::gameday::join_ticker_on;
  using esphome::gameday::join_ticker_text;
  using esphome::gameday::kJoinTickerMs;
  CHECK_EQ(join_ticker_text("192.168.1.23", "gameday-2f6a70"),
           std::string("IP 192.168.1.23 | gameday-2f6a70.local | "));
  CHECK_EQ(join_ticker_text("192.168.1.23", ""), std::string("IP 192.168.1.23 | "));
  // No address, off Wi-Fi: nothing, not "IP  | "
  CHECK_EQ(join_ticker_text("", "gameday-2f6a70"), std::string(""));

  CHECK_EQ(kJoinTickerMs, (uint32_t) 30000);
  CHECK(!join_ticker_on(0, 5000));  // never started
  CHECK(join_ticker_on(1000, 1000));
  CHECK(join_ticker_on(1000, 30999));
  CHECK(!join_ticker_on(1000, 31000));
  CHECK(!join_ticker_on(1000, 90000));
  // Started just before millis() wrapped
  CHECK(join_ticker_on(0xFFFFF000u, 0x00001000u));
  CHECK(!join_ticker_on(0xFFFFF000u, 0x00008000u));
}

// Where the scoreboard page runs the ticker. A message ("Can't reach ESPN",
// "Getting ready", "Looking for a live game", a finals header) hides it, but
// not while the address line after a join leads it, or the address would
// time out unseen on exactly the screens where an owner needs it.
static void test_ticker_host() {
  using esphome::gameday::ticker_host;
  using esphome::gameday::TickerHost;
  // ticker_host(no_game_screen, card, board, join_line)
  // A game: under the score, address line or not.
  CHECK(ticker_host(false, false, false, false) == TickerHost::GAME);
  CHECK(ticker_host(false, false, false, true) == TickerHost::GAME);
  // A leaderboard: under its rows.
  CHECK(ticker_host(false, false, true, false) == TickerHost::BOARD);
  CHECK(ticker_host(false, false, true, true) == TickerHost::BOARD);
  // The off-season clock or a leaderboard's card: under it.
  CHECK(ticker_host(true, true, false, false) == TickerHost::NO_GAME);
  CHECK(ticker_host(true, true, false, true) == TickerHost::NO_GAME);
  // A message: stays hidden with the game layout as before...
  CHECK(ticker_host(true, false, false, false) == TickerHost::GAME);
  // ...and runs under the message while the address line leads it.
  CHECK(ticker_host(true, false, false, true) == TickerHost::NO_GAME);
}

// Who answers on the panel's setup hotspot (hotspot.h). While the portal is
// up and the panel is off Wi-Fi, every OS's check for internet and every
// page gets the setup page, the panel's own /gameday/ routes still answer,
// and POSTs go where they always did. On Wi-Fi, or with no hotspot,
// nothing changes, so the device page stays where it is.
static void test_hotspot_route() {
  using esphome::gameday::hotspot_route;
  using esphome::gameday::HotspotRoute;
  using esphome::gameday::panel_route;
  // Android, Samsung and other Android makers, iPhone and Mac (old and new),
  // Windows (old and new), Firefox, Linux, and the address typed by hand or
  // handed out by the hotspot's DHCP (/)
  const char *const probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                                "/connecttest.txt", "/ncsi.txt", "/redirect", "/canonical.html", "/success.txt",
                                "/check_network_status.txt", "/", "/config.json", "/wifisave"};
  for (const char *p : probes) {
    CHECK(hotspot_route(true, false, true, p) == HotspotRoute::SETUP_PAGE);
    // On Wi-Fi the hotspot is going: the device page and the rest as usual
    CHECK(hotspot_route(true, true, true, p) == HotspotRoute::USUAL);
    // No hotspot
    CHECK(hotspot_route(false, false, true, p) == HotspotRoute::USUAL);
  }
  // The device page's own files are the setup page too while off Wi-Fi
  CHECK(hotspot_route(true, false, true, "/0.js") == HotspotRoute::SETUP_PAGE);
  // The panel's own routes, for a bench script on the hotspot
  CHECK(hotspot_route(true, false, true, "/gameday/state") == HotspotRoute::PANEL);
  CHECK(hotspot_route(true, false, true, "/gameday/crash") == HotspotRoute::PANEL);
  CHECK(hotspot_route(true, true, true, "/gameday/state") == HotspotRoute::USUAL);
  // Not a GET: a setting, an update upload
  CHECK(hotspot_route(true, false, false, "/gameday/set") == HotspotRoute::USUAL);
  CHECK(hotspot_route(true, false, false, "/update") == HotspotRoute::USUAL);
  CHECK(hotspot_route(true, false, false, "/wifisave") == HotspotRoute::USUAL);

  CHECK(panel_route("/gameday/state"));
  CHECK(!panel_route("/gameday"));
  CHECK(!panel_route("/gamedays/x"));
  CHECK(!panel_route(""));
}

// Where a portal's redirect points. A relative link stays on whoever
// answered, even when its query carries ESPN's address back, and a link
// that starts with "//" names another host.
static void test_captive_redirect_host() {
  CHECK_EQ(::espn::detail::url_host("/login?next=http://site.api.espn.com/x"), std::string(""));
  CHECK_EQ(::espn::detail::url_host("login.php?u=https://espn.com"), std::string(""));
  CHECK_EQ(::espn::detail::url_host("#a://espn.com"), std::string(""));
  CHECK_EQ(::espn::detail::url_host("//Portal.Example:8080/login"), std::string("portal.example"));
  CHECK_EQ(::espn::detail::url_host("http://portal.example/x?r=https://espn.com"), std::string("portal.example"));
  CHECK(classify_probe(302, "/login?next=http://site.api.espn.com/apis/x", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "/portal?u=http://espn.com", "text/html", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "//portal.example/login?mac=aa:bb", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(301, "//www.espn.com/x", "", "") == Probe::OPEN);
}

static struct tm mk(int year, int yday, int wday, int hour, int min, int mon = 8, int mday = 5) {
  struct tm t{};
  t.tm_year = year - 1900;
  t.tm_yday = yday;
  t.tm_wday = wday;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_mon = mon;
  t.tm_mday = mday;
  return t;
}

static void test_kickoff_label() {
  struct tm now = mk(2026, 247, 6, 10, 0);  // Sat Sep 5 10:00
  CHECK_EQ(kickoff_label(mk(2026, 247, 6, 14, 30), now), std::string("Today 2:30 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 248, 0, 12, 0), now), std::string("Tomorrow 12:00 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 251, 3, 19, 20), now), std::string("Wed 7:20 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 256, 1, 0, 5, 8, 14), now), std::string("Sep 14 12:05 AM"));
  // ESPTime{}.to_c_tm(), what the panel gets for an epoch it can't convert:
  // no label rather than a read before the month table (found by fuzz_espn).
  CHECK_EQ(kickoff_label(mk(1900, -1, -1, 0, 0, -1, 0), now), std::string(""));
  // A year far enough out to overflow the day count in int.
  CHECK_EQ(kickoff_label(mk(2147481747, 0, 4, 19, 30, 0, 1), now), std::string("Jan 1 7:30 PM"));
  // Across New Year: Thu Dec 31 2026 (day 364 of a 365-day year) to the bowl
  // games on Fri Jan 1 2027 and the days after.
  struct tm eve = mk(2026, 364, 4, 20, 0, 11, 31);
  CHECK_EQ(kickoff_label(mk(2027, 0, 5, 12, 0, 0, 1), eve), std::string("Tomorrow 12:00 PM"));
  CHECK_EQ(kickoff_label(mk(2027, 5, 3, 19, 0, 0, 6), eve), std::string("Wed 7:00 PM"));
  CHECK_EQ(kickoff_label(mk(2027, 6, 4, 19, 0, 0, 7), eve), std::string("Jan 7 7:00 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 364, 4, 21, 0, 11, 31), eve), std::string("Today 9:00 PM"));
  // and back: a kickoff on Dec 31 seen on Jan 1 is a past date
  CHECK_EQ(kickoff_label(mk(2026, 364, 4, 21, 0, 11, 31), mk(2027, 0, 5, 1, 0, 0, 1)),
           std::string("Dec 31 9:00 PM"));
  // A leap year's Dec 31 is day 365
  CHECK_EQ(kickoff_label(mk(2029, 0, 1, 12, 0, 0, 1), mk(2028, 365, 0, 20, 0, 11, 31)),
           std::string("Tomorrow 12:00 PM"));
}

static void test_upcoming() {
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_nfl_dal.json"), 6, 3, up));
  CHECK_EQ(up.size(), (size_t) 3);
  if (up.size() == 3) {
    CHECK_EQ(up[0].event_id, std::string("401872930"));
    CHECK_EQ(up[0].opp_abbr, std::string("NYG"));
    CHECK_EQ(up[0].opp_id, (uint32_t) 19);
    CHECK(!up[0].home);
    CHECK_EQ(up[0].tv, std::string("NBC"));
    CHECK_EQ(up[0].kickoff_epoch, parse_iso8601_z("2026-09-14T00:20Z"));
    CHECK_EQ(up[1].opp_abbr, std::string("WSH"));
    CHECK(up[1].home);
    CHECK_EQ(up[2].opp_abbr, std::string("BAL"));
    CHECK(up[2].neutral);
  }
  // College: the first event is already final and must be skipped
  up.clear();
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_ncaa_bc.json"), 103, 4, up));
  CHECK_EQ(up.size(), (size_t) 4);
  if (up.size() >= 3) {
    CHECK_EQ(up[0].event_id, std::string("401858214"));
    CHECK_EQ(up[0].opp_abbr, std::string("RUTG"));
    CHECK(up[0].home);
    CHECK_EQ(up[0].tv, std::string("ESPN2"));
    CHECK_EQ(up[2].tv, std::string(""));  // no broadcast listed yet
    CHECK_EQ((int) up[0].opp_rank, 0);  // left out unless asked for
  }
  // With ranks (a college league's read): #17 SMU, and 99 is unranked.
  up.clear();
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_ncaa_bc.json"), 103, 4, up, true));
  CHECK_EQ(up.size(), (size_t) 4);
  if (up.size() == 4) {
    CHECK_EQ(up[0].opp_abbr, std::string("RUTG"));
    CHECK_EQ((int) up[0].opp_rank, 0);
    CHECK_EQ(up[3].event_id, std::string("401858246"));
    CHECK_EQ(up[3].opp_abbr, std::string("SMU"));
    CHECK_EQ((int) up[3].opp_rank, 17);
  }
  CHECK_EQ(schedule_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6/schedule"));
}

static void test_live_games() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  std::vector<LiveGame> games;
  CHECK(parse_live_games(json, games));
  CHECK(games.size() >= 1);
  bool found = false;
  for (const auto &g : games) {
    if (g.event_id == kLiveEvent) {
      found = true;
      CHECK_EQ(g.away_abbr, std::string("BOIS"));
      CHECK_EQ(g.home_abbr, std::string("ORE"));
      CHECK_EQ(g.away_id, kBoise);
      CHECK_EQ(g.home_id, kOregon);
      CHECK(g.group != 0);
    }
  }
  CHECK(found);
  std::vector<LiveGame> none;
  std::string nfl = slurp("fixtures/scoreboard_nfl.json");
  CHECK(parse_live_games(nfl, none));
  CHECK_EQ(none.size(), (size_t) 0);
  // The earliest kickoff still to come, for the wait when nothing is live.
  int64_t next = -1;
  games.clear();
  CHECK(parse_live_games(json, games, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-09-05T23:00Z"));
  none.clear();
  CHECK(parse_live_games(nfl, none, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-09-10T00:20Z"));
  std::string finals = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  CHECK(parse_live_games(finals, none, &next));
  CHECK_EQ(next, (int64_t) 0);  // every game final: nothing left to wait for
  // A TBD kickoff sits at midnight Eastern with timeValid false and must not
  // count, or it would hold the wait at its floor all day.
  std::string tbd = R"({"events":[)"
                    R"({"id":"1","date":"2026-10-24T04:00Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":false}]},)"
                    R"({"id":"2","date":"2026-10-24T19:30Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":true}]},)"
                    R"({"id":"3","date":"2026-10-24T16:00Z","status":{"type":{"state":"post"}},"competitions":[{}]}]})";
  CHECK(parse_live_games(tbd, none, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-24T19:30Z"));
  CHECK(scan_url(League::NFL, parse_iso8601_z("2026-09-14T01:00Z")).find("dates=20260913") != std::string::npos);
  CHECK(scan_url(League::NCAA, 0).find("groups=80") != std::string::npos);
  // College basketball needs its group too, or the scan sees the Top 25 only.
  CHECK_EQ(scan_url(League::MCBB, parse_iso8601_z("2026-02-14T20:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard"
                       "?groups=50&limit=300&dates=20260214"));
  CHECK(scan_url(League::NBA, 0).find("groups=") == std::string::npos);
  // neutral splashes name both sides
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true, true).text, std::string("DAL TOUCHDOWN"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 3), true, true).text, std::string("PHI FIELD GOAL"));
  CHECK_EQ(decide_splash(live(20, 21), live(20, 21, GameState::POST), true, true).text, std::string("PHI WINS!"));
}

static void test_clock_text() {
  GameSnapshot s = live(0, 0);
  s.display_clock = "0:42";
  s.period = 2;
  s.short_detail = "1:10 - 2nd";  // stale formatted text
  CHECK_EQ(clock_text(s), std::string("0:42 2nd"));
  s.period = 5;
  CHECK_EQ(clock_text(s), std::string("0:42 OT"));
  s.period = 6;
  CHECK_EQ(clock_text(s), std::string("0:42 2OT"));
  s.period = 2;
  s.display_clock = "0:00";
  s.short_detail = "Halftime";
  CHECK_EQ(clock_text(s), std::string("Halftime"));
  s.short_detail = "End of 3rd";
  CHECK_EQ(clock_text(s), std::string("End of 3rd"));
  s.display_clock = "";
  s.short_detail = "12:34 - 4th";
  CHECK_EQ(clock_text(s), std::string("12:34 4th"));

  // Halftime goes by ESPN's status name. shortDetail lags it, so a
  // "Halftime" still there once the second half has started is ignored.
  GameSnapshot h = live(14, 7);
  h.period = 2;
  h.display_clock = "0:00";
  h.short_detail = "Halftime";
  h.status_name = "STATUS_HALFTIME";
  CHECK(is_halftime(h));
  CHECK_EQ(clock_text(h), std::string("Halftime"));
  h.period = 3;  // the status name moved on first
  h.display_clock = "15:00";
  h.status_name = "STATUS_IN_PROGRESS";
  CHECK(!is_halftime(h));
  CHECK_EQ(clock_text(h), std::string("15:00 3rd"));
  h.display_clock = "14:21";
  h.status_name = "";  // no status name: the period decides
  CHECK(!is_halftime(h));
  CHECK_EQ(clock_text(h), std::string("14:21 3rd"));
  h.period = 3;
  h.status_name = "STATUS_IN_PROGRESS";
  h.display_clock = "";  // no clock yet: the quarter alone
  CHECK_EQ(clock_text(h), std::string("3rd"));
  h.period = 2;
  h.status_name = "";
  h.display_clock = "0:00";
  CHECK(is_halftime(h));  // older documents without a status name
  h.short_detail = "End of 2nd";
  h.status_name = "STATUS_HALFTIME";
  CHECK(is_halftime(h));  // the status name says so before shortDetail does
  TickerOptions all;
  h.team_record = "2-0";
  h.opp_record = "1-1";
  CHECK_EQ(status_text(h, all, ""), std::string("Halftime | DAL 2-0 | PHI 1-1"));
  h.short_detail = "Halftime";
  h.status_name = "STATUS_IN_PROGRESS";
  h.period = 3;
  h.display_clock = "15:00";
  CHECK(status_text(h, all, "").rfind("Halftime", 0) != 0);
  CHECK(ha_status_text(h, "").rfind("Halftime", 0) != 0);

  // Any break's words give way once ESPN says the clock runs again.
  GameSnapshot q = live(14, 7);
  q.period = 4;
  q.display_clock = "15:00";
  q.short_detail = "End of 3rd";
  q.status_name = "STATUS_IN_PROGRESS";
  CHECK(clock_running(q));
  CHECK_EQ(clock_text(q), std::string("15:00 4th"));
  q.display_clock = "0:00";  // a period that just ended keeps ESPN's words
  CHECK(!clock_running(q));
  CHECK_EQ(clock_text(q), std::string("End of 3rd"));
  q.status_name = "STATUS_END_PERIOD";
  q.display_clock = "15:00";
  CHECK(!clock_running(q));
  CHECK_EQ(clock_text(q), std::string("End of 3rd"));

  // The drive carries across the 1st and 3rd quarter breaks, not the others.
  GameSnapshot b = live(14, 7);
  b.down_distance = "3rd & 4 at DAL 30";
  b.status_name = "STATUS_END_PERIOD";
  b.short_detail = "End of 1st";
  b.period = 1;
  CHECK(!football_break(b));
  CHECK(status_text(b, all, "").find("3rd & 4") != std::string::npos);
  b.period = 3;
  CHECK(!football_break(b));
  b.period = 2;
  b.short_detail = "End of 2nd";
  CHECK(football_break(b));
  CHECK(status_text(b, all, "").find("3rd & 4") == std::string::npos);
  b.period = 4;
  b.short_detail = "End of 4th";
  CHECK(football_break(b));
  b.status_name = "STATUS_HALFTIME";
  b.period = 2;
  CHECK(football_break(b));
  b.status_name = "STATUS_IN_PROGRESS";
  CHECK(!football_break(b));
  b.sport = Sport::BASKETBALL;
  b.status_name = "STATUS_HALFTIME";
  CHECK(!football_break(b));  // football only

  // Hockey: an intermission's words give way once the next period's clock runs.
  GameSnapshot k = live(2, 1);
  k.sport = Sport::HOCKEY;
  k.period = 2;
  k.display_clock = "20:00";
  k.short_detail = "End of 1st";
  k.status_name = "STATUS_IN_PROGRESS";
  CHECK_EQ(clock_text(k), std::string("20:00 2nd"));
  k.status_name = "STATUS_END_PERIOD";
  k.period = 1;
  k.display_clock = "0:00";
  CHECK_EQ(clock_text(k), std::string("End of 1st"));
}

static void test_carry_football() {
  GameSnapshot none;
  GameSnapshot pre = live(0, 0, GameState::PRE);
  pre.team_timeouts = pre.opp_timeouts = -1;
  carry_football(none, pre);
  CHECK_EQ(pre.team_timeouts, 3);  // before kickoff
  CHECK_EQ(pre.opp_timeouts, 3);
  GameSnapshot q1 = live(0, 0);
  q1.period = 1;
  q1.team_timeouts = -1;  // between plays, no situation
  q1.opp_timeouts = 2;    // what ESPN sent stays
  carry_football(pre, q1);
  CHECK_EQ(q1.team_timeouts, 3);
  CHECK_EQ(q1.opp_timeouts, 2);
  GameSnapshot q2 = live(7, 0);
  q2.period = 2;
  q2.team_timeouts = 1;
  q2.opp_timeouts = 0;
  GameSnapshot q2b = q2;
  q2b.team_timeouts = q2b.opp_timeouts = -1;
  carry_football(q2, q2b);
  CHECK_EQ(q2b.team_timeouts, 1);  // the last count, the same half
  CHECK_EQ(q2b.opp_timeouts, 0);   // a real 0 carries too
  GameSnapshot ht = q2b;  // halftime
  ht.status_name = "STATUS_HALFTIME";
  ht.short_detail = "Halftime";
  carry_football(q2b, ht);
  GameSnapshot q3 = ht;
  q3.status_name = "STATUS_IN_PROGRESS";
  q3.short_detail = "14:20 - 3rd";
  q3.period = 3;
  q3.last_play = "(14:20) First play of the half.";
  q3.team_timeouts = q3.opp_timeouts = -1;
  carry_football(ht, q3);
  CHECK_EQ(q3.team_timeouts, -1);  // a play in, no count from ESPN: unknown, not a guess
  q3.team_timeouts = q3.opp_timeouts = 3;  // ESPN's
  GameSnapshot q4 = q3;
  q4.period = 4;
  q4.team_timeouts = 2;
  GameSnapshot q4b = q4;
  q4b.team_timeouts = -1;
  carry_football(q4, q4b);
  CHECK_EQ(q4b.team_timeouts, 2);
  GameSnapshot ot = q4b;
  ot.period = 5;
  ot.team_timeouts = ot.opp_timeouts = -1;
  carry_football(q4b, ot);
  CHECK_EQ(ot.team_timeouts, -1);  // overtime's allotment differs by league: unknown
  GameSnapshot fin = q4b;
  fin.state = GameState::POST;
  fin.team_timeouts = fin.opp_timeouts = -1;
  carry_football(q4b, fin);
  CHECK_EQ(fin.team_timeouts, 2);  // the final keeps the 4th quarter's count
  GameSnapshot mid = live(10, 3);  // joined mid-game after a restart
  mid.period = 2;
  mid.team_timeouts = -1;
  carry_football(none, mid);
  CHECK_EQ(mid.team_timeouts, -1);  // no guess
  GameSnapshot other = q2;
  other.event_id = "2";  // another game's count never carries
  GameSnapshot nxt = q2b;
  nxt.team_timeouts = -1;
  carry_football(other, nxt);
  CHECK_EQ(nxt.team_timeouts, -1);
  GameSnapshot nba = live(50, 48, GameState::PRE);
  nba.sport = Sport::BASKETBALL;
  nba.team_timeouts = -1;
  carry_football(none, nba);
  CHECK_EQ(nba.team_timeouts, -1);  // football only: a football pregame would get 3

  // A guess carries only within its period.
  GameSnapshot late = pre;  // a pregame read, then the next read is mid-2nd
  GameSnapshot mid2 = live(7, 7);
  mid2.period = 2;
  mid2.team_timeouts = mid2.opp_timeouts = -1;
  carry_football(late, mid2);
  CHECK_EQ(mid2.team_timeouts, -1);
  GameSnapshot g1 = q1;  // the 1st quarter's guessed 3
  CHECK(g1.to_guess & 1);
  GameSnapshot mid4 = live(14, 7);
  mid4.period = 4;
  mid4.team_timeouts = -1;
  carry_football(g1, mid4);
  CHECK_EQ(mid4.team_timeouts, -1);
  CHECK(!(q2b.to_guess & 1));  // carried from ESPN's count: not a guess

  // Cards the panel builds without ESPN's situation
  Pending pd;
  pd.league = (uint8_t) League::NFL;
  CHECK_EQ(pending_snapshot(pd).team_timeouts, 3);  // before kickoff
  CHECK_EQ(pending_snapshot(pd).to_guess, 3);       // the panel's, not ESPN's
  Final fn;
  fn.league = (uint8_t) League::NFL;
  CHECK_EQ(final_snapshot(fn).team_timeouts, -1);  // unknown: hidden, not all used
}

// ESPN's status can stay at halftime while the second half's plays come in.
static void test_halftime_resumes() {
  GameSnapshot h = live(14, 7);
  h.period = 2;
  h.display_clock = "0:00";
  h.short_detail = "Halftime";
  h.status_name = "STATUS_HALFTIME";
  h.last_play = "End of 2nd quarter.";
  h.down_distance = "1st & 10 at DAL 25";
  h.short_down_distance = "1st & 10";
  h.possession = 1;
  h.team_timeouts = 1;
  h.opp_timeouts = 0;
  GameSnapshot none;
  carry_football(none, h);
  CHECK(is_halftime(h));
  CHECK(football_break(h));
  CHECK_EQ(h.break_play, std::string("End of 2nd quarter."));
  CHECK_EQ(h.team_timeouts, 1);  // ESPN's halftime counts are the first half's: shown as sent

  GameSnapshot still = h;  // the next halftime poll, nothing new
  still.break_play.clear();
  carry_football(h, still);
  CHECK(!still.resumed);
  CHECK(is_halftime(still));
  GameSnapshot words = still;  // a new last play with no clock isn't a play
  words.last_play = "Halftime";
  carry_football(still, words);
  CHECK(!words.resumed);

  GameSnapshot kick = still;  // the second half's kickoff; the status hasn't caught up
  kick.last_play = "(15:00) J.Smith kicks 65 yards from DAL 35 to PHI End Zone. Touchback.";
  kick.down_distance = "1st & 10 at PHI 25";
  kick.short_down_distance = "1st & 10";
  kick.possession = 2;
  kick.team_timeouts = 1;  // still the first half's
  kick.opp_timeouts = 0;
  carry_football(still, kick);
  CHECK(kick.resumed);
  CHECK(!is_halftime(kick));
  CHECK(!football_break(kick));
  CHECK_EQ(clock_text(kick), std::string("15:00 3rd"));
  TickerOptions all;
  const std::string t = status_text(kick, all, "");
  CHECK(t.find("1st & 10 at PHI 25") != std::string::npos);  // the downs stay
  CHECK(t.find("15:00 3rd") != std::string::npos);
  CHECK(t.find("Halftime") == std::string::npos);
  CHECK(ha_status_text(kick, "").rfind("Live", 0) == 0);
  CHECK_EQ(kick.team_timeouts, 1);  // a new play: ESPN's counts from here on

  GameSnapshot next = kick;  // still stuck at halftime a poll later: stays resumed
  next.last_play = "(14:21) A.Back rush for 4 yards.";
  next.resumed = false;
  carry_football(kick, next);
  CHECK(next.resumed);
  CHECK_EQ(clock_text(next), std::string("14:21 3rd"));
  GameSnapshot caught = next;  // ESPN's status catches up
  caught.status_name = "STATUS_IN_PROGRESS";
  caught.short_detail = "13:58 - 3rd";
  caught.display_clock = "13:58";
  caught.period = 3;
  caught.resumed = false;
  carry_football(next, caught);
  CHECK(!caught.resumed);
  CHECK_EQ(clock_text(caught), std::string("13:58 3rd"));

  // The second half before its first play: ESPN still sends the first half's counts.
  GameSnapshot q3 = h;
  q3.status_name = "STATUS_IN_PROGRESS";
  q3.short_detail = "15:00 - 3rd";
  q3.display_clock = "15:00";
  q3.period = 3;
  q3.team_timeouts = 1;
  q3.opp_timeouts = 0;
  q3.break_play.clear();
  carry_football(h, q3);
  CHECK_EQ(q3.team_timeouts, 3);
  CHECK_EQ(q3.opp_timeouts, 3);
  CHECK(q3.to_guess == 3);
  GameSnapshot q3b = q3;  // the first play: ESPN's counts again
  q3b.last_play = "(15:00) Kickoff return for 22 yards.";
  q3b.team_timeouts = 3;
  q3b.opp_timeouts = 2;
  carry_football(q3, q3b);
  CHECK_EQ(q3b.opp_timeouts, 2);
  CHECK(q3b.to_guess == 0);

  CHECK_EQ(play_clock("(00:14) Shotgun pass"), std::string("0:14"));
  CHECK_EQ(play_clock("(9:05) Run"), std::string("9:05"));
  CHECK_EQ(play_clock("(00:00) End"), std::string(""));
  CHECK_EQ(play_clock("End of 2nd quarter."), std::string(""));
  CHECK_EQ(play_clock("(1a:00) x"), std::string(""));

  // "Halftime" left in shortDetail while the status says the 2nd is still on.
  GameSnapshot lag = live(7, 7);
  lag.period = 2;
  lag.short_detail = "Halftime";
  lag.status_name = "STATUS_IN_PROGRESS";
  lag.display_clock = "0:42";
  CHECK(!is_halftime(lag));
  CHECK_EQ(clock_text(lag), std::string("0:42 2nd"));
  // The status says halftime before shortDetail does.
  GameSnapshot early = lag;
  early.status_name = "STATUS_HALFTIME";
  early.short_detail = "End of 2nd";
  early.display_clock = "0:00";
  CHECK_EQ(clock_text(early), std::string("Halftime"));
  // Soccer's Home Assistant line keeps "Live" at half time, as before.
  GameSnapshot soc = live(1, 0);
  soc.sport = Sport::SOCCER;
  soc.status_name = "STATUS_HALFTIME";
  soc.short_detail = "HT";
  CHECK(ha_status_text(soc, "").rfind("Live", 0) == 0);
}

static void test_break_timelines() {
  GameSnapshot none;
  auto at = [](int period, const char *name, const char *detail, const char *clock, const char *play) {
    GameSnapshot g = live(14, 7);
    g.period = period;
    g.status_name = name;
    g.short_detail = detail;
    g.display_clock = clock;
    g.last_play = play;
    g.short_down_distance = "2nd & 6";
    g.down_distance = "2nd & 6 at PHI 40";
    g.possession = 1;
    return g;
  };
  // The 4th quarter's end keeps ESPN's counts, and the final keeps them.
  GameSnapshot q4 = at(4, "STATUS_IN_PROGRESS", "0:31 - 4th", "0:31", "(00:31) Run for 2 yards.");
  q4.team_timeouts = 0;
  q4.opp_timeouts = 1;
  carry_football(none, q4);
  GameSnapshot e4 = at(4, "STATUS_END_PERIOD", "End of 4th", "0:00", "End of 4th quarter.");
  e4.team_timeouts = 0;
  e4.opp_timeouts = 1;
  carry_football(q4, e4);
  CHECK(football_break(e4));
  CHECK_EQ(e4.team_timeouts, 0);
  CHECK_EQ(e4.opp_timeouts, 1);
  GameSnapshot fin = e4;
  fin.state = GameState::POST;
  fin.team_timeouts = fin.opp_timeouts = -1;
  carry_football(e4, fin);
  CHECK_EQ(fin.team_timeouts, 0);
  CHECK_EQ(fin.opp_timeouts, 1);

  // A first-half play that reaches ESPN's plays after halftime began isn't the 3rd quarter.
  GameSnapshot h1 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(00:14) Pass incomplete.");
  carry_football(none, h1);
  CHECK(h1.held);
  CHECK_EQ(h1.break_secs, 14);
  GameSnapshot h2 = h1;
  h2.last_play = "(00:09) Field goal is GOOD.";
  h2.team_score = 17;
  carry_football(h1, h2);
  CHECK(!h2.resumed);
  CHECK(is_halftime(h2));
  CHECK_EQ(clock_text(h2), std::string("Halftime"));
  CHECK_EQ(h2.break_secs, 9);
  GameSnapshot h3 = h2;  // ESPN changes its words during the break
  h3.last_play = "End of Half";
  carry_football(h2, h3);
  CHECK(!h3.resumed);
  CHECK(h3.held);
  // ESPN's status moves to the 3rd before the half's first play: drive and counts held.
  GameSnapshot s3 = at(3, "STATUS_IN_PROGRESS", "15:00 - 3rd", "15:00", "End of Half");
  s3.team_timeouts = 1;
  s3.opp_timeouts = 0;
  carry_football(h3, s3);
  CHECK(s3.held);
  CHECK(football_break(s3));  // the first half's drive stays hidden
  TickerOptions all;
  CHECK(status_text(s3, all, "").find("2nd & 6") == std::string::npos);
  CHECK_EQ(s3.team_timeouts, 3);
  CHECK_EQ(s3.opp_timeouts, 3);
  GameSnapshot s3b = s3;  // the kickoff
  s3b.last_play = "(15:00) Kickoff for a touchback.";
  s3b.team_timeouts = 3;
  s3b.opp_timeouts = 3;
  carry_football(s3, s3b);
  CHECK(!s3b.held);
  CHECK(!football_break(s3b));

  // ESPN's seconds left tells the halves apart, plays without a clock included.
  GameSnapshot c1 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "End of 2nd quarter.");
  c1.play_secs_left = 1800;
  carry_football(none, c1);
  CHECK(!c1.resumed);  // the half's end
  GameSnapshot c2 = c1;  // a late first-half play without a clock
  c2.last_play = "Shotgun #2 QB pass complete to #5 for 9 yds";
  c2.play_secs_left = 1806;
  carry_football(c1, c2);
  CHECK(!c2.resumed);
  CHECK_EQ(clock_text(c2), std::string("Halftime"));
  GameSnapshot c3 = c2;  // the 3rd quarter's kickoff
  c3.last_play = "(15:00) J.Smith kickoff for 65 yds for a touchback.";
  c3.play_secs_left = 1800;
  carry_football(c2, c3);
  CHECK(c3.resumed);
  CHECK_EQ(clock_text(c3), std::string("15:00 3rd"));
  GameSnapshot c4 = c2;  // a second-half play without a clock
  c4.last_play = "#21 T.Lester rush middle for 2 yds";
  c4.play_secs_left = 1756;
  carry_football(c2, c4);
  CHECK(c4.resumed);
  CHECK_EQ(clock_text(c4), std::string("3rd"));
  // The panel's first look at a game whose status is stuck at halftime
  GameSnapshot cold = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(13:40) Run for 4 yards.");
  cold.play_secs_left = 1720;
  carry_football(none, cold);
  CHECK(cold.resumed);
  CHECK(!football_break(cold));
  GameSnapshot cold2 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(13:40) Run for 4 yards.");
  carry_football(none, cold2);  // no seconds left sent: a 10:00 or later clock still tells
  CHECK(cold2.resumed);
  GameSnapshot cold3 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(00:21) Run for 4 yards.");
  carry_football(none, cold3);
  CHECK(!cold3.resumed);  // a late first-half play

  // ESPN's plays frozen at the half's end into the 4th: the counts stay the guessed 3.
  GameSnapshot f3 = s3;
  GameSnapshot f4 = at(4, "STATUS_IN_PROGRESS", "14:00 - 4th", "14:00", "End of Half");
  f4.team_timeouts = 1;
  f4.opp_timeouts = 0;
  carry_football(f3, f4);
  CHECK(f4.held);
  CHECK_EQ(f4.team_timeouts, 3);
  CHECK_EQ(f4.opp_timeouts, 3);

  // A gap from mid-2nd to mid-3rd (no halftime seen) doesn't guess 3.
  GameSnapshot m2 = at(2, "STATUS_IN_PROGRESS", "5:00 - 2nd", "5:00", "(05:00) Run.");
  m2.team_timeouts = 1;
  carry_football(none, m2);
  GameSnapshot m3 = at(3, "STATUS_IN_PROGRESS", "9:00 - 3rd", "9:00", "(09:00) Pass.");
  m3.team_timeouts = -1;
  carry_football(m2, m3);
  CHECK_EQ(m3.team_timeouts, -1);
}

// The fourth review's cases
static void test_break_edges() {
  GameSnapshot none;
  auto at = [](int period, const char *name, const char *detail, const char *clock, const char *play) {
    GameSnapshot g = live(14, 7);
    g.period = period;
    g.status_name = name;
    g.short_detail = detail;
    g.display_clock = clock;
    g.last_play = play;
    g.short_down_distance = "2nd & 6";
    g.down_distance = "2nd & 6 at PHI 40";
    g.possession = 1;
    return g;
  };
  // Time put back after "End of 4th": play goes on with ESPN's counts.
  GameSnapshot e4 = at(4, "STATUS_IN_PROGRESS", "End of 4th", "0:00", "(00:05) Pass incomplete.");
  e4.team_timeouts = 0;
  e4.opp_timeouts = 1;
  carry_football(none, e4);
  CHECK(e4.held);
  CHECK_EQ(e4.break_period, 4);
  GameSnapshot back = at(4, "STATUS_IN_PROGRESS", "0:01 - 4th", "0:01", "(00:05) Pass incomplete.");
  back.team_timeouts = 0;
  back.opp_timeouts = 1;
  carry_football(e4, back);
  CHECK(!back.held);
  CHECK(!football_break(back));
  CHECK_EQ(back.team_timeouts, 0);
  CHECK_EQ(back.opp_timeouts, 1);

  // After the status reaches the 3rd, ESPN's words or an empty play don't end the hold.
  GameSnapshot h = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "End of 2nd quarter.");
  h.play_secs_left = 1800;
  carry_football(none, h);
  GameSnapshot s3 = at(3, "STATUS_IN_PROGRESS", "15:00 - 3rd", "15:00", "End of 2nd quarter.");
  s3.play_secs_left = 1800;
  s3.team_timeouts = 1;
  s3.opp_timeouts = 0;
  carry_football(h, s3);
  CHECK(s3.held);
  GameSnapshot blank = s3;  // the situation drops out for a poll
  blank.last_play = "";
  blank.play_secs_left = -1;
  blank.team_timeouts = blank.opp_timeouts = -1;
  carry_football(s3, blank);
  CHECK(blank.held);
  CHECK_EQ(blank.team_timeouts, 3);
  GameSnapshot again = s3;  // the old words come back with the first half's counts
  carry_football(blank, again);
  CHECK(again.held);
  CHECK_EQ(again.team_timeouts, 3);
  CHECK_EQ(again.opp_timeouts, 3);
  TickerOptions all;
  CHECK(status_text(again, all, "").find("2nd & 6") == std::string::npos);
  GameSnapshot first = again;  // the half's first play
  first.last_play = "Kickoff for 65 yards, touchback.";
  first.play_secs_left = 1800;
  first.team_timeouts = first.opp_timeouts = 3;
  carry_football(again, first);
  CHECK(!first.held);

  // A first look at a 3rd quarter whose plays are still the first half's (recorded)
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot ark;
  CHECK(parse_scoreboard_str(json, "401856635", 8, ark));
  CHECK_EQ(ark.play_secs_left, 1800);
  carry_football(none, ark);
  CHECK(ark.held);
  CHECK(football_break(ark));  // the first half's "2nd & 10" stays hidden
  CHECK_EQ(ark.team_timeouts, 3);  // over the first half's 1 and 0
  CHECK_EQ(ark.opp_timeouts, 3);

  // A stuck halftime into the 4th quarter reads "4th" from ESPN's seconds left.
  GameSnapshot h4 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(08:12) Run for 3 yards.");
  h4.play_secs_left = 492;
  carry_football(none, h4);
  CHECK(h4.resumed);
  CHECK_EQ(clock_text(h4), std::string("8:12 4th"));

  // A kickoff without a clock at 1800 seconds left resumes a stuck halftime.
  GameSnapshot k1 = h;
  GameSnapshot k2 = h;
  k2.last_play = "J.Smith kickoff for 65 yds for a touchback.";
  k2.play_secs_left = 1800;
  carry_football(k1, k2);
  CHECK(k2.resumed);

  // A first-half kickoff returned as the half ran out also has 1800 left.
  GameSnapshot r1 = h;
  GameSnapshot r2 = h;
  r2.last_play = "(00:05) #44 G.Spetic kickoff 65 yards to the PHI00, #3 A.Smith return 28 yards to the PHI28";
  r2.play_secs_left = 1800;
  carry_football(r1, r2);
  CHECK(!r2.resumed);
  CHECK(is_halftime(r2));
  GameSnapshot r3 = at(2, "STATUS_HALFTIME", "Halftime", "0:00", "(00:05) J.Bates kicks 65 yards, returned to the 30");
  r3.play_secs_left = 1800;
  carry_football(none, r3);  // a first look too
  CHECK(!r3.resumed);
  // A first look at the 3rd quarter's kickoff touchback isn't a held break.
  GameSnapshot t1 = at(3, "STATUS_IN_PROGRESS", "15:00 - 3rd", "15:00",
                       "(15:00) J.Bates kicks 65 yards from DET 35 to end zone, Touchback.");
  t1.play_secs_left = 1800;
  t1.team_timeouts = t1.opp_timeouts = 3;
  carry_football(none, t1);
  CHECK(!t1.held);
  CHECK(!football_break(t1));
  GameSnapshot t2 = t1;  // nor on the poll after
  carry_football(t1, t2);
  CHECK(!t2.held);
  GameSnapshot c1 = at(3, "STATUS_IN_PROGRESS", "15:00 - 3rd", "15:00", "#44 G.Spetic kickoff 65 yards to the PHI00, Touchback");
  c1.play_secs_left = 1800;
  carry_football(none, c1);
  CHECK(!c1.held);
  // A resumed halftime whose plays go back to the first half's says Halftime again.
  GameSnapshot u1 = h4;
  GameSnapshot u2 = h;
  carry_football(u1, u2);
  CHECK(!u2.resumed);

  // An overtime before its first play hides the 4th quarter's counts.
  GameSnapshot ot = at(5, "STATUS_IN_PROGRESS", "10:00 - OT", "10:00", "(00:05) Pass incomplete.");
  ot.team_timeouts = 0;
  ot.opp_timeouts = 1;
  GameSnapshot e4b = e4;
  carry_football(e4, e4b);
  carry_football(e4b, ot);
  CHECK(ot.held);
  CHECK_EQ(ot.team_timeouts, -1);
  GameSnapshot ot1 = ot;  // its first play
  ot1.last_play = "(10:00) Kickoff, touchback.";
  ot1.team_timeouts = 2;
  ot1.opp_timeouts = 2;
  carry_football(ot, ot1);
  CHECK(!ot1.held);
  CHECK_EQ(ot1.team_timeouts, 2);
}

// ESPN also ends a half as in progress: "End of 2nd" at 0:00.
static void test_end_of_half_in_progress() {
  std::string json = slurp("fixtures/day_ncaa_20261017.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, "401856720", 99, s));
  CHECK_EQ(s.status_name, std::string("STATUS_IN_PROGRESS"));
  CHECK_EQ(s.short_detail, std::string("End of 2nd"));
  CHECK_EQ(s.play_secs_left, 1800);
  CHECK(football_break(s));
  TickerOptions all;
  CHECK(status_text(s, all, "").find("2nd & 15") == std::string::npos);
}

// ESPN flags the red zone on extra-point tries and after scores too.
static void test_red_zone() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot duke;
  CHECK(parse_scoreboard_str(json, "401858209", 150, duke));
  CHECK(duke.is_red_zone);
  CHECK(in_red_zone(duke));  // 3rd & Goal with the ball
  TickerOptions all;
  CHECK(status_text(duke, all, "").find("RED ZONE | 3rd & Goal") != std::string::npos);
  TickerOptions no_down;
  no_down.down_distance = false;
  CHECK(status_text(duke, no_down, "").find("RED ZONE") == std::string::npos);
  GameSnapshot tenn, app;
  CHECK(parse_scoreboard_str(json, "401856666", 2633, tenn));
  CHECK(parse_scoreboard_str(json, "401866627", 2026, app));
  CHECK(tenn.is_red_zone);
  CHECK(!in_red_zone(tenn));  // no down, no possession: a try or a kickoff
  CHECK(!in_red_zone(app));
  CHECK(status_text(tenn, all, "").find("RED ZONE") == std::string::npos);
  GameSnapshot half = duke;
  half.status_name = "STATUS_HALFTIME";
  CHECK(!in_red_zone(half));  // ESPN keeps it through halftime
  GameSnapshot pre = duke;
  pre.state = GameState::PRE;
  CHECK(!in_red_zone(pre));
}

// The recorded STATUS_HALFTIME game keeps the punt's "1st & 10" in situation.
static void test_halftime_fixture() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, "401862694", 2429, s));
  CHECK_EQ(s.status_name, std::string("STATUS_HALFTIME"));
  CHECK(is_halftime(s));
  CHECK(football_break(s));
  CHECK_EQ(s.short_down_distance, std::string("1st & 10"));  // ESPN's, hidden at the break
  CHECK_EQ(clock_text(s), std::string("Halftime"));
}

static void test_color() {
  CHECK_EQ(parse_color("002a5c"), (uint32_t) 0x002a5c);
  CHECK_EQ(parse_color(""), (uint32_t) 0xFFFFFF);
  CHECK_EQ(parse_color("zzzzzz"), (uint32_t) 0xFFFFFF);
}

// ---- favorites mode: lock-on, release, collisions, playlist -------------------
static FavGame fav(GameState st, int64_t kick, int64_t final_at = 0) {
  FavGame g;
  g.valid = true;
  g.state = st;
  g.kickoff_epoch = kick;
  g.final_epoch = final_at;
  return g;
}

static void test_favorites() {
  FavRules r;
  r.lockon_s = 15 * 60;
  r.release_s = 30;
  r.alternate = false;
  r.rotate_s = 5 * 60;
  r.dwell_s = 10;
  const int64_t T = 1000000;

  // lock window: only within lockon_s before kickoff, while IN, or release_s after the final
  CHECK(!fav_locked(fav(GameState::PRE, T + 16 * 60), T, r));
  CHECK(fav_locked(fav(GameState::PRE, T + 15 * 60), T, r));
  CHECK(fav_locked(fav(GameState::PRE, T - 60), T, r));  // ESPN lag: still PRE past kickoff
  CHECK(fav_locked(fav(GameState::IN, T - 3600), T, r));
  CHECK(fav_locked(fav(GameState::POST, T - 3 * 3600, T - 29), T, r));
  CHECK(!fav_locked(fav(GameState::POST, T - 3 * 3600, T - 30), T, r));
  CHECK(!fav_locked(fav(GameState::POST, T - 3 * 3600, 0), T, r));  // already final when fetched
  CHECK(!fav_locked(FavGame{}, T, r));

  // playlist: nothing locked, cycle valid entries in slot order every dwell_s
  std::vector<FavGame> idle = {fav(GameState::PRE, T + 86400), FavGame{}, fav(GameState::PRE, T + 2 * 86400),
                               fav(GameState::PRE, T + 3 * 86400)};
  FavChoice c = pick_favorite(idle, T, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  CHECK(!c.locked);
  c = pick_favorite(idle, T + 9, r, 0, T, -1);
  CHECK_EQ(c.index, 0);  // dwell not over
  c = pick_favorite(idle, T + 10, r, 0, T, -1);
  CHECK_EQ(c.index, 2);  // skips the empty slot
  c = pick_favorite(idle, T + 30, r, 3, T + 20, -1);
  CHECK_EQ(c.index, 0);  // wraps
  // a current entry that became invalid moves on at once
  c = pick_favorite(idle, T + 1, r, 1, T, -1);
  CHECK_EQ(c.index, 2);
  // nothing valid at all
  std::vector<FavGame> none = {FavGame{}, FavGame{}, FavGame{}, FavGame{}};
  c = pick_favorite(none, T, r, -1, 0, -1);
  CHECK_EQ(c.index, -1);

  // one locked: hold it regardless of dwell
  std::vector<FavGame> one = {fav(GameState::PRE, T + 86400), fav(GameState::IN, T - 600), fav(GameState::PRE, T + 86400),
                              FavGame{}};
  c = pick_favorite(one, T + 100, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
  c = pick_favorite(one, T + 5000, r, 1, T + 100, -1);
  CHECK_EQ(c.index, 1);

  // collision, stick: the higher slot wins even when the lower one is shown
  std::vector<FavGame> two = {fav(GameState::IN, T - 600), fav(GameState::IN, T - 600), fav(GameState::PRE, T + 86400),
                              FavGame{}};
  c = pick_favorite(two, T + 5000, r, 1, T, -1);
  CHECK_EQ(c.index, 0);
  CHECK(c.locked);
  // collision, alternate: hold for rotate_s, then the next locked one, wrapping
  r.alternate = true;
  c = pick_favorite(two, T + 299, r, 0, T, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(two, T + 300, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  c = pick_favorite(two, T + 600, r, 1, T + 300, -1);
  CHECK_EQ(c.index, 0);
  // every 30 seconds, the shortest Alternate every
  r.rotate_s = espn::rotate_saved_seconds(1, true);
  c = pick_favorite(two, T + 29, r, 0, T, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(two, T + 30, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  c = pick_favorite(two, T + 60, r, 1, T + 30, -1);
  CHECK_EQ(c.index, 0);
  // a pinned locked game holds past 30 seconds too
  c = pick_favorite(two, T + 300, r, 1, T, 1);
  CHECK_EQ(c.index, 1);
  r.rotate_s = 5 * 60;
  // alternate with the shown entry not locked jumps to a locked one at once
  c = pick_favorite(two, T + 1, r, 2, T, -1);
  CHECK_EQ(c.index, 0);
  r.alternate = false;

  // pin: a pinned locked entry beats priority; a pinned idle entry only seeds the playlist
  c = pick_favorite(two, T + 5000, r, 0, T, 1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
  c = pick_favorite(idle, T + 5, r, 2, T, 2);
  CHECK_EQ(c.index, 2);
  CHECK(!c.locked);
  c = pick_favorite(idle, T + 10, r, 2, T, 2);
  CHECK_EQ(c.index, 3);  // dwell over, playlist continues past the pin
  // a pin on an empty slot is ignored
  c = pick_favorite(two, T + 5000, r, 0, T, 3);
  CHECK_EQ(c.index, 0);
}

// The list is only the favorites set, so it can get shorter while a later
// slot is up or pinned. A slot past the end is skipped like an empty one.
static void test_favorites_shrunk() {
  FavRules r;
  const int64_t T = 1000000;
  std::vector<FavGame> idle = {fav(GameState::PRE, T + 86400), fav(GameState::PRE, T + 2 * 86400)};
  FavChoice c = pick_favorite(idle, T, r, 5, T, 5);
  CHECK_EQ(c.index, 0);
  CHECK(!c.locked);
  c = pick_favorite(idle, T, r, 1, T - 60, 7);
  CHECK_EQ(c.index, 0);
  std::vector<FavGame> live = {fav(GameState::PRE, T + 86400), fav(GameState::IN, T - 600)};
  c = pick_favorite(live, T, r, 6, T, 9);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
  r.alternate = true;
  c = pick_favorite(live, T, r, 6, T, -1);
  CHECK_EQ(c.index, 1);
  c = pick_favorite({}, T, r, 3, T, 3);
  CHECK_EQ(c.index, -1);
}

// A schedule parsed from the team endpoint does not know its own league, so
// the caller has to stamp it. Without the stamp a college favorite polls the
// NFL scoreboard and never finds its game.
// The favorite Home Assistant follows while the idle playlist rotates.
static void test_followed_favorite() {
  const int64_t T = 1000000;
  CHECK_EQ(followed_favorite({}), -1);
  CHECK_EQ(followed_favorite({FavGame{}, FavGame{}}), -1);
  // The next kickoff, whatever its slot
  CHECK_EQ(followed_favorite({fav(GameState::PRE, T + 3 * 86400), FavGame{}, fav(GameState::PRE, T + 86400),
                              fav(GameState::PRE, T + 2 * 86400)}),
           2);
  // A card that is already final loses to any upcoming game
  CHECK_EQ(followed_favorite({fav(GameState::POST, T - 3600), fav(GameState::PRE, T + 86400)}), 1);
  // No upcoming game: the first slot with a card
  CHECK_EQ(followed_favorite({FavGame{}, fav(GameState::POST, T - 3600), fav(GameState::POST, T - 60)}), 1);
}

// ---- Follow this game: the pin on the board's game ---------------------------------
static GameSnapshot board_game(const char *event, GameState st, Sport sport = Sport::FOOTBALL) {
  GameSnapshot g;
  g.valid = true;
  g.event_id = event;
  g.state = st;
  g.sport = sport;
  return g;
}

static void test_follow() {
  const int64_t T = 1000000;
  const int64_t H = 3600;
  // The modes that pick games themselves: Live games, the old live modes,
  // and Favorite teams with a favorite set.
  CHECK(follow_mode_ok((uint8_t) Mode::LIVE, false));
  CHECK(follow_mode_ok((uint8_t) Mode::LIVE_NFL, false));
  CHECK(follow_mode_ok((uint8_t) Mode::LIVE_NCAA, false));
  CHECK(follow_mode_ok((uint8_t) Mode::LIVE_ANY, false));
  CHECK(follow_mode_ok((uint8_t) Mode::FAVORITES, true));
  CHECK(!follow_mode_ok((uint8_t) Mode::FAVORITES, false));  // with none it runs as My team
  CHECK(!follow_mode_ok((uint8_t) Mode::MY_TEAM, true));
  CHECK(!follow_mode_ok(kModeCount, true));

  // A final stays up for the mode's own time: Live games moves on at its
  // first look after a final, as it does without the pin, and Favorite teams
  // keeps it for After the final.
  const int64_t LIVE = follow_after_final_s(false, 30);
  CHECK_EQ(LIVE, (int64_t) 0);
  CHECK_EQ(follow_after_final_s(false, 3600), (int64_t) 0);
  CHECK_EQ(follow_after_final_s(true, 30), (int64_t) 30);
  CHECK_EQ(follow_after_final_s(true, 3600), (int64_t) 3600);

  // Nothing to follow: the pin stays off
  FollowPin p;
  CHECK(!follow_start(p, false, true, board_game("1", GameState::IN), T));  // My team
  CHECK(!follow_start(p, true, false, board_game("1", GameState::IN), T));  // Live games' fallback card, the finals
  CHECK(!follow_start(p, true, true, GameSnapshot{}, T));                   // an empty board
  CHECK(!follow_start(p, true, true, board_game("", GameState::PRE), T));   // a team's card before its first read
  CHECK(!follow_start(p, true, true, board_game("1", GameState::IN, Sport::GOLF), T));
  CHECK(!follow_start(p, true, true, board_game("1", GameState::IN, Sport::RACING), T));
  CHECK(!follow_start(p, true, true, board_game("1", GameState::POST), T));  // over already: a favorite's last final
  CHECK(!p.on);

  // A live game holds for as long as it is on, whatever the switch timer says
  CHECK(follow_start(p, true, true, board_game("1", GameState::IN), T));
  CHECK(p.on);
  CHECK_EQ(p.since, T);
  CHECK_EQ(p.final_epoch, (int64_t) 0);
  CHECK(follow_holds(p, board_game("1", GameState::IN), T + 4 * H, LIVE, 5));
  // Live games: the first look at its final lets go
  CHECK(!follow_holds(p, board_game("1", GameState::POST), T + 4 * H + 60, LIVE, 5));
  CHECK(!p.on);  // let go by itself
  CHECK(p.event_id.empty());
  CHECK(!follow_holds(p, board_game("1", GameState::POST), T + 4 * H + 120, LIVE, 5));

  // Favorite teams: the final stays up for After the final, from when the panel saw it
  CHECK(follow_start(p, true, true, board_game("2", GameState::IN), T));
  CHECK(follow_holds(p, board_game("2", GameState::POST), T + 3 * H, 300, 5));
  CHECK_EQ(p.final_epoch, T + 3 * H);
  CHECK(follow_holds(p, board_game("2", GameState::POST), T + 3 * H + 299, 300, 5));
  CHECK(!follow_holds(p, board_game("2", GameState::POST), T + 3 * H + 300, 300, 5));
  CHECK(!p.on);

  // ESPN takes a final back: the wait starts over at the next one
  CHECK(follow_start(p, true, true, board_game("2", GameState::IN), T));
  CHECK(follow_holds(p, board_game("2", GameState::POST), T + 100, 30, 5));
  CHECK(follow_holds(p, board_game("2", GameState::IN), T + 200, 30, 5));
  CHECK_EQ(p.final_epoch, (int64_t) 0);
  CHECK(follow_holds(p, board_game("2", GameState::POST), T + 300, 30, 5));
  CHECK(follow_holds(p, board_game("2", GameState::POST), T + 329, 30, 5));
  CHECK(!follow_holds(p, board_game("2", GameState::POST), T + 330, 30, 5));

  // A game days off holds until it is over; another game on the board lets go at once
  GameSnapshot later = board_game("3", GameState::PRE);
  later.kickoff_epoch = T + 3 * 86400;
  CHECK(follow_start(p, true, true, later, T));
  CHECK(follow_holds(p, later, T + 3 * 86400 + 8 * H - 1, 30, 5));
  CHECK(!follow_holds(p, board_game("4", GameState::PRE), T + 86400, 30, 5));
  CHECK(!p.on);
  CHECK(follow_start(p, true, true, board_game("3", GameState::IN), T));
  CHECK(!follow_holds(p, GameSnapshot{}, T + 10, 30, 5));  // the board emptied
  CHECK(!p.on);

  // A game that never finishes. ESPN stops answering for it: half an hour
  // with no good read lets go, counted from the pin at the earliest, where
  // Live games' Switch every moved past it before.
  GameSnapshot on = board_game("4", GameState::IN);
  on.kickoff_epoch = T - H;
  CHECK(follow_start(p, true, true, on, T));
  CHECK(follow_holds(p, on, T + 600, LIVE, 25 * 60 + 600));  // the reads went bad before the pin
  CHECK(follow_holds(p, on, T + kFollowUnreadSeconds - 1, LIVE, 55 * 60 - 1));
  CHECK(!follow_holds(p, on, T + kFollowUnreadSeconds, LIVE, 55 * 60));
  CHECK(!p.on);
  CHECK(follow_start(p, true, true, on, T));
  CHECK(follow_holds(p, on, T + 2 * H, LIVE, kFollowUnreadSeconds - 1));  // a good read now and then keeps it
  CHECK(!follow_holds(p, on, T + 2 * H, LIVE, kFollowUnreadSeconds));
  // ESPN keeps it in progress: 8 hours past its start lets go
  on.kickoff_epoch = T + H;  // pinned an hour before it started
  CHECK(follow_start(p, true, true, on, T));
  CHECK(follow_holds(p, on, T + 9 * H - 1, LIVE, 5));
  CHECK(!follow_holds(p, on, T + 9 * H, LIVE, 5));
  CHECK(!p.on);
  // A game with no start, or pinned past its start, counts from the pin
  on.kickoff_epoch = 0;
  CHECK(follow_start(p, true, true, on, T));
  CHECK(follow_holds(p, on, T + 8 * H - 1, LIVE, 5));
  CHECK(!follow_holds(p, on, T + 8 * H, LIVE, 5));
  on.kickoff_epoch = T - 10 * H;
  CHECK(follow_start(p, true, true, on, T));
  CHECK(follow_holds(p, on, T + 8 * H - 1, LIVE, 5));
  CHECK(!follow_holds(p, on, T + 8 * H, LIVE, 5));
  // A game with no start time yet counts from the end of its day
  GameSnapshot tbd = board_game("5", GameState::PRE);
  tbd.kickoff_epoch = T + 86400;
  tbd.time_tbd = true;
  CHECK(follow_start(p, true, true, tbd, T));
  CHECK(follow_holds(p, tbd, T + 2 * 86400 + 8 * H - 1, 30, 5));
  CHECK(!follow_holds(p, tbd, T + 2 * 86400 + 8 * H, 30, 5));
  // A final that comes just inside the 8 hours still gets its own time
  CHECK(follow_start(p, true, true, board_game("5", GameState::IN), T));
  CHECK(follow_holds(p, board_game("5", GameState::IN), T + 8 * H - 60, 300, 5));
  CHECK(follow_holds(p, board_game("5", GameState::POST), T + 8 * H + 60, 300, 5));
  CHECK(follow_holds(p, board_game("5", GameState::POST), T + 8 * H + 359, 300, 5));
  CHECK(!follow_holds(p, board_game("5", GameState::POST), T + 8 * H + 360, 300, 5));

  // follow=1 again on the same game keeps its count, even on its final; on
  // another game it starts over
  CHECK(follow_start(p, true, true, board_game("6", GameState::IN), T));
  CHECK(follow_holds(p, board_game("6", GameState::POST), T + 10, 30, 5));
  CHECK(follow_start(p, true, true, board_game("6", GameState::POST), T + 20));
  CHECK_EQ(p.final_epoch, T + 10);
  CHECK_EQ(p.since, T);
  CHECK(follow_start(p, true, true, board_game("7", GameState::IN), T + 20));
  CHECK_EQ(p.event_id, std::string("7"));
  CHECK_EQ(p.final_epoch, (int64_t) 0);
  CHECK_EQ(p.since, T + 20);
  // A refused follow=1 leaves the pin as it was
  CHECK(!follow_start(p, false, true, board_game("8", GameState::IN), T + 20));
  CHECK(!follow_start(p, true, true, board_game("8", GameState::POST), T + 20));
  CHECK(p.on);
  CHECK_EQ(p.event_id, std::string("7"));
  // No pin, nothing held
  FollowPin off;
  CHECK(!follow_holds(off, GameSnapshot{}, T, 30, 5));
  CHECK(!follow_holds(off, board_game("7", GameState::IN), T, 30, 5));
}

static void test_schedule_league() {
  std::string json = slurp("fixtures/team_ncaa_bc.json");
  Schedule s;
  CHECK(parse_team_str(json, s));
  CHECK(s.valid);
  CHECK_EQ((int) s.league, 0);  // documents the gap: 0 is League::NFL

  adopt_league(s, League::NCAA);
  CHECK_EQ((int) s.league, (int) League::NCAA);

  std::string url = scoreboard_url((League) s.league, s.group, s.kickoff_epoch);
  CHECK(url.find("/college-football/") != std::string::npos);
  CHECK(url.find("/nfl/") == std::string::npos);
}

// Refresh Now, and the six hour schedule cycle. SCHEDULE_INTERVAL is 6 hours;
// the helper takes it as an argument so the test does not depend on the device
// constant.
static const uint32_t kSix = 6u * 60u * 60u * 1000u;

static void test_no_live_rescan() {
  const uint32_t lo = 2 * 60 * 1000, hi = 3 * 60 * 60 * 1000;
  int64_t now = parse_iso8601_z("2026-10-17T13:00Z");
  CHECK_EQ(no_live_rescan_ms(now, now + 45 * 60, lo, hi), (uint32_t) 45 * 60 * 1000);  // waits for kickoff
  CHECK_EQ(no_live_rescan_ms(now, now + 8 * 3600, lo, hi), hi);                        // capped
  CHECK_EQ(no_live_rescan_ms(now, now + 30, lo, hi), lo);                              // never under the floor
  CHECK_EQ(no_live_rescan_ms(now, now - 600, lo, hi), lo);  // due but not under way yet
  CHECK_EQ(no_live_rescan_ms(now, 0, lo, hi), hi);          // nothing left today
  CHECK_EQ(no_live_rescan_ms(0, now + 3600, lo, hi), lo);   // no clock yet
}

static void test_schedule_due() {
  // Nothing fetched yet: the panel has to read the team endpoint.
  CHECK(schedule_due(false, false, 1000, 0, kSix));

  // Fresh schedule, nothing asked for: poll the game we already know about.
  CHECK(!schedule_due(true, false, kSix, kSix - 1000, kSix));

  // Older than the cycle: read it again.
  CHECK(schedule_due(true, false, 2 * kSix, kSix - 1000, kSix));
  CHECK(schedule_due(true, false, kSix, 0, kSix));  // exactly due

  // Refresh Now forces the re-read however fresh the schedule is, and whatever
  // the uptime. The second case is the v1.3.x bug: zeroing the timestamp on a
  // still valid schedule left the test as millis() >= 6 h, so Refresh Now did
  // nothing for the first six hours after a restart.
  CHECK(schedule_due(true, true, 1000, 500, kSix));
  CHECK(schedule_due(true, true, 60u * 1000u, 0, kSix));

  // Without the force flag that same state is not due: this is what made the
  // old refresh_now() a no-op, and it stays true so the force flag is the only
  // thing carrying Refresh Now.
  CHECK(!schedule_due(true, false, 60u * 1000u, 0, kSix));

  // millis() wraps every 49 days. Unsigned subtraction still gives the real
  // age, so a wrap must not force a spurious re-read.
  uint32_t before_wrap = 0xFFFFFFFFu - 1000u;
  CHECK(!schedule_due(true, false, 2000, before_wrap, kSix));  // ~3 s old
  CHECK(schedule_due(true, false, before_wrap + 1000u + kSix, before_wrap, kSix));
}

static void test_team_read_waits() {
  // My team, a final on the board since 10:00 (millis 36,000,000) with a
  // 30 minute linger (the AHL's and the ECHL's) or 10 (minor league baseball)
  const uint32_t at_final = 36000000u, min = 60000u, linger = 30 * min;
  // ESPN's team endpoint keeps naming the final: its read never waits
  CHECK(!team_read_waits(Source::ESPN, at_final, at_final + 5 * min, linger));
  // MLB's and HockeyTech's name the next game: the six-hour read or Refresh
  // Now waits until the final has lingered its time
  CHECK(team_read_waits(Source::HOCKEYTECH, at_final, at_final, linger));
  CHECK(team_read_waits(Source::HOCKEYTECH, at_final, at_final + 29 * min, linger));
  CHECK(!team_read_waits(Source::HOCKEYTECH, at_final, at_final + linger, linger));
  CHECK(team_read_waits(Source::MLB_STATS, at_final, at_final + 9 * min, 10 * min));
  CHECK(!team_read_waits(Source::MLB_STATS, at_final, at_final + 10 * min, 10 * min));
  // No final on the board: read as usual
  CHECK(!team_read_waits(Source::HOCKEYTECH, 0, at_final, linger));
  CHECK(!team_read_waits(Source::MLB_STATS, 0, 5 * min, linger));
  // Across the millis() wrap the final's age is still a few minutes
  const uint32_t before_wrap = 0xFFFFFFFFu - min;
  CHECK(team_read_waits(Source::HOCKEYTECH, before_wrap, 4 * min, linger));
  CHECK(!team_read_waits(Source::HOCKEYTECH, before_wrap, linger, linger));
  // Refresh Now (schedule_due's force) in the linger: the read is due but waits
  CHECK(schedule_due(true, true, at_final + min, at_final - min, kSix));
  CHECK(team_read_waits(Source::HOCKEYTECH, at_final, at_final + min, linger));
}

static void test_team_cache() {
  Schedule d;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), d));
  // Saved the evening before the game.
  int64_t saved = d.kickoff_epoch - 20 * 3600;
  TeamCache c;
  CHECK(team_cache_save(d, League::NFL, 6, saved, c));

  // Next morning, same team: straight to the scoreboard.
  Schedule s;
  CHECK(team_cache_load(c, League::NFL, 6, saved + 12 * 3600, s));
  CHECK(s.valid);
  CHECK_EQ(s.event_id, std::string("401872930"));
  CHECK_EQ(s.kickoff_epoch, d.kickoff_epoch);
  CHECK_EQ(s.group, d.group);
  CHECK_EQ(s.league, (uint8_t) League::NFL);
  // The seed and place the Standing setting shows, which only the team read has
  CHECK_EQ((int) s.seed, 4);
  CHECK_EQ(s.place, std::string(""));
  Schedule placed = d;
  placed.place = "1st NFCE";
  TeamCache pc;
  CHECK(team_cache_save(placed, League::NFL, 6, saved, pc));
  CHECK(team_cache_load(pc, League::NFL, 6, saved + 60, s));
  CHECK_EQ(s.place, std::string("1st NFCE"));
  placed.place = "12th Brazilian Serie";  // past the record's 15 characters: cut
  CHECK(team_cache_save(placed, League::NFL, 6, saved, pc));
  CHECK(team_cache_load(pc, League::NFL, 6, saved + 60, s));
  CHECK_EQ(s.place, std::string("12th Brazilian "));

  // Another team, or the same id in the other league: read the team endpoint.
  CHECK(!team_cache_load(c, League::NFL, 21, saved + 60, s));
  CHECK(!team_cache_load(c, League::NCAA, 6, saved + 60, s));

  // More than a day old, or saved "in the future" (a clock that jumped back).
  CHECK(team_cache_load(c, League::NFL, 6, saved + TEAM_CACHE_MAX_AGE, s));
  CHECK(!team_cache_load(c, League::NFL, 6, saved + TEAM_CACHE_MAX_AGE + 1, s));
  CHECK(!team_cache_load(c, League::NFL, 6, saved - 1, s));

  // Kicked off more than four hours ago: that game is over.
  TeamCache late;
  CHECK(team_cache_save(d, League::NFL, 6, d.kickoff_epoch + 3600, late));
  CHECK(team_cache_load(late, League::NFL, 6, d.kickoff_epoch + TEAM_CACHE_GAME_OVER, s));
  CHECK(!team_cache_load(late, League::NFL, 6, d.kickoff_epoch + TEAM_CACHE_GAME_OVER + 1, s));

  // College keeps its conference group for the scoreboard request.
  Schedule bc;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_bc.json"), bc));
  TeamCache cc;
  CHECK(team_cache_save(bc, League::NCAA, 103, bc.kickoff_epoch - 3600, cc));
  Schedule sc;
  CHECK(team_cache_load(cc, League::NCAA, 103, bc.kickoff_epoch, sc));
  CHECK_EQ(sc.event_id, std::string("401856777"));
  CHECK_EQ(sc.group, (uint32_t) 1);
  CHECK_EQ(sc.league, (uint8_t) League::NCAA);

  // Nothing to keep: no next game, an id too long for the record, no clock.
  Schedule none;
  none.valid = true;
  TeamCache n;
  CHECK(!team_cache_save(none, League::NFL, 6, saved, n));
  Schedule longid = d;
  longid.event_id = "1234567890123456";  // 16 characters, no room for the end
  CHECK(!team_cache_save(longid, League::NFL, 6, saved, n));
  CHECK(!team_cache_save(d, League::NFL, 6, 0, n));

  // A blank record (nothing saved yet reads back as zeros) is never used.
  TeamCache blank{};
  CHECK(!team_cache_load(blank, League::NFL, 0, 0, s));
}

// The boot cache's edges: the longest event id the record holds, a game with
// no date, and a record that is all the record's 16 id bytes with no end.
static void test_team_cache_edges() {
  const int64_t now = 1792000000;
  Schedule d;
  d.valid = true;
  d.event_id = "123456789012345";  // 15 characters and the end: fits
  d.kickoff_epoch = now + 3600;
  d.group = 80;
  TeamCache c;
  CHECK(team_cache_save(d, League::NCAA, 2294, now, c));
  Schedule s;
  CHECK(team_cache_load(c, League::NCAA, 2294, now + 60, s));
  CHECK_EQ(s.event_id, d.event_id);
  CHECK_EQ(s.group, (uint32_t) 80);
  CHECK(!s.time_tbd);
  CHECK(s.comp_slug == nullptr);
  // A record from flash whose id fills all 16 bytes still reads as a string.
  memset(c.event_id, '7', sizeof(c.event_id));
  CHECK(team_cache_load(c, League::NCAA, 2294, now + 60, s));
  CHECK_EQ(s.event_id, std::string(16, '7'));
  // A game ESPN gave no date saves, and never loads: its kickoff reads as long over.
  Schedule undated = d;
  undated.kickoff_epoch = 0;
  CHECK(team_cache_save(undated, League::NCAA, 2294, now, c));
  CHECK(!team_cache_load(c, League::NCAA, 2294, now + 60, s));
  // A schedule that isn't valid saves nothing and leaves the record alone.
  Schedule bad = d;
  bad.valid = false;
  TeamCache kept = c;
  CHECK(!team_cache_save(bad, League::NCAA, 2294, now, c));
  CHECK(memcmp(&kept, &c, sizeof(c)) == 0);
  // A negative clock is no clock.
  CHECK(!team_cache_save(d, League::NCAA, 2294, -5, c));
}

static void test_boot_hold() {
  // Holding: no board yet, inside the cap.
  CHECK(boot_hold(false, 1000, 1000));
  CHECK(boot_hold(false, 1000, 1000 + BOOT_HOLD_CAP_MS - 1));
  // The cap ends it whether or not a board came.
  CHECK(!boot_hold(false, 1000, 1000 + BOOT_HOLD_CAP_MS));
  // The first scoreboard ends it at once.
  CHECK(!boot_hold(true, 1000, 1500));
  // No hold running.
  CHECK(!boot_hold(false, 0, 1500));
  // millis() wraps every 49 days: the age is still right.
  uint32_t before_wrap = 0xFFFFFFFFu - 2000u;
  CHECK(boot_hold(false, before_wrap, 3000));
  CHECK(!boot_hold(false, before_wrap, before_wrap + BOOT_HOLD_CAP_MS));
}

static void test_rotate_minutes() {
  // 1 minute is a real choice on the device page and in the app. Treating it
  // as corrupt at boot reset the mode to My team.
  CHECK(espn::rotate_minutes_valid(1));
  CHECK(espn::rotate_minutes_valid(30));
  CHECK(!espn::rotate_minutes_valid(0));
  CHECK(!espn::rotate_minutes_valid(31));
  CHECK(espn::rotate_minutes_valid(espn::kRotateDefaultMinutes));
  CHECK(espn::kRotateDefaultMinutes == 1);
  CHECK(espn::clamp_rotate_minutes(0) == 1);
  CHECK(espn::clamp_rotate_minutes(-5) == 1);
  CHECK(espn::clamp_rotate_minutes(1) == 1);
  CHECK(espn::clamp_rotate_minutes(45) == 30);
}

// Switch every's 30 seconds: what the set request's seconds come to, what is
// saved for them (the minutes, and the flag that counts only next to 1
// minute), and what the state document shows pages from before.
static void test_rotate_seconds() {
  CHECK(espn::rotate_seconds_valid(30));
  CHECK(espn::rotate_seconds_valid(60));
  CHECK(espn::rotate_seconds_valid(1800));
  CHECK(!espn::rotate_seconds_valid(0));
  CHECK(!espn::rotate_seconds_valid(45));
  CHECK(!espn::rotate_seconds_valid(90));
  CHECK(!espn::rotate_seconds_valid(1860));
  CHECK_EQ(espn::clamp_rotate_seconds(INT_MIN), 30);
  CHECK_EQ(espn::clamp_rotate_seconds(0), 30);
  CHECK_EQ(espn::clamp_rotate_seconds(30), 30);
  CHECK_EQ(espn::clamp_rotate_seconds(59), 30);
  CHECK_EQ(espn::clamp_rotate_seconds(60), 60);
  CHECK_EQ(espn::clamp_rotate_seconds(119), 60);
  CHECK_EQ(espn::clamp_rotate_seconds(1800), 1800);
  CHECK_EQ(espn::clamp_rotate_seconds(INT_MAX), 1800);
  for (int s = -100; s < 4000; s++)
    CHECK(espn::rotate_seconds_valid(espn::clamp_rotate_seconds(s)));

  // Saved: the minutes stay 1 to 30, as older firmware checks them at boot.
  CHECK_EQ(espn::rotate_minutes_of(30), 1);
  CHECK_EQ(espn::rotate_minutes_of(60), 1);
  CHECK_EQ(espn::rotate_minutes_of(600), 10);
  CHECK_EQ(espn::rotate_minutes_of(1800), 30);
  for (int s = 30; s <= 1800; s = s == 30 ? 60 : s + 60) {
    const int m = espn::rotate_minutes_of(s);
    CHECK(espn::rotate_minutes_valid(m));
    CHECK_EQ(espn::rotate_saved_seconds(m, s < 60), s);  // what is saved reads back the same
  }
  // The flag counts only next to 1 minute: any other minute set on older
  // firmware, which leaves it set, is still minutes, and every panel from
  // before reads as it was. 1 minute set there can't be told from 30 seconds.
  CHECK_EQ(espn::rotate_saved_seconds(1, true), 30);
  CHECK_EQ(espn::rotate_saved_seconds(1, false), 60);
  CHECK_EQ(espn::rotate_saved_seconds(10, true), 600);
  CHECK_EQ(espn::rotate_saved_seconds(30, false), 1800);
  for (int m = espn::kRotateMinMinutes; m <= espn::kRotateMaxMinutes; m++)
    CHECK_EQ(espn::rotate_saved_seconds(m, false), m * 60);

  // A turn is over once its clock has run the setting; 0 hasn't started.
  CHECK(!espn::rotate_turn_over(5000, 0, 30));
  CHECK(!espn::rotate_turn_over(30999, 1000, 30));
  CHECK(espn::rotate_turn_over(31000, 1000, 30));
  CHECK(espn::rotate_turn_over(1000 + 1800 * 1000u, 1000, 1800));
  CHECK(!espn::rotate_turn_over(999 + 1800 * 1000u, 1000, 1800));
  CHECK(espn::rotate_turn_over(25000, UINT32_MAX - 4999, 30));  // across the millis() wrap
  CHECK(!espn::rotate_turn_over(24999, UINT32_MAX - 4999, 30));
}

// Another game's big play (watch.h) has the board for a while: the game the
// board goes back to gets what was left of its turn, not a turn already used
// up by the stay.
static void test_rotate_turn_resume() {
  // 30 s turns, a 60 s stay starting 20 s in: 10 s left on the way back.
  uint32_t t = espn::rotate_turn_resume(1000, 81000, 60000);
  CHECK_EQ(t, 61000u);
  CHECK(!espn::rotate_turn_over(81000, t, 30));
  CHECK(!espn::rotate_turn_over(90999, t, 30));
  CHECK(espn::rotate_turn_over(91000, t, 30));
  // A turn that was over before the stay is still over after it.
  t = espn::rotate_turn_resume(1000, 96000, 60000);
  CHECK(espn::rotate_turn_over(96000, t, 30));
  // Never moves past now, and a clock that hasn't started stays that way.
  CHECK_EQ(espn::rotate_turn_resume(1000, 5000, 60000), 5000u);
  CHECK_EQ(espn::rotate_turn_resume(0, 5000, 1000), 0u);
  CHECK_EQ(espn::rotate_turn_resume(1000, 5000, 0), 1000u);
  // Across the millis() wrap, and never the 0 that means no clock.
  CHECK_EQ(espn::rotate_turn_resume(UINT32_MAX - 999, 20000, 10000), 9000u);
  CHECK_EQ(espn::rotate_turn_resume(UINT32_MAX, 10, 1), 1u);

  // Favorite teams' clock, in epoch seconds.
  const int64_t T = 1000000;
  CHECK_EQ(espn::rotate_since_resume(T, T + 80, 60), T + 60);
  CHECK_EQ(espn::rotate_since_resume(T, T + 50, 60), T + 50);
  CHECK_EQ(espn::rotate_since_resume(T, T + 80, 0), T);
  CHECK_EQ(espn::rotate_since_resume(T, T + 80, -5), T);
  // Alternating every 30 s, a 60 s stay from 20 s in: the favorite keeps 10 s.
  FavRules r;
  r.alternate = true;
  r.rotate_s = 30;
  std::vector<FavGame> two = {fav(GameState::IN, T - 600), fav(GameState::IN, T - 600)};
  const int64_t since = espn::rotate_since_resume(T, T + 80, 60);
  CHECK_EQ(pick_favorite(two, T + 80, r, 0, since, -1).index, 0);
  CHECK_EQ(pick_favorite(two, T + 89, r, 0, since, -1).index, 0);
  CHECK_EQ(pick_favorite(two, T + 90, r, 0, since, -1).index, 1);
}

// The root "id" of a recorded single-event document, so the event id check
// runs on every sport's document, not only football's.
static std::string root_id(const std::string &json) {
  size_t at = json.find("\"id\":\"");
  if (at == std::string::npos)
    return "";
  at += 6;
  return json.substr(at, json.find('"', at) - at);
}

// MLB single-event documents recorded 2026-10-07 (see fixtures/README.md).
static const uint32_t kBraves = 15, kDodgers = 19, kWhiteSox = 4, kBrewers = 8;

static GameSnapshot mlb_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::BASEBALL, team, s, root_id(json)));
  return s;
}

// A team change's placeholder is dropped on a failed read so the board says
// "Can't reach ESPN"; a game read from ESPN or a final card never is.
static void test_team_placeholder() {
  Team dal{League::NFL, 6, "DAL", "Dallas Cowboys", 0};
  GameSnapshot p = team_placeholder(dal);
  CHECK(p.valid);
  CHECK_EQ(p.team_abbr, std::string("DAL"));
  CHECK_EQ(p.team_logo, team_logo_url(League::NFL, 6, "DAL"));
  CHECK(is_team_placeholder(p));
  CHECK(!is_team_placeholder(GameSnapshot{}));
  GameSnapshot pre = mlb_event("fixtures/event_mlb_top.json", kBraves);
  CHECK(!pre.event_id.empty());
  CHECK(!is_team_placeholder(pre));
  Final f;
  f.away_abbr = "DAL";
  f.home_abbr = "PHI";
  CHECK(!is_team_placeholder(final_snapshot(f)));
  // A golf tour's: the card with the tour's name, no logo
  Team pga{League::PGA, 1106, "PGA", "PGA Tour", 0};
  GameSnapshot gp = team_placeholder(pga);
  CHECK(gp.valid);
  CHECK(gp.sport == Sport::GOLF);
  CHECK_EQ(gp.game_name, std::string("PGA Tour"));
  CHECK(gp.team_logo.empty());
  CHECK(gp.team_abbr.empty());
  CHECK(is_team_placeholder(gp));
  CHECK_EQ(golf_card_text(gp, false, ""), std::string("PGA Tour"));
  CHECK(golf_lines(gp, true).name.empty());
  CHECK_EQ(status_text(gp, TickerOptions{}, ""), std::string("PGA Tour"));
  // A racing series': the card with the series' name
  Team f1{League::F1, 2030, "F1", "Formula 1", 0};
  GameSnapshot fp = team_placeholder(f1);
  CHECK(fp.valid);
  CHECK(fp.sport == Sport::RACING);
  CHECK_EQ(fp.game_name, std::string("Formula 1"));
  CHECK(fp.team_logo.empty());
  CHECK(is_team_placeholder(fp));
  CHECK_EQ(racing_card_text(fp, false, ""), std::string("Formula 1"));
  CHECK_EQ(board_card_text(fp, true, ""), std::string("Formula 1"));
  CHECK(racing_lines(fp, true).name.empty());
  CHECK_EQ(status_text(fp, TickerOptions{}, ""), std::string("Formula 1"));
  CHECK_EQ(ha_status_text(fp, ""), std::string("Formula 1"));
}

static void test_mlb_urls() {
  CHECK_EQ(team_url(League::MLB, 15), std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams/15"));
  CHECK_EQ(event_url(League::MLB, "401908016"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard/401908016"));
  CHECK_EQ(team_logo_url(League::MLB, 15, "ATL"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/mlb/500-dark/scoreboard/atl.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::MLB)), std::string("mlb"));
  CHECK(league_by_key("mlb") != nullptr && league_by_key("mlb")->per_event);
  CHECK(league_by_key("cfl") == nullptr);
  // A West Coast game still on at 2:30 AM Eastern is listed under the day it
  // started; the next morning's scan moves on to the new day.
  CHECK(scan_url(League::MLB, parse_iso8601_z("2026-10-08T06:30Z")).find("dates=20261007") != std::string::npos);
  CHECK(scan_url(League::MLB, parse_iso8601_z("2026-10-08T13:00Z")).find("dates=20261008") != std::string::npos);
  CHECK_EQ(scan_url(League::MLB, parse_iso8601_z("2026-10-08T13:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard?dates=20261008"));
  CHECK(league_sport(League::MLB) == Sport::BASEBALL);
  CHECK(league_sport(League::NCAA) == Sport::FOOTBALL);
  bool found = false;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::MLB && kTeams[i].espn_id == kBraves) {
      CHECK_EQ(team_option(kTeams[i]), std::string("MLB: Atlanta Braves"));
      found = true;
    }
  }
  CHECK(found);
}

static void test_mlb_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_atl.json"), s));
  CHECK_EQ(s.event_id, std::string("401908016"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-07T22:00Z"));
  CHECK_EQ(s.team_color, std::string("0c2340"));
  CHECK_EQ(s.team_record, std::string("94-68"));
}

static void test_mlb_live() {
  // Top 7th, LAD batting, runner on second, 2-1 count, two out. ATL is home.
  GameSnapshot t = mlb_event("fixtures/event_mlb_top.json", kBraves);
  CHECK(t.valid);
  CHECK(t.sport == Sport::BASEBALL);
  CHECK(t.state == GameState::IN);
  CHECK(t.team_home);
  CHECK_EQ(t.team_abbr, std::string("ATL"));
  CHECK_EQ(t.opp_abbr, std::string("LAD"));
  CHECK_EQ(t.team_score, 1);
  CHECK_EQ(t.opp_score, 1);
  CHECK(t.mlb.half == Half::TOP);
  CHECK_EQ((int) t.mlb.balls, 2);
  CHECK_EQ((int) t.mlb.strikes, 1);
  CHECK_EQ((int) t.mlb.outs, 2);
  CHECK_EQ((int) t.mlb.bases, (int) Baseball::SECOND);
  CHECK_EQ(t.possession, 2);  // the visitors bat in the top half
  CHECK_EQ(t.mlb.batter, std::string("M. Muncy"));
  CHECK_EQ(t.mlb.pitcher, std::string("D. Fuentes"));
  CHECK_EQ(t.mlb.team_hits, 2);
  CHECK_EQ(t.mlb.opp_hits, 3);
  CHECK_EQ(t.mlb.team_errors, 2);
  CHECK_EQ(t.mlb.opp_errors, 0);
  CHECK_EQ(t.mlb.series, std::string("LAD lead series 2-1"));
  CHECK_EQ(t.play_type, std::string("Ball"));
  CHECK_EQ(t.last_play, std::string("Pitch 3 : Ball 2"));
  CHECK_EQ(t.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/mlb/500-dark/scoreboard/atl.png&w=64&h=64"));
  CHECK_EQ(clock_text(t), std::string("Top 7th"));
  CHECK_EQ(baseball_count(t), std::string("2-1"));
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(t, o, ""), std::string("2-1, 2 outs | P D. Fuentes, AB M. Muncy | Pitch 3 : Ball 2"));
  TickerOptions quiet = o;
  quiet.baseball_count = quiet.last_play = false;
  CHECK_EQ(status_text(t, quiet, ""), std::string("Top 7th"));
  // Football's switch doesn't touch the count.
  TickerOptions no_football = o;
  no_football.down_distance = false;
  CHECK_EQ(status_text(t, no_football, ""), status_text(t, o, ""));

  // The same game from the Dodgers' side: they bat.
  GameSnapshot d = mlb_event("fixtures/event_mlb_top.json", kDodgers);
  CHECK(!d.team_home);
  CHECK_EQ(d.possession, 1);
  CHECK_EQ(d.mlb.team_hits, 3);

  // Bottom 6th: the Braves bat, one out.
  GameSnapshot b = mlb_event("fixtures/event_mlb_bottom.json", kBraves);
  CHECK(b.mlb.half == Half::BOTTOM);
  CHECK_EQ(b.possession, 1);
  CHECK_EQ((int) b.mlb.outs, 1);
  CHECK_EQ(status_text(b, o, ""), std::string("1-1, 1 out | P E. Henriquez, AB L. Thomas | Pitch 2 : Ball 1"));

  // End of an inning: ESPN keeps a stale count, the panel must not show it.
  GameSnapshot e = mlb_event("fixtures/event_mlb_end.json", kBraves);
  CHECK(e.mlb.half == Half::END);
  CHECK_EQ((int) e.mlb.balls, -1);
  CHECK_EQ((int) e.mlb.outs, -1);
  CHECK_EQ((int) e.mlb.bases, 0);
  CHECK_EQ(e.possession, 0);
  CHECK_EQ(baseball_count(e), std::string(""));
  CHECK_EQ(clock_text(e), std::string("End 5th"));
  CHECK_EQ(status_text(e, o, ""), std::string("ATL 1 H 2 E, LAD 2 H 0 E | LAD lead series 2-1"));
}

static void test_mlb_pre_post() {
  GameSnapshot p = mlb_event("fixtures/event_mlb_pre.json", kBrewers);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ(p.mlb.team_probable, std::string("R. Gasser"));
  CHECK_EQ(p.mlb.opp_probable, std::string("W. Buehler"));
  CHECK_EQ((int) p.mlb.balls, -1);
  CHECK_EQ(p.possession, 0);
  CHECK_EQ(p.odds, std::string("SD -112"));
  CHECK_EQ(p.tv, std::string("FS1"));
  TickerOptions o;
  std::string pre = status_text(p, o, "Today 10:00 PM");
  CHECK(pre.rfind("Today 10:00 PM | MIL leads series 2-1 | MIL R. Gasser vs SD W. Buehler | SD -112 | ", 0) == 0);
  CHECK(pre.find(" | FS1 | Petco Park") != std::string::npos);

  GameSnapshot f = mlb_event("fixtures/event_mlb_final.json", kWhiteSox);
  CHECK(f.state == GameState::POST);
  CHECK(!f.team_winner);
  CHECK_EQ(f.team_score, 3);
  CHECK_EQ(f.opp_score, 9);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | CHW 6 H 0 E, CLE 11 H 0 E | CHW lead series 2-1"));
  f.short_detail = "Final/10";
  CHECK(status_text(f, o, "").rfind("Final/10 | ", 0) == 0);

  // A real rainout (CHC @ CLE, 2026-04-04): ESPN sends post with completed false.
  GameSnapshot ppd = mlb_event("fixtures/event_mlb_postponed.json", 5);
  CHECK(ppd.state == GameState::POST);
  CHECK(!ppd.completed);
  CHECK_EQ(status_text(ppd, o, ""), std::string("Postponed"));
  GameSnapshot ppd_was = ppd;
  ppd_was.state = GameState::IN;
  ppd.team_score = 2;
  CHECK_EQ(decide_splash(ppd_was, ppd, true, true).text, std::string(""));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_mlb_top.json"), Sport::BASEBALL, kWhiteSox, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_mlb_top.json"), Sport::BASEBALL, kBraves, x, "401908017"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::BASEBALL, kBraves, x));
}

static void test_mlb_splash() {
  GameSnapshot prev = mlb_event("fixtures/event_mlb_top.json", kBraves);
  auto after = [&](int us, int them, const char *play) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    c.play_type = play;
    c.play_points = us + them;  // one play scored them all
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true).text, std::string("RUN!"));
  CHECK_EQ(decide_splash(prev, after(3, 0, "Play Result"), true).text, std::string("3 RUNS!"));
  CHECK_EQ(decide_splash(prev, after(2, 0, "Home Run"), true).text, std::string("HOME RUN!"));
  CHECK_EQ(decide_splash(prev, after(4, 0, "Home Run"), true).text, std::string("GRAND SLAM!"));
  // Four runs in the update but two from the home run itself (alerts.h).
  GameSnapshot two_and_two = after(4, 0, "Home Run");
  two_and_two.play_points = 2;
  CHECK_EQ(decide_splash(prev, two_and_two, true).text, std::string("HOME RUN!"));
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true).color, (uint32_t) 0x0c2340);
  CHECK_EQ(decide_splash(prev, after(0, 1, "Play Result"), true).text, std::string("LAD SCORES"));
  CHECK_EQ(decide_splash(prev, after(0, 2, "Play Result"), true).text, std::string("LAD 2 RUNS"));
  CHECK_EQ(decide_splash(prev, after(0, 1, "Home Run"), true).text, std::string("LAD HOME RUN"));
  CHECK_EQ(decide_splash(prev, after(0, 1, "Home Run"), false).text, std::string(""));
  GameSnapshot hr = after(0, 2, "Play Result");
  hr.last_play = "Muncy homered to right (402 feet), Freeman scored.";
  CHECK_EQ(decide_splash(prev, hr, true).text, std::string("LAD HOME RUN"));
  // Live modes: neither side is ours.
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true, true).text, std::string("ATL SCORES"));
  CHECK_EQ(decide_splash(prev, after(0, 0, "Ball"), true).text, std::string(""));
  GameSnapshot fin = after(2, 0, "Play Result");
  fin.state = GameState::POST;
  fin.completed = true;
  GameSnapshot was = fin;
  was.state = GameState::IN;
  CHECK_EQ(decide_splash(was, fin, true).text, std::string("ATL WINS!"));
}

// Favorites list: the today-only filter and the season horizon.
static void test_favorites_filters() {
  const int64_t day = 86400;
  const int64_t now = 1791000000;  // a Wednesday afternoon, say
  FavRules r;
  r.day_start = now - 15 * 3600;
  r.day_end = r.day_start + day;
  auto card = [](GameState st, int64_t kick, int64_t horizon) {
    FavGame g;
    g.valid = true;
    g.state = st;
    g.kickoff_epoch = kick;
    g.horizon_s = horizon;
    return g;
  };
  const int64_t h = 10 * day;
  FavGame tonight = card(GameState::PRE, now + 4 * 3600, h);
  FavGame saturday = card(GameState::PRE, now + 3 * day, h);
  FavGame spring = card(GameState::PRE, now + 150 * day, h);       // MLB in December
  FavGame bye_week = card(GameState::PRE, now + 13 * day, 0);      // football: no horizon
  FavGame old_final = card(GameState::POST, now - 40 * day, h);    // last season's final
  FavGame live = card(GameState::IN, now - 3600, h);
  FavGame final_today = card(GameState::POST, r.day_start + 3600, h);

  CHECK(fav_idle_ok(tonight, now, r, false));
  CHECK(fav_idle_ok(saturday, now, r, false));
  CHECK(!fav_idle_ok(spring, now, r, false));
  CHECK(fav_idle_ok(bye_week, now, r, false));
  CHECK(!fav_idle_ok(old_final, now, r, false));
  CHECK(fav_idle_ok(tonight, now, r, true));
  CHECK(!fav_idle_ok(saturday, now, r, true));
  CHECK(fav_idle_ok(live, now, r, true));
  CHECK(fav_idle_ok(final_today, now, r, true));
  CHECK(!fav_idle_ok(FavGame{}, now, r, false));

  // The playlist skips the out-of-season card.
  std::vector<FavGame> games = {saturday, spring, tonight};
  FavChoice c = pick_favorite(games, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 2);
  c = pick_favorite(games, now, r, 2, now - 20, -1);
  CHECK_EQ(c.index, 0);
  // Today only: just tonight's game, and it stays up past the dwell.
  r.today_only = true;
  c = pick_favorite(games, now, r, 2, now - 20, -1);
  CHECK_EQ(c.index, 2);
  CHECK(!c.locked);
  // Nothing today: the filter relaxes to the season horizon...
  std::vector<FavGame> quiet = {saturday, spring};
  c = pick_favorite(quiet, now, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(quiet, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 0);
  // ...and with every team out of season, any card beats a blank panel.
  std::vector<FavGame> winter = {spring, old_final};
  c = pick_favorite(winter, now, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(winter, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 1);
  // A remote press shows its card for the dwell even when today-only would
  // skip it, then the playlist moves on.
  c = pick_favorite(games, now, r, 0, now, 0);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(games, now, r, 0, now - 20, 0);
  CHECK_EQ(c.index, 2);
  // A locked game still wins over everything.
  r.today_only = false;
  std::vector<FavGame> mixed = {saturday, live};
  c = pick_favorite(mixed, now, r, 0, now, -1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
}

// Soccer single-event documents (see fixtures/README.md): Brazilian Serie A
// recorded live 2026-10-08, MLS and Premier League pre-game and finals, a
// Champions League final on penalties and a World Cup game after extra time.
static const uint32_t kArsenal = 359, kLeeds = 357, kSunderland = 366, kMiami = 20232, kDcUnited = 193,
                      kInter = 1936, kCorinthians = 874, kVasco = 3454, kMirassol = 9169, kBragantino = 6079,
                      kPsg = 160, kArgentina = 202, kCruzeiro = 2022;

static GameSnapshot soccer_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::SOCCER, team, s, root_id(json)));
  return s;
}

static void test_soccer_urls() {
  CHECK_EQ(team_url(League::EPL, kArsenal), std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/teams/359"));
  CHECK_EQ(team_url(League::MLS, kMiami), std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/usa.1/teams/20232"));
  CHECK_EQ(event_url(League::EPL, "401879268"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/scoreboard/401879268"));
  // A soccer schedule lists past results unless asked for fixtures.
  CHECK_EQ(schedule_url(League::EPL, kArsenal),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/teams/359/schedule?fixture=true"));
  CHECK_EQ(schedule_url(League::MLB, 15), std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams/15/schedule"));
  CHECK_EQ(team_logo_url(League::EPL, kArsenal, "ARS"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/359.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::MLS, kMiami, "MIA"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/20232.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::EPL)), std::string("epl"));
  CHECK_EQ(std::string(league_key(League::MLS)), std::string("mls"));
  CHECK(league_by_key("epl") != nullptr && league_by_key("epl")->per_event && league_by_key("epl")->season_list);
  CHECK(league_by_key("mls") != nullptr && league_by_key("mls")->logo_by_id);
  CHECK(league_sport(League::EPL) == Sport::SOCCER);
  CHECK(league_sport(League::MLS) == Sport::SOCCER);
  int epl = 0, mls = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    epl += t.league == League::EPL;
    mls += t.league == League::MLS;
    if (t.league == League::EPL && t.espn_id == kArsenal)
      CHECK_EQ(team_option(t), std::string("EPL: Arsenal"));
    if (t.league == League::MLS && t.espn_id == kMiami)
      CHECK_EQ(team_option(t), std::string("MLS: Inter Miami CF"));
  }
  CHECK_EQ(epl, 20);
  CHECK_EQ(mls, 30);
}

static void test_soccer_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_epl_ars.json"), s));
  CHECK_EQ(s.event_id, std::string("401879268"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-10T11:30Z"));
  CHECK_EQ(s.team_color, std::string("e20520"));
  CHECK_EQ(s.team_record, std::string("4-0-1"));
  CHECK(parse_team_str(slurp("fixtures/team_mls_mia.json"), s));
  CHECK_EQ(s.event_id, std::string("761847"));
  CHECK_EQ(s.team_record, std::string("12-10-5"));

  // The fixture list feeds Up next the same way football's schedule does.
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_mls_mia.json"), kMiami, 4, up));
  CHECK_EQ(up.size(), (size_t) 2);
  CHECK_EQ(up[0].event_id, std::string("761847"));
  CHECK_EQ(up[0].opp_id, kDcUnited);
  CHECK_EQ(up[0].opp_abbr, std::string("DC"));
  CHECK(up[0].home);
  CHECK_EQ(up[0].tv, std::string("Apple TV"));
  CHECK_EQ(up[1].opp_abbr, std::string("NYC"));
  CHECK_EQ(up[1].kickoff_epoch, parse_iso8601_z("2026-10-14T23:30Z"));
}

static void test_soccer_pre() {
  GameSnapshot a = soccer_event("fixtures/event_epl_pre.json", kArsenal);
  CHECK(a.valid);
  CHECK(a.sport == Sport::SOCCER);
  CHECK(a.state == GameState::PRE);
  CHECK(a.team_home);
  CHECK_EQ(a.opp_abbr, std::string("LEE"));
  CHECK_EQ(a.team_record, std::string("4-0-1"));  // W-D-L, the order of ESPN's standings
  // ESPN sends the newest result first ("LWWWW": Arsenal lost their last
  // game); the ticker prints it oldest first like a league table.
  CHECK_EQ(a.soc.team_form, std::string("WWWWL"));
  CHECK_EQ(a.soc.opp_form, std::string("DDLWD"));
  CHECK_EQ(a.soc.team_line, std::string("-275"));
  CHECK_EQ(a.soc.draw_line, std::string("+425"));
  CHECK_EQ(a.soc.opp_line, std::string("+750"));
  CHECK(a.soc.goals.empty());
  CHECK_EQ(a.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/359.png&w=64&h=64"));
  TickerOptions o;
  CHECK_EQ(status_text(a, o, "Sat 7:30 AM"),
           std::string("Sat 7:30 AM | Form ARS WWWWL, LEE DDLWD | ARS -275, draw +425, LEE +750 | O/U 2.5 | USA Net | "
                       "Emirates Stadium"));
  // From the visitors' side the line turns around.
  GameSnapshot l = soccer_event("fixtures/event_epl_pre.json", kLeeds);
  CHECK(!l.team_home);
  CHECK_EQ(l.soc.team_line, std::string("+750"));
  CHECK_EQ(l.soc.opp_line, std::string("-275"));

  GameSnapshot m = soccer_event("fixtures/event_mls_pre.json", kMiami);
  CHECK_EQ(m.team_record, std::string("12-10-5"));
  CHECK_EQ(status_text(m, o, "Sat 7:30 PM"),
           std::string("Sat 7:30 PM | Form MIA DDWDL, DC DWDLD | MIA -270, draw +425, DC +500 | O/U 3.5 | Apple TV | "
                       "Nu Stadium"));
  TickerOptions no_odds = o;
  no_odds.odds = false;
  CHECK_EQ(status_text(m, no_odds, ""),
           std::string("Scheduled | Form MIA DDWDL, DC DWDLD | Nu Stadium"));
  // Only the favorite's line without the three-way one.
  GameSnapshot partial = m;
  partial.soc.opp_line.clear();
  CHECK_EQ(status_text(partial, o, "Sat 7:30 PM").find("MIA -270, draw +425 | O/U 3.5") != std::string::npos, true);

  GameSnapshot none;
  none.sport = Sport::SOCCER;
  CHECK_EQ(status_text(none, o, ""), std::string("No upcoming game"));
}

static void test_soccer_live() {
  TickerOptions o;
  o.clock = false;  // as the component asks: the clock has its own row

  // First half, no goals yet: an empty situation row.
  GameSnapshot fh = soccer_event("fixtures/event_soccer_first_half.json", kCruzeiro);
  CHECK(fh.state == GameState::IN);
  CHECK_EQ(fh.soc.status, std::string("STATUS_FIRST_HALF"));
  CHECK_EQ(clock_text(fh), std::string("25'"));
  CHECK_EQ(soccer_situation(fh), std::string(""));
  CHECK_EQ(status_text(fh, o, ""),
           std::string("Cards CRU 1Y, SAO 1Y | Possession CRU 64.1%, SAO 35.9% | Shots (on target) CRU 4 (0), SAO 4 (1) | "
                       "24' Yellow Card: G. Rojas (CRU)"));
  TickerOptions with_clock;
  with_clock.soccer_stats = with_clock.last_play = false;
  CHECK_EQ(status_text(fh, with_clock, ""), std::string("25'"));
  // Football's and baseball's switches don't touch soccer's.
  TickerOptions others_off = o;
  others_off.down_distance = others_off.baseball_count = false;
  CHECK_EQ(status_text(fh, others_off, ""), status_text(fh, o, ""));

  // Half time, Vasco (away) lead 1-0.
  GameSnapshot h = soccer_event("fixtures/event_soccer_halftime.json", kVasco);
  CHECK(h.state == GameState::IN);
  CHECK_EQ(h.soc.status, std::string("STATUS_HALFTIME"));
  CHECK_EQ(h.display_clock, std::string("45'+5'"));  // the clock stops where the half ended
  CHECK_EQ(clock_text(h), std::string("HT"));
  CHECK_EQ(soccer_situation(h), std::string("28' Lescano"));
  CHECK_EQ(status_text(h, o, ""),
           std::string("Half time | Goals VAS 28' Lescano | Possession VAS 54.8%, BOT 45.2% | "
                       "Shots (on target) VAS 8 (4), BOT 6 (1) | W-D-L VAS 8-7-12, BOT 9-8-11"));

  // Stoppage time, Internacional (home) 2-1 up. ESPN's status name flips
  // between STATUS_SECOND_HALF and STATUS_IN_PROGRESS from poll to poll.
  GameSnapshot s = soccer_event("fixtures/event_soccer_stoppage.json", kInter);
  CHECK_EQ(s.soc.status, std::string("STATUS_IN_PROGRESS"));
  CHECK_EQ(clock_text(s), std::string("90'+2'"));
  CHECK_EQ(s.team_score, 2);
  CHECK_EQ(s.opp_score, 1);
  CHECK_EQ(s.soc.goals.size(), (size_t) 3);
  CHECK(s.soc.goals[0].ours);
  CHECK(!s.soc.goals[2].ours);
  CHECK_EQ(s.soc.goals[2].name, std::string("Andre"));  // "André", folded for the panel font
  CHECK_EQ(s.soc.team_yellows, 3);
  CHECK_EQ(s.soc.opp_yellows, 4);
  CHECK_EQ(s.soc.team_reds + s.soc.opp_reds, 0);
  CHECK_EQ(soccer_situation(s), std::string("80' Andre"));
  CHECK_EQ(s.soc.last_event, std::string("85' Yellow Card: R. Garro (COR)"));
  CHECK_EQ(status_text(s, o, ""),
           std::string("Goals INT 1' Vitinho, 77' Alan Patrick; COR 80' Andre | Cards INT 3Y, COR 4Y | "
                       "Possession INT 28%, COR 72% | Shots (on target) INT 17 (7), COR 21 (6) | "
                       "85' Yellow Card: R. Garro (COR)"));
  TickerOptions quiet = o;
  quiet.soccer_stats = quiet.last_play = false;
  CHECK_EQ(status_text(s, quiet, ""), std::string("90'+2'"));

  // Second half with a red card: Mirassol (away) down to ten.
  GameSnapshot r = soccer_event("fixtures/event_soccer_second_half.json", kMirassol);
  CHECK_EQ(clock_text(r), std::string("90'"));
  CHECK_EQ(r.soc.team_reds, 1);
  CHECK_EQ(r.soc.opp_reds, 0);
  CHECK_EQ(r.soc.last_event, std::string("70' Goal: Andre Luis (MIR)"));
  // 14 characters is too wide for the row: the last word of the name stays.
  CHECK_EQ(soccer_situation(r), std::string("70' Luis"));
  CHECK(status_text(r, o, "").rfind("Goals MIR 70' Andre Luis; BRA 51' Eduardo Sasha | Cards MIR 1R | ", 0) == 0);
  GameSnapshot b = soccer_event("fixtures/event_soccer_second_half.json", kBragantino);
  CHECK_EQ(b.soc.team_reds, 0);
  CHECK_EQ(b.soc.opp_reds, 1);

  // Full time in the same game as the stoppage-time poll.
  GameSnapshot f = soccer_event("fixtures/event_soccer_full_time.json", kInter);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(clock_text(f), std::string("FT"));
  CHECK_EQ(status_text(f, o, ""),
           std::string("Full time | Goals INT 1' Vitinho, 77' Alan Patrick; COR 80' Andre | W-D-L INT 6-10-12, COR 8-8-12"));
  CHECK_EQ(decide_splash(s, f, true).text, std::string("INT WINS!"));
  CHECK_EQ(decide_splash(s, f, true).color, (uint32_t) 0xC60000);
  GameSnapshot sc = soccer_event("fixtures/event_soccer_stoppage.json", kCorinthians);
  GameSnapshot fc = soccer_event("fixtures/event_soccer_full_time.json", kCorinthians);
  CHECK_EQ(decide_splash(sc, fc, true).text, std::string(""));
  CHECK_EQ(decide_splash(sc, fc, true, true).text, std::string("INT WINS!"));  // live modes: the winner, either side
}

static void test_soccer_finals() {
  TickerOptions o;
  // Premier League: Arsenal win 2-0 away with a stoppage-time penalty; Sunderland had a man sent off.
  GameSnapshot a = soccer_event("fixtures/event_epl_final.json", kArsenal);
  CHECK(a.state == GameState::POST);
  CHECK(a.team_winner);
  CHECK_EQ(a.soc.opp_reds, 1);
  CHECK(a.soc.goals.back().penalty);
  CHECK_EQ(status_text(a, o, ""),
           std::string("Full time | Goals ARS 58' Guimaraes, 90'+7' Saka (P) | Red cards SUN 1 | W-D-L ARS 4-0-0, SUN 1-1-2"));
  // "90'+7' Saka (P)" is 15 characters: the penalty mark gives way.
  CHECK_EQ(soccer_situation(a), std::string("90'+7' Saka"));
  CHECK_EQ(soccer_situation(a, 16), std::string("90'+7' Saka (P)"));
  GameSnapshot sun = soccer_event("fixtures/event_epl_final.json", kSunderland);
  CHECK_EQ(sun.soc.team_reds, 1);

  // MLS: a 2-2 draw. Both sides have winner false and nobody gets a splash.
  GameSnapshot d = soccer_event("fixtures/event_mls_final.json", kMiami);
  CHECK(!d.team_winner);
  CHECK_EQ(d.team_score, 2);
  CHECK_EQ(d.opp_score, 2);
  CHECK_EQ(clock_text(d), std::string("FT"));
  CHECK_EQ(status_text(d, o, ""),
           std::string("Full time | Goals MIA 24' Messi, 74' Suarez; SD 12' Dreyer, 82' Dreyer | W-D-L MIA 12-10-4, SD 8-7-11"));
  GameSnapshot live = d;
  live.state = GameState::IN;
  CHECK_EQ(decide_splash(live, d, true).text, std::string(""));
  CHECK_EQ(decide_splash(live, d, true, true).text, std::string(""));

  // Champions League final on penalties: 1-1, PSG win the shootout 4-3.
  GameSnapshot p = soccer_event("fixtures/event_soccer_pens.json", kArsenal);
  CHECK_EQ(p.soc.status, std::string("STATUS_FINAL_PEN"));
  CHECK_EQ(p.soc.team_shootout, 3);
  CHECK_EQ(p.soc.opp_shootout, 4);
  CHECK_EQ(p.soc.goals.size(), (size_t) 2);  // shootout kicks are not goals
  CHECK_EQ(clock_text(p), std::string("PENS"));
  CHECK_EQ(p.soc.last_event, std::string("Shootout Penalty - Scored: L. Beraldo (PSG)"));
  CHECK_EQ(status_text(p, o, ""),
           std::string("PSG win 4-3 on penalties | UEFA Champions League, Final | Goals ARS 6' Havertz; PSG 65' Dembele (P)"));
  GameSnapshot pl = p;
  pl.state = GameState::IN;
  pl.soc.status = "STATUS_SHOOTOUT";
  CHECK_EQ(clock_text(pl), std::string("PENS 3-4"));
  CHECK_EQ(decide_splash(pl, p, true).text, std::string(""));
  CHECK_EQ(decide_splash(pl, p, true, true).text, std::string("PSG WINS!"));
  GameSnapshot psg = soccer_event("fixtures/event_soccer_pens.json", kPsg);
  GameSnapshot psg_live = psg;
  psg_live.state = GameState::IN;
  CHECK_EQ(decide_splash(psg_live, psg, true).text, std::string("PSG WINS!"));

  // World Cup round of 32 after extra time, with an own goal at 111'.
  GameSnapshot e = soccer_event("fixtures/event_soccer_aet.json", kArgentina);
  CHECK_EQ(clock_text(e), std::string("AET"));
  CHECK(e.soc.goals.back().own_goal);
  CHECK(e.soc.goals.back().ours);  // an own goal counts for the side it helped
  CHECK_EQ(e.soc.goals.back().name, std::string("Borges"));
  CHECK_EQ(status_text(e, o, ""),
           std::string("After extra time | FIFA World Cup, Round of 32 | Goals ARG 29' Messi, 92' Martinez, "
                       "111' Borges (OG); CPV 59' Duarte, 103' Lopes Cabral"));
  CHECK_EQ(soccer_situation(e), std::string("111' Borges"));
  GameSnapshot og = e;
  og.soc.goals.back().clock = "55'";
  CHECK_EQ(soccer_situation(og), std::string("55' Borges"));  // "55' Borges OG" is 13
  CHECK_EQ(soccer_situation(og, 13), std::string("55' Borges OG"));
  og.soc.goals.back().name = "Hany";
  CHECK_EQ(soccer_situation(og), std::string("55' Hany OG"));

  // Extra time while it is played (no recording yet: built from the final).
  GameSnapshot et = e;
  et.state = GameState::IN;
  et.soc.status = "STATUS_SECOND_HALF_EXTRA_TIME";
  et.period = 3;
  et.display_clock = "105'+1'";
  CHECK_EQ(clock_text(et), std::string("ET 105'+1'"));
  et.period = 4;
  et.display_clock = "120'+12'";
  CHECK_EQ(clock_text(et), std::string("120'+12'"));
  et.period = 3;
  et.soc.status = "STATUS_HALFTIME_ET";
  CHECK_EQ(clock_text(et), std::string("ET HT"));
  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_soccer_aet.json"), Sport::SOCCER, kArsenal, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_soccer_aet.json"), Sport::SOCCER, kArgentina, x, "760501"));  // some other event
}

static void test_soccer_splash() {
  GameSnapshot prev = soccer_event("fixtures/event_soccer_stoppage.json", kInter);
  auto after = [&](int us, int them) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0), true).text, std::string("GOAL!"));
  CHECK_EQ(decide_splash(prev, after(1, 0), true).color, (uint32_t) 0xC60000);
  CHECK_EQ(decide_splash(prev, after(0, 1), true).text, std::string("COR GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), false).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(1, 0), true, true).text, std::string("INT GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(-1, 0), true).text, std::string(""));  // a goal taken back
  GameSnapshot pre = prev;
  pre.state = GameState::PRE;
  CHECK_EQ(decide_splash(pre, after(1, 0), true).text, std::string(""));
  for (const char *word : {"GOAL!", "REMO GOAL", "INT WINS!"})
    CHECK(strlen(word) <= 10);
}

static void test_soccer_text_helpers() {
  CHECK_EQ(ascii_fold("Guimar\xC3\xA3" "es"), std::string("Guimaraes"));
  CHECK_EQ(ascii_fold("Gy\xC3\xB6keres"), std::string("Gyokeres"));
  CHECK_EQ(ascii_fold("\xC3\x98" "degaard"), std::string("Odegaard"));
  CHECK_EQ(ascii_fold("Demb\xC3\xA9l\xC3\xA9"), std::string("Dembele"));
  CHECK_EQ(ascii_fold("\xC5\x81ukasz \xC5\x9E" "ahin"), std::string("Lukasz Sahin"));
  CHECK_EQ(ascii_fold("\xC8\x98tefan"), std::string("Stefan"));
  CHECK_EQ(ascii_fold("O\xE2\x80\x99Reilly"), std::string("O'Reilly"));
  CHECK_EQ(ascii_fold("Stra\xC3\x9F" "e \xC3\x86"), std::string("Strasse AE"));
  CHECK_EQ(ascii_fold("Plain ASCII 1-0"), std::string("Plain ASCII 1-0"));
  CHECK_EQ(ascii_fold("bad\xFF\xC3"), std::string("bad"));
  CHECK_EQ(ascii_fold("\xE6\x97\xA5\xE6\x9C\xAC"), std::string(""));
  CHECK_EQ(soccer_board_record("6-10-12"), std::string("6-10-12"));
  CHECK_EQ(soccer_board_record("12-10-12"), std::string("46 pts"));  // too wide under a logo
  CHECK_EQ(soccer_board_record(""), std::string(""));
  // No season has a thousand of anything: such a "record" is left as it
  // came, never multiplied into an int it overflows (fuzz_espn under UBSan).
  CHECK_EQ(soccer_board_record("999999999-0-0"), std::string("999999999-0-0"));
  CHECK_EQ(soccer_board_record("1000-0-0"), std::string("1000-0-0"));
  CHECK_EQ(soccer_board_record("999-99-9"), std::string("3096 pts"));
}

// A club's next game across its league and cups (team endpoints recorded
// 2026-10-08: Arsenal's league game is on 10/10, its Champions League game
// on 10/13, and it is not in the Europa League).
static void test_soccer_cups() {
  CHECK_EQ(team_url(League::EPL, kArsenal, "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/teams/359"));
  CHECK_EQ(event_url(League::EPL, "401915417", "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/scoreboard/401915417"));
  CHECK_EQ(team_url(League::MLS, kMiami, "concacaf.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/concacaf.champions/teams/20232"));
  CHECK_EQ(event_url(League::EPL, "401879268", nullptr), event_url(League::EPL, "401879268"));
  CHECK_EQ(team_url(League::NFL, 6, nullptr), team_url(League::NFL, 6));
  CHECK_EQ(std::string(league_cup(League::EPL, 0)), std::string("uefa.champions"));
  CHECK_EQ(std::string(league_cup(League::EPL, 2)), std::string("uefa.europa.conf"));
  CHECK(league_cup(League::EPL, 3) == nullptr);
  CHECK_EQ(std::string(league_cup(League::MLS, 0)), std::string("concacaf.champions"));
  CHECK_EQ(league_cup_count(League::EPL), (size_t) 3);
  CHECK_EQ(league_cup_count(League::MLS), (size_t) 1);
  CHECK_EQ(league_cup_count(League::NFL), (size_t) 0);
  CHECK_EQ(league_cup_count(League::MLB), (size_t) 0);

  std::vector<Schedule> comps(4);
  CHECK(parse_team_str(slurp("fixtures/team_epl_ars.json"), comps[0]));
  CHECK(parse_team_str(slurp("fixtures/team_ucl_ars.json"), comps[1]));
  CHECK(parse_team_str(slurp("fixtures/team_uel_ars.json"), comps[2]));
  for (size_t i = 0; i < comps.size(); i++) {
    adopt_league(comps[i], League::EPL);
    adopt_comp(comps[i], i == 0 ? nullptr : league_cup(League::EPL, i - 1));
  }
  CHECK_EQ(comps[1].event_id, std::string("401915417"));
  CHECK_EQ(comps[1].kickoff_epoch, parse_iso8601_z("2026-10-13T19:00Z"));
  CHECK(comps[2].valid);  // not in the Europa League: a good read with no game
  CHECK(comps[2].event_id.empty());
  CHECK(!comps[3].valid);  // the Conference League not read yet

  int64_t league_ko = parse_iso8601_z("2026-10-10T11:30Z");
  // Before the league game: it is first.
  CHECK_EQ(pick_schedule(comps, parse_iso8601_z("2026-10-08T00:00Z"), ""), 0);
  // While it is on, and just after, ESPN still names it: it stays.
  CHECK_EQ(pick_schedule(comps, league_ko + 2 * 3600, ""), 0);
  // Seen to finish, or long over: the cup game is next.
  CHECK_EQ(pick_schedule(comps, league_ko + 2 * 3600, "401879268"), 1);
  CHECK_EQ(pick_schedule(comps, league_ko + 5 * 3600, ""), 1);
  // A cup game first in the week.
  std::vector<Schedule> early = comps;
  early[1].kickoff_epoch = league_ko - 3 * 86400;
  CHECK_EQ(pick_schedule(early, parse_iso8601_z("2026-10-06T00:00Z"), ""), 1);
  // Same kickoff: the league's read wins.
  early[1].kickoff_epoch = league_ko;
  CHECK_EQ(pick_schedule(early, 0, ""), 0);
  // Nothing else: a game that is over is still better than none.
  std::vector<Schedule> only_over = {comps[0], comps[2]};
  CHECK_EQ(pick_schedule(only_over, league_ko + 10 * 3600, ""), 0);
  // No read names a game (an off season): none.
  std::vector<Schedule> none = {comps[2], comps[3]};
  CHECK_EQ(pick_schedule(none, 0, ""), -1);
  CHECK_EQ(pick_schedule(std::vector<Schedule>{}, 0, ""), -1);
  // The league read had no game but a cup does (MLS's winter, the CONCACAF Champions Cup).
  std::vector<Schedule> cup_only = {comps[2], comps[1]};
  CHECK_EQ(pick_schedule(cup_only, 0, ""), 1);

  // The boot cache has no room for the cup: a cup game saves a blank record
  // that never loads, so an older league game cannot come back either.
  TeamCache c{};
  c.team_id = 99;
  CHECK(team_cache_save(comps[1], League::EPL, kArsenal, league_ko, c));
  CHECK_EQ(c.team_id, (uint32_t) 0);
  Schedule loaded;
  CHECK(!team_cache_load(c, League::EPL, kArsenal, league_ko, loaded));
  CHECK(team_cache_save(comps[0], League::EPL, kArsenal, league_ko - 86400, c));
  CHECK(team_cache_load(c, League::EPL, kArsenal, league_ko - 3600, loaded));
  CHECK_EQ(loaded.event_id, std::string("401879268"));
  CHECK(loaded.comp_slug == nullptr);
}

// NBA (preseason) and WNBA (semifinal) single-event documents recorded
// 2026-10-07 (see fixtures/README.md).
static const uint32_t kPacers = 11, kWolves = 16, kThunder = 25, kBlazers = 22, kDream = 20, kLiberty = 9;

static GameSnapshot hoops_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::BASKETBALL, team, s, root_id(json)));
  return s;
}

static void test_nba_urls() {
  CHECK_EQ(team_url(League::NBA, 11), std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/nba/teams/11"));
  CHECK_EQ(event_url(League::WNBA, "401918297"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/wnba/scoreboard/401918297"));
  CHECK_EQ(team_logo_url(League::NBA, 11, "IND"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500-dark/scoreboard/ind.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::WNBA, 129689, "GS"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/wnba/500-dark/gs.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::NBA)), std::string("nba"));
  CHECK_EQ(std::string(league_key(League::WNBA)), std::string("wnba"));
  for (const char *key : {"nba", "wnba"}) {
    const LeagueInfo *l = league_by_key(key);
    CHECK(l != nullptr && l->per_event && !l->season_list && l->linger_min == 30);
    CHECK(l != nullptr && l->sport == Sport::BASKETBALL);
  }
  int nba = 0, wnba = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    nba += t.league == League::NBA;
    wnba += t.league == League::WNBA;
    if (t.league == League::NBA && t.espn_id == kPacers)
      CHECK_EQ(team_option(t), std::string("NBA: Indiana Pacers"));
    if (t.league == League::WNBA && t.espn_id == kLiberty)
      CHECK_EQ(team_option(t), std::string("WNBA: New York Liberty"));
  }
  CHECK_EQ(nba, 30);
  CHECK(wnba >= 13);
}

static void test_nba_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_nba_ind.json"), s));
  CHECK_EQ(s.event_id, std::string("401914123"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-07T23:00Z"));
  CHECK_EQ(s.team_color, std::string("0c2340"));
  CHECK_EQ(s.team_record, std::string("0-0"));
  Schedule w;
  CHECK(parse_team_str(slurp("fixtures/team_wnba_ny.json"), w));
  CHECK_EQ(w.event_id, std::string("401918297"));
  CHECK_EQ(w.kickoff_epoch, parse_iso8601_z("2026-10-07T23:30Z"));
  CHECK_EQ(w.team_color, std::string("86cebc"));
  CHECK_EQ(w.team_record, std::string("26-18"));
}

static void test_nba_live() {
  // 8:27 in the 3rd, IND 74 MIN 73. IND is home.
  GameSnapshot g = hoops_event("fixtures/event_nba_live.json", kPacers);
  CHECK(g.valid);
  CHECK(g.sport == Sport::BASKETBALL);
  CHECK(g.state == GameState::IN);
  CHECK(g.team_home);
  CHECK_EQ(g.team_abbr, std::string("IND"));
  CHECK_EQ(g.opp_abbr, std::string("MIN"));
  CHECK_EQ(g.team_score, 74);
  CHECK_EQ(g.opp_score, 73);
  CHECK_EQ(g.period, 3);
  CHECK_EQ((int) g.nba.regulation, 4);
  CHECK(g.nba.phase == Phase::PLAY);
  CHECK_EQ(g.nba.series, std::string(""));
  CHECK_EQ((int) g.nba.team_wins, -1);
  CHECK_EQ(g.possession, 0);
  CHECK_EQ(g.team_timeouts, -1);  // not sent
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500-dark/scoreboard/ind.png&w=64&h=64"));
  CHECK_EQ(clock_text(g), std::string("8:27 3rd"));
  CHECK_EQ(basketball_series(g), std::string(""));
  const std::string play = "Jaden McDaniels makes 14-foot driving floating jump shot (Jonathan Kuminga assists)";
  CHECK_EQ(g.last_play, play);
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), play);
  TickerOptions clock = o;
  clock.clock = true;
  CHECK_EQ(status_text(g, clock, ""), "8:27 - 3rd | " + play);
  TickerOptions quiet = o;
  quiet.last_play = false;
  CHECK_EQ(status_text(g, quiet, ""), std::string("8:27 - 3rd"));

  // Under a minute ESPN sends tenths and no minutes.
  GameSnapshot c = hoops_event("fixtures/event_nba_clock.json", kThunder);
  CHECK_EQ(c.display_clock, std::string("12.3"));
  CHECK_EQ(clock_text(c), std::string("12.3 1st"));
  CHECK_EQ(status_text(c, o, ""), std::string("Brayden Burries makes 17-foot pullup jump shot"));

  // Between quarters: the clock row names the break, the ticker the records.
  GameSnapshot e = hoops_event("fixtures/event_nba_end.json", kThunder);
  CHECK(e.nba.phase == Phase::END_PERIOD);
  CHECK_EQ(clock_text(e), std::string("End 1st"));
  CHECK_EQ(status_text(e, o, ""), std::string("OKC 0-1 | MIL 0-1"));

  // Overtime, from the period number: OT is period 5 with four quarters.
  GameSnapshot ot = g;
  ot.period = 5;
  CHECK_EQ(clock_text(ot), std::string("8:27 OT"));
  ot.period = 6;
  CHECK_EQ(clock_text(ot), std::string("8:27 2OT"));
  ot.nba.phase = Phase::END_PERIOD;
  CHECK_EQ(clock_text(ot), std::string("End 2OT"));
  ot.period = 4;
  CHECK_EQ(clock_text(ot), std::string("End 4th"));
  ot.nba.phase = Phase::OTHER;
  ot.short_detail = "Delayed";
  CHECK_EQ(clock_text(ot), std::string("Delayed"));
}

static void test_wnba_live() {
  // A semifinal, ATL leads the series 1-0: 4.5 left in the 2nd.
  GameSnapshot g = hoops_event("fixtures/event_wnba_live.json", kLiberty);
  CHECK(g.state == GameState::IN);
  CHECK(!g.team_home);
  CHECK_EQ(g.team_abbr, std::string("NY"));
  CHECK_EQ(g.team_score, 54);
  CHECK_EQ(g.opp_score, 56);
  CHECK_EQ(clock_text(g), std::string("4.5 2nd"));
  CHECK_EQ(g.nba.series, std::string("ATL leads series 1-0"));
  CHECK_EQ((int) g.nba.team_wins, 0);
  CHECK_EQ((int) g.nba.opp_wins, 1);
  CHECK_EQ(basketball_series(g), std::string("ATL lead 1-0"));
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/wnba/500-dark/ny.png&w=64&h=64"));
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), std::string("Rhyne Howard makes 23-foot three point jumper (Jordin Canada assists)"));
  GameSnapshot a = hoops_event("fixtures/event_wnba_live.json", kDream);
  CHECK_EQ((int) a.nba.team_wins, 1);
  CHECK_EQ(basketball_series(a), std::string("ATL lead 1-0"));

  GameSnapshot h = hoops_event("fixtures/event_wnba_half.json", kLiberty);
  CHECK(h.nba.phase == Phase::HALFTIME);
  CHECK_EQ(clock_text(h), std::string("Halftime"));
  CHECK_EQ(status_text(h, o, ""), std::string("ATL leads series 1-0"));

  // The game went to overtime: period 5, "20.8 - OT".
  GameSnapshot ot = hoops_event("fixtures/event_wnba_ot.json", kDream);
  CHECK_EQ(ot.period, 5);
  CHECK_EQ(clock_text(ot), std::string("20.8 OT"));
  CHECK_EQ(basketball_series(ot), std::string("ATL lead 1-0"));

  // ESPN had not counted this game in the series yet when it went final.
  GameSnapshot f = hoops_event("fixtures/event_wnba_final.json", kDream);
  CHECK(f.state == GameState::POST);
  CHECK_EQ(f.team_score, 101);
  CHECK_EQ(f.opp_score, 98);
  CHECK_EQ(status_text(f, o, ""), std::string("Final/OT | ATL leads series 1-0"));
  CHECK_EQ(decide_splash(ot, f, true).text, std::string("ATL WINS!"));
  CHECK_EQ(decide_splash(ot, f, true).color, (uint32_t) 0xe31837);
  GameSnapshot ny_ot = hoops_event("fixtures/event_wnba_ot.json", kLiberty);
  GameSnapshot ny_f = hoops_event("fixtures/event_wnba_final.json", kLiberty);
  CHECK_EQ(decide_splash(ny_ot, ny_f, true).text, std::string("FINAL"));
  CHECK_EQ(decide_splash(ny_ot, ny_f, true, true).text, std::string("ATL WINS!"));

  // The short series line fits the 12-character situation row.
  GameSnapshot s = g;
  s.nba.team_wins = s.nba.opp_wins = 2;
  CHECK_EQ(basketball_series(s), std::string("Series 2-2"));
  s.team_abbr = "UTAH";
  s.nba.team_wins = 3;
  CHECK_EQ(basketball_series(s), std::string("UTAH up 3-2"));
}

static void test_nba_pre_post() {
  GameSnapshot p = hoops_event("fixtures/event_nba_pre.json", kBlazers);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ(p.odds, std::string("POR -14.5"));
  CHECK_EQ(p.over_under, std::string("216.5"));
  CHECK_EQ(p.tv, std::string("NBA TV"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"), std::string("Today 10:00 PM | POR -14.5 | O/U 216.5 | NBA TV | Moda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | Moda Center"));
  CHECK_EQ(basketball_series(p), std::string(""));

  GameSnapshot f = hoops_event("fixtures/event_nba_final.json", kPacers);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(f.team_score, 123);
  CHECK_EQ(f.opp_score, 112);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | IND 0-0 | MIN 1-0"));
  f.short_detail = "Final/OT";
  CHECK_EQ(status_text(f, o, ""), std::string("Final/OT | IND 0-0 | MIN 1-0"));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_nba_live.json"), Sport::BASKETBALL, kBlazers, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_nba_live.json"), Sport::BASKETBALL, kPacers, x, "401914124"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::BASKETBALL, kPacers, x));
}

static void test_nba_splash() {
  GameSnapshot prev = hoops_event("fixtures/event_nba_live.json", kPacers);
  auto after = [&](int us, int them, GameState st) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    c.state = st;
    c.completed = st == GameState::POST;
    return c;
  };
  // Baskets in the 3rd quarter never splash, whoever scores.
  CHECK_EQ(decide_splash(prev, after(3, 0, GameState::IN), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(0, 2, GameState::IN), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(2, 0, GameState::IN), true, true).text, std::string(""));
  Splash win = decide_splash(prev, after(10, 0, GameState::POST), true);
  CHECK_EQ(win.text, std::string("IND WINS!"));
  CHECK_EQ(win.color, (uint32_t) 0x0c2340);
  CHECK_EQ(decide_splash(prev, after(0, 10, GameState::POST), true).text, std::string("FINAL"));
  CHECK_EQ(decide_splash(prev, after(0, 10, GameState::POST), true, true).text, std::string("MIN WINS!"));
  GameSnapshot fin = after(10, 0, GameState::POST);
  CHECK_EQ(decide_splash(fin, fin, true).text, std::string(""));
  // The recorded final after the recorded live poll: IND won 123-112.
  GameSnapshot final_doc = hoops_event("fixtures/event_nba_final.json", kPacers);
  CHECK_EQ(decide_splash(prev, final_doc, true).text, std::string("IND WINS!"));
  CHECK_EQ(decide_splash(hoops_event("fixtures/event_nba_live.json", kWolves),
                         hoops_event("fixtures/event_nba_final.json", kWolves), true)
               .text,
           std::string("FINAL"));
}

// Men's college basketball, recorded 2026-10-07 in the off season: last
// season's finals (ESPN serves only the final state of a past game) and the
// coming season's opener (see fixtures/README.md).
static const uint32_t kMichigan = 130, kUConn = 41, kPenn = 219;

static void test_mcbb_urls() {
  CHECK_EQ(team_url(League::MCBB, 130),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/teams/130"));
  CHECK_EQ(event_url(League::MCBB, "401856600"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard/401856600"));
  CHECK_EQ(team_logo_url(League::MCBB, 130, "MICH"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/130.png&w=64&h=64"));
  const LeagueInfo *l = league_by_key("mcbb");
  CHECK(l != nullptr && l->per_event && !l->season_list && l->logo_by_id && l->sport == Sport::BASKETBALL);
  CHECK_EQ(std::string(league_path(League::MCBB)), std::string("mens-college-basketball"));
  int n = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    if (t.league != League::MCBB)
      continue;
    n++;
    if (t.espn_id == kMichigan)
      CHECK_EQ(team_option(t), std::string("NCAAM: Michigan Wolverines"));
  }
  CHECK(n >= 350);
}

static void test_mcbb() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_mcbb_mich.json"), s));
  CHECK_EQ(s.event_id, std::string("401925733"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-02T23:00Z"));
  CHECK_EQ(s.team_color, std::string("00274c"));

  GameSnapshot p = hoops_event("fixtures/event_mcbb_pre.json", kMichigan);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ((int) p.nba.regulation, 2);
  CHECK_EQ(p.opp_abbr, std::string("OAK"));
  CHECK_EQ(p.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/130.png&w=64&h=64"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Nov 2 6:00 PM"), std::string("Nov 2 6:00 PM | BTN | Crisler Center"));

  // The national championship: two halves, no series.
  GameSnapshot f = hoops_event("fixtures/event_mcbb_final.json", kUConn);
  CHECK(f.state == GameState::POST);
  CHECK_EQ((int) f.nba.regulation, 2);
  CHECK_EQ(f.team_score, 63);
  CHECK_EQ(f.opp_score, 69);
  // Both ranked going in: the rank before each abbreviation
  CHECK_EQ(status_text(f, o, ""), std::string("Final | #2 CONN 34-6 | #1 MICH 37-3"));
  CHECK_EQ(basketball_series(f), std::string(""));

  // Overtime is period 3 after two halves.
  GameSnapshot ot = hoops_event("fixtures/event_mcbb_ot.json", kPenn);
  CHECK_EQ(ot.period, 3);
  CHECK(ot.team_winner);
  CHECK_EQ(status_text(ot, o, "").rfind("Final/OT | PENN ", 0), (size_t) 0);

  // Past games come back final only, so the live clock is checked on a copy.
  GameSnapshot live = f;
  live.state = GameState::IN;
  live.nba.phase = Phase::PLAY;
  live.period = 1;
  live.display_clock = "12:34";
  CHECK_EQ(clock_text(live), std::string("12:34 1st"));
  live.period = 2;
  CHECK_EQ(clock_text(live), std::string("12:34 2nd"));
  live.period = 3;
  CHECK_EQ(clock_text(live), std::string("12:34 OT"));
  live.period = 4;
  CHECK_EQ(clock_text(live), std::string("12:34 2OT"));
  live.nba.phase = Phase::HALFTIME;
  live.period = 1;
  CHECK_EQ(clock_text(live), std::string("Halftime"));
  live.nba.phase = Phase::END_PERIOD;
  live.period = 2;
  CHECK_EQ(clock_text(live), std::string("End 2nd"));
}

// A real home run (TB @ NYY, 2026-10-07, Top 6th): ESPN types the play
// "Play Result", so the splash relies on the play text.
static void test_mlb_home_run() {
  const uint32_t kRays = 30, kYankees = 10;
  GameSnapshot before = mlb_event("fixtures/event_mlb_hr_before.json", kRays);
  GameSnapshot after = mlb_event("fixtures/event_mlb_hr.json", kRays);
  CHECK_EQ(after.play_type, std::string("Play Result"));
  CHECK(after.last_play.find("homered") != std::string::npos);
  CHECK_EQ(after.team_score - before.team_score, 2);
  CHECK_EQ(decide_splash(before, after, true).text, std::string("HOME RUN!"));
  GameSnapshot nyy_before = mlb_event("fixtures/event_mlb_hr_before.json", kYankees);
  GameSnapshot nyy_after = mlb_event("fixtures/event_mlb_hr.json", kYankees);
  CHECK_EQ(decide_splash(nyy_before, nyy_after, true).text, std::string("TB HOME RUN"));
}

// NHL single-event documents (see fixtures/README.md)
static const uint32_t kJets = 28, kAvalanche = 17, kPenguins = 16, kCapitals = 23, kOilers = 6, kHurricanes = 7;

static GameSnapshot nhl_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::HOCKEY, team, s, root_id(json)));
  return s;
}

static void test_nhl_urls() {
  CHECK_EQ(team_url(League::NHL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/teams/6"));
  CHECK_EQ(event_url(League::NHL, "401891830"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/scoreboard/401891830"));
  CHECK_EQ(team_logo_url(League::NHL, 129764, "UTAH"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nhl/500-dark/scoreboard/utah.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::NHL)), std::string("nhl"));
  const LeagueInfo *nhl = league_by_key("nhl");
  CHECK(nhl != nullptr && nhl->per_event && !nhl->season_list && nhl->linger_min == 30);
  CHECK(league_sport(League::NHL) == Sport::HOCKEY);
  int count = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league != League::NHL)
      continue;
    count++;
    if (kTeams[i].espn_id == kPenguins)
      CHECK_EQ(team_option(kTeams[i]), std::string("NHL: Pittsburgh Penguins"));
  }
  CHECK_EQ(count, 32);
}

static void test_nhl_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_nhl_edm.json"), s));
  CHECK_EQ(s.event_id, std::string("401892456"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-08T02:00Z"));
  CHECK_EQ(s.team_color, std::string("00205b"));
  CHECK_EQ(s.team_record, std::string("2-0-1"));  // W-L-OTL
}

static void test_nhl_live() {
  // COL @ WPG, 3:21 left in the 1st, a delay of game penalty the last play
  GameSnapshot g = nhl_event("fixtures/event_nhl_live.json", kJets);
  CHECK(g.valid);
  CHECK(g.sport == Sport::HOCKEY);
  CHECK(g.state == GameState::IN);
  CHECK(g.team_home);
  CHECK(!g.nhl.playoffs);
  CHECK_EQ(g.nhl.series, std::string(""));
  CHECK_EQ(g.team_abbr, std::string("WPG"));
  CHECK_EQ(g.opp_abbr, std::string("COL"));
  CHECK_EQ(g.team_score, 0);
  CHECK_EQ(g.opp_score, 0);
  CHECK_EQ(g.period, 1);
  CHECK_EQ(g.possession, 0);
  CHECK_EQ(g.team_timeouts, -1);  // not sent
  CHECK_EQ(g.down_distance, std::string(""));
  CHECK_EQ(g.team_record, std::string("2-0-1"));
  CHECK_EQ(g.opp_record, std::string("2-0-0"));
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nhl/500-dark/scoreboard/wpg.png&w=64&h=64"));
  CHECK_EQ(clock_text(g), std::string("3:21 1st"));
  const std::string penalty = "Delaying Game - Puck over glass served by Fedor Svechkov";
  CHECK_EQ(g.last_play, penalty);
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), penalty);
  TickerOptions with_clock;
  CHECK_EQ(status_text(g, with_clock, ""), "3:21 - 1st | " + penalty);
  TickerOptions quiet = o;
  quiet.last_play = false;
  CHECK_EQ(status_text(g, quiet, ""), std::string("3:21 - 1st"));
  GameSnapshot nameless = g;  // seen live: a goal before ESPN had the scorer
  nameless.last_play = ", assists: Neal Pionk (1), Dylan Samberg (1)";
  CHECK_EQ(status_text(nameless, o, ""), std::string("assists: Neal Pionk (1), Dylan Samberg (1)"));

  // The same game from the visitors' side
  GameSnapshot c = nhl_event("fixtures/event_nhl_live.json", kAvalanche);
  CHECK(!c.team_home);
  CHECK_EQ(c.team_abbr, std::string("COL"));
  CHECK_EQ(c.team_record, std::string("2-0-0"));

  // Intermission: PIT @ WSH after the 1st, the ticker shows the records
  GameSnapshot e = nhl_event("fixtures/event_nhl_end.json", kPenguins);
  CHECK(e.state == GameState::IN);
  CHECK(!e.team_home);
  CHECK_EQ(e.team_score, 1);
  CHECK_EQ(e.opp_score, 2);
  CHECK_EQ(clock_text(e), std::string("End of 1st"));
  CHECK_EQ(status_text(e, o, ""), std::string("PIT 2-1-0 | WSH 1-1-0"));
  CHECK_EQ(status_text(e, quiet, ""), std::string("PIT 2-1-0 | WSH 1-1-0"));
  GameSnapshot w = nhl_event("fixtures/event_nhl_end.json", kCapitals);
  CHECK_EQ(w.team_score, 2);
  CHECK_EQ(status_text(w, o, ""), std::string("WSH 1-1-0 | PIT 2-1-0"));

  // Overtime, then the shootout (regular season) or more overtime (playoffs)
  GameSnapshot ot = g;
  ot.period = 4;
  ot.display_clock = "3:10";
  ot.short_detail = "3:10 - OT";
  CHECK_EQ(clock_text(ot), std::string("3:10 OT"));
  ot.display_clock = "0:00";
  ot.short_detail = "End of OT";
  CHECK_EQ(clock_text(ot), std::string("End of OT"));
  GameSnapshot so = ot;
  so.period = 5;
  so.short_detail = "Shootout";
  CHECK_EQ(clock_text(so), std::string("SO"));
  so.short_detail = "0:00 - SO";
  CHECK_EQ(clock_text(so), std::string("SO"));
  GameSnapshot ot2 = so;
  ot2.nhl.playoffs = true;
  ot2.display_clock = "12:00";
  ot2.short_detail = "12:00 - 2OT";
  CHECK_EQ(clock_text(ot2), std::string("12:00 2OT"));
  ot2.period = 6;
  CHECK_EQ(clock_text(ot2), std::string("12:00 3OT"));
}

static void test_nhl_pre_post() {
  GameSnapshot p = nhl_event("fixtures/event_nhl_pre.json", kOilers);
  CHECK(p.state == GameState::PRE);
  CHECK(!p.team_home);
  CHECK_EQ(p.odds, std::string("EDM -135"));
  CHECK_EQ(p.over_under, std::string("6.5"));
  CHECK_EQ(p.tv, std::string("ESPN+"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"), std::string("Today 10:00 PM | EDM -135 | O/U 6.5 | ESPN+ | Honda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | Honda Center"));

  // Tonight's final, and the win splash from the intermission document of the same game
  GameSnapshot f = nhl_event("fixtures/event_nhl_final.json", kCapitals);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(f.team_score, 5);
  CHECK_EQ(f.opp_score, 3);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | WSH 2-1-0 | PIT 2-2-0"));
  GameSnapshot w = nhl_event("fixtures/event_nhl_end.json", kCapitals);
  CHECK_EQ(decide_splash(w, f, true).text, std::string("WSH WINS!"));
  CHECK_EQ(decide_splash(w, f, true).color, (uint32_t) 0xd71830);
  GameSnapshot lost_prev = nhl_event("fixtures/event_nhl_end.json", kPenguins);
  GameSnapshot lost = nhl_event("fixtures/event_nhl_final.json", kPenguins);
  CHECK_EQ(decide_splash(lost_prev, lost, true).text, std::string(""));
  CHECK_EQ(decide_splash(lost_prev, lost, true, true).text, std::string("WSH WINS!"));

  // A regular-season shootout: CAR won it 5-4
  GameSnapshot so = nhl_event("fixtures/event_nhl_final_so.json", kPenguins);
  CHECK(so.state == GameState::POST);
  CHECK(!so.team_winner);
  CHECK_EQ(so.period, 5);
  CHECK_EQ(so.team_score, 4);
  CHECK_EQ(so.opp_score, 5);
  CHECK_EQ(clock_text(so), std::string("Final/SO"));
  CHECK_EQ(status_text(so, o, ""), std::string("Final/SO | PIT 32-17-15 | CAR 41-17-6"));

  // A playoff game in double overtime: the series line stands in for records
  GameSnapshot po = nhl_event("fixtures/event_nhl_final_2ot.json", kHurricanes);
  CHECK(po.nhl.playoffs);
  CHECK(po.team_winner);
  CHECK_EQ(po.nhl.series, std::string("CAR leads series 2-0"));
  CHECK_EQ(status_text(po, o, ""), std::string("Final/2OT | CAR leads series 2-0"));
  GameSnapshot pre = po;
  pre.state = GameState::PRE;
  pre.odds = pre.over_under = pre.tv = "";
  CHECK_EQ(status_text(pre, o, "Today 7:00 PM"), std::string("Today 7:00 PM | CAR leads series 2-0 | Lenovo Center"));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_nhl_live.json"), Sport::HOCKEY, kPenguins, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_nhl_live.json"), Sport::HOCKEY, kJets, x, "401892456"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::HOCKEY, kJets, x));
}

static void test_nhl_splash() {
  GameSnapshot prev = nhl_event("fixtures/event_nhl_live.json", kJets);
  auto after = [&](int us, int them) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0), true).text, std::string("GOAL!"));
  CHECK_EQ(decide_splash(prev, after(1, 0), false).color, (uint32_t) 0x002d62);
  CHECK_EQ(decide_splash(prev, after(0, 1), true).text, std::string("COL GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), true).color, (uint32_t) 0x860038);
  CHECK_EQ(decide_splash(prev, after(0, 1), false).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(0, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(after(1, 0), prev, true).text, std::string(""));  // goal taken back on review
  // Live modes: neither side is ours
  CHECK_EQ(decide_splash(prev, after(1, 0), true, true).text, std::string("WPG GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), true, true).text, std::string("COL GOAL"));
  // The win, including a shootout decided between two polls
  GameSnapshot fin = after(1, 0);
  fin.state = GameState::POST;
  fin.completed = true;
  CHECK_EQ(decide_splash(prev, fin, true).text, std::string("WPG WINS!"));
  GameSnapshot lost = after(0, 1);
  lost.state = GameState::POST;
  lost.completed = true;
  CHECK_EQ(decide_splash(prev, lost, true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, lost, true, true).text, std::string("COL WINS!"));
  GameSnapshot other = after(1, 0);
  other.event_id = "1";
  CHECK_EQ(decide_splash(prev, other, true).text, std::string(""));
  GameSnapshot pre = prev;
  pre.state = GameState::PRE;
  CHECK_EQ(decide_splash(pre, after(1, 0), true).text, std::string(""));
}

// ESPN's team endpoint keeps naming a finished game for hours (PIT's still
// named the 10/7 final at 11 AM Eastern on 10/8). Leagues without a season
// list find the next game in the coming days' scoreboards.
static void test_lookahead() {
  Schedule pit;
  CHECK(parse_team_str(slurp("fixtures/team_nhl_pit_post.json"), pit));
  CHECK_EQ(pit.event_id, std::string("401891830"));
  CHECK(pit.next_final);
  Schedule atl;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_atl.json"), atl));
  CHECK(!atl.next_final);  // its next event was in progress
  Schedule dal;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), dal));
  CHECK(!dal.next_final);

  Schedule next;
  CHECK(parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 16, next));
  CHECK_EQ(next.event_id, std::string("401892467"));  // PIT @ CBJ
  CHECK_EQ(next.kickoff_epoch, parse_iso8601_z("2026-10-09T23:00Z"));
  Schedule none;
  CHECK(!parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 17, none));  // COL is off
  CHECK(!parse_team_game_str(slurp("fixtures/day_mlb_20261009.json"), 15, none));   // no games at all
  // Game 2 of a doubleheader, after game 1's final.
  const char *dh = R"({"events":[
    {"id":"1","date":"2026-07-04T17:05Z","status":{"type":{"state":"post"}},
     "competitions":[{"competitors":[{"id":"15"},{"id":"19"}]}]},
    {"id":"2","date":"2026-07-04T22:10Z","status":{"type":{"state":"pre"}},
     "competitions":[{"competitors":[{"id":"19"},{"id":"15"}]}]}]})";
  CHECK(parse_team_game_str(dh, 15, next));
  CHECK_EQ(next.event_id, std::string("2"));
  // The scoreboard can still call game 1 live after the panel saw it end.
  const char *dh_live = R"({"events":[
    {"id":"1","date":"2026-07-04T17:05Z","status":{"type":{"state":"in"}},
     "competitions":[{"competitors":[{"id":"15"},{"id":"19"}]}]},
    {"id":"2","date":"2026-07-04T22:10Z","status":{"type":{"state":"pre"}},
     "competitions":[{"competitors":[{"id":"19"},{"id":"15"}]}]}]})";
  CHECK(parse_team_game_str(dh_live, 15, next, "1"));
  CHECK_EQ(next.event_id, std::string("2"));
  CHECK(!parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 16, none, "401892467"));
  // A document that does not parse is a failed read, not an off day.
  std::string bad = "{\"events\":[";
  CHECK_EQ(parse_team_game(bad, 16, std::string(), none), -1);
  std::string off = slurp("fixtures/day_mlb_20261009.json");
  CHECK_EQ(parse_team_game(off, 15, std::string(), none), 0);

  // Mid-game, the team endpoint already names the next game; today's
  // scoreboard still has the one on now (LAD @ MIL, Bot 3rd, 2026-10-12).
  Schedule mil;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_mil_live.json"), mil));
  CHECK_EQ(mil.event_id, std::string("401908032"));  // MIL @ LAD, 10/14
  Schedule playing;
  CHECK(parse_team_game_str(slurp("fixtures/day_mlb_20261012.json"), 8, playing));
  CHECK_EQ(playing.event_id, std::string("401908031"));
  CHECK(today_game_first(mil, playing));
  // CLE's game tonight hasn't started: both name it, nothing changes.
  Schedule cle, cle_today;
  cle.event_id = "401908023";
  cle.kickoff_epoch = parse_iso8601_z("2026-10-13T00:00Z");
  CHECK(parse_team_game_str(slurp("fixtures/day_mlb_20261012.json"), 5, cle_today));
  CHECK(!today_game_first(cle, cle_today));
  // No game today, or no next game named at all.
  CHECK(!today_game_first(mil, Schedule{}));
  Schedule unnamed;
  CHECK(today_game_first(unnamed, playing));
  // Doubleheader: game 1 on now, the team endpoint names game 2.
  Schedule game2;
  game2.event_id = "2";
  game2.kickoff_epoch = parse_iso8601_z("2026-07-04T22:10Z");
  CHECK(parse_team_game_str(dh_live, 15, next));
  CHECK(today_game_first(game2, next));

  // Soccer jumps the same way: CRU @ BRA at 25' (Brazil's Serie A, the same
  // documents as MLS and the Premier League, 2026-10-12).
  Schedule cru;
  CHECK(parse_team_str(slurp("fixtures/team_bra_cru_live.json"), cru));
  CHECK_EQ(cru.event_id, std::string("401841276"));  // CRU @ GRE, 10/18
  Schedule cru_now;
  CHECK(parse_team_game_str(slurp("fixtures/day_bra_20261012.json"), 2022, cru_now));
  CHECK_EQ(cru_now.event_id, std::string("401841260"));
  CHECK(today_game_first(cru, cru_now));
  // CHA's game that day is over: nothing on now.
  CHECK(!parse_team_game_str(slurp("fixtures/day_bra_20261012.json"), 9318, none));

  // College football too: FLA @ TEX at halftime, 2026-10-17. Texas's team
  // endpoint names next week's game; its conference's scoreboard (the group
  // the team endpoint gives) has the one on now.
  Schedule tex;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_tex_live.json"), tex));
  CHECK_EQ(tex.event_id, std::string("401856732"));  // MISS @ TEX, 10/24
  CHECK_EQ(tex.group, 8u);
  CHECK_EQ(team_day_url(League::NCAA, tex.group, parse_iso8601_z("2026-10-17T17:40Z") - 4 * 3600),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=8&dates=20261017"));
  Schedule tex_now;
  CHECK(parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 251, tex_now));
  CHECK_EQ(tex_now.event_id, std::string("401856724"));
  CHECK(today_game_first(tex, tex_now));
  // Ole Miss plays later that day: both name the same game, nothing changes.
  Schedule miss, miss_today;
  CHECK(parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 145, miss_today));
  miss.event_id = miss_today.event_id;
  miss.kickoff_epoch = miss_today.kickoff_epoch;
  CHECK(!today_game_first(miss, miss_today));
  // A game the panel saw finish is passed over even while ESPN lists it live,
  // and a team that is off that day (Iowa) has nothing on now.
  CHECK(!parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 251, none, "401856724"));
  CHECK(!parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 2294, none));

  CHECK_EQ(team_day_url(League::NHL, 0, parse_iso8601_z("2026-10-09T15:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/scoreboard?dates=20261009"));
  CHECK_EQ(team_day_url(League::MCBB, 8, parse_iso8601_z("2026-12-06T18:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=8&dates=20261206"));
  CHECK_EQ(team_day_url(League::MLS, 0, parse_iso8601_z("2026-10-15T00:30Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/usa.1/scoreboard?dates=20261014"));
  CHECK_EQ(team_day_url(League::EPL, 0, parse_iso8601_z("2026-10-21T19:00Z"), "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/scoreboard?dates=20261021"));
}

int run_football_golden();  // golden_football.cpp
int run_set_tests(int &checks);  // test_set.cpp
int run_alert_tests(int &checks);  // test_alerts.cpp
int run_bet_tests(int &checks);  // test_bets.cpp
int run_watch_tests(int &checks);  // test_watch.cpp
int run_player_tests(int &checks);  // test_players.cpp
int run_mlbstats_tests(int &checks);  // test_mlbstats.cpp
int run_hockeytech_tests(int &checks);  // test_hockeytech.cpp

static void test_logo_lru() {
  using espn::LogoSlot;
  // Oldest idle, off-screen slot goes first.
  CHECK_EQ(espn::pick_evict({{300, false, false}, {100, false, false}, {200, false, false}}, 1000), 1);
  // On screen or still downloading: never dropped, even when oldest.
  CHECK_EQ(espn::pick_evict({{100, false, true}, {150, true, false}, {200, false, false}}, 1000), 2);
  // Nothing droppable.
  CHECK_EQ(espn::pick_evict({{100, true, false}, {200, false, true}}, 1000), -1);
  CHECK_EQ(espn::pick_evict({}, 1000), -1);
  // Age counts across the millis() wrap (49.7 days): a logo used just before
  // it is older than one used just after, though its stamp is larger.
  CHECK_EQ(espn::pick_evict({{0x00000100u, false, false}, {0xFFFFFF00u, false, false}}, 0x00000200u), 1);
  CHECK_EQ(espn::pick_evict({{0xFFFFFF00u, false, false}, {0x00000100u, false, false}}, 0x00000200u), 0);
  // A failed logo is due once its wait has passed, including across the millis() wrap.
  CHECK(!espn::logo_retry_due(1000, 30000, 1000 + 30000 - 1));
  CHECK(espn::logo_retry_due(1000, 30000, 1000 + 30000));
  CHECK(espn::logo_retry_due(0xFFFFF000u, 30000, 0xFFFFF000u + 30000));
  CHECK(!espn::logo_retry_due(0xFFFFF000u, 30000, 0xFFFFF000u + 29999));
  // A board with new logos waits for both, then goes up; a cap ends the wait
  // for a logo that is slow, and one that failed is not waited for at all.
  CHECK(espn::logo_hold_over(false, false, 1000, 1000));
  CHECK(!espn::logo_hold_over(true, false, 1000, 1000));
  CHECK(!espn::logo_hold_over(false, true, 1000, 1000 + espn::LOGO_HOLD_MS - 1));
  CHECK(espn::logo_hold_over(true, true, 1000, 1000 + espn::LOGO_HOLD_MS));
  CHECK(espn::logo_hold_over(true, false, 0xFFFFF000u, 0xFFFFF000u + espn::LOGO_HOLD_MS));
  CHECK(!espn::logo_hold_over(true, false, 0xFFFFF000u, 0xFFFFF000u + espn::LOGO_HOLD_MS - 1));
  // A dark logo that 404s falls back to the plain one, the URL dark_logo made it from.
  std::string plain = "https://a.espncdn.com/i/teamlogos/soccer/500/436.png";
  std::string dark = espn::dark_logo(plain);
  CHECK_EQ(espn::plain_logo_url(dark),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500/436.png&w=64&h=64"));
  CHECK_EQ(espn::plain_logo_url(espn::dark_logo("https://a.espncdn.com/i/teamlogos/nfl/500/scoreboard/dal.png")),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500/scoreboard/dal.png&w=64&h=64"));
  CHECK_EQ(espn::plain_logo_url("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500/436.png&w=64&h=64"),
           std::string());
}

// Ties in the logo cache go to the first slot, and the plain-logo fallback
// only rewrites a dark logo's path.
static void test_logo_lru_edges() {
  using espn::LogoSlot;
  CHECK_EQ(espn::pick_evict({{500, false, false}, {200, true, false}, {500, false, false}}, 1000), 0);
  CHECK_EQ(espn::pick_evict({{0, false, false}}, 1000), 0);
  CHECK_EQ(espn::pick_evict({{0, false, true}}, 1000), -1);
  CHECK_EQ(espn::plain_logo_url("https://a.espncdn.com/i/teamlogos/nfl/500-dark/dal.png"),
           std::string("https://a.espncdn.com/i/teamlogos/nfl/500/dal.png"));
  CHECK_EQ(espn::plain_logo_url("https://a.espncdn.com/i/teamlogos/nfl/500/dal.png"), std::string());
  CHECK_EQ(espn::plain_logo_url(""), std::string());
  CHECK_EQ(espn::plain_logo_url(espn::dark_logo("")), std::string());  // no logo, no fallback
}

// The wait before a failed logo is asked for again: per logo, growing with each
// failure in a row, longer after a refusal, cleared when it loads.
static void test_logo_backoff() {
  uint8_t fails = 0;
  uint32_t wait = 0;
  // Ordinary failures: 30 s doubling to 10 min, jittered into the upper half.
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 1);
  CHECK_EQ(wait, 15000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(wait, 30000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(wait, 60000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 4);
  CHECK_EQ(wait, 120000u);
  for (int i = 0; i < 20; i++)
    espn::logo_failed(fails, wait, false, 0, 300000);
  CHECK_EQ(wait, 600000u);
  // Each failure waits at least as long as the one before it, with no jitter.
  uint8_t f = 0;
  uint32_t w = 0, prev_floor = 0;
  for (int i = 0; i < 12; i++) {
    espn::logo_failed(f, w, false, 0, 0);
    CHECK(w >= prev_floor);
    prev_floor = w;
  }
  // A 403 or 429 starts at 5 min and stops at an hour.
  fails = 0;
  espn::logo_failed(fails, wait, true, 0, 0);
  CHECK_EQ(wait, 150000u);
  espn::logo_failed(fails, wait, true, 0, 300000);
  CHECK_EQ(wait, 600000u);
  for (int i = 0; i < 20; i++)
    espn::logo_failed(fails, wait, true, 0, 1800000);
  CHECK_EQ(wait, 3600000u);
  // Retry-After longer than the schedule is honored, up to the hour.
  fails = 0;
  espn::logo_failed(fails, wait, true, 900000, 0);
  CHECK_EQ(wait, 900000u);
  fails = 0;
  espn::logo_failed(fails, wait, true, 9000000, 0);
  CHECK_EQ(wait, 3600000u);
  // A refusal after plain failures lands on the refusal schedule for its place in the run.
  fails = 2;
  espn::logo_failed(fails, wait, true, 0, 300000);
  CHECK_EQ(wait, 900000u);
  // The count saturates instead of wrapping back to a short wait.
  fails = 255;
  wait = 0;
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 255);
  CHECK_EQ(wait, 300000u);
  // A logo that loads clears its own wait and count, and the next failure starts over.
  espn::logo_loaded(fails, wait);
  CHECK_EQ(fails, 0);
  CHECK_EQ(wait, 0u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 1);
  CHECK_EQ(wait, 15000u);
  // Two logos keep separate runs: one failing for the fifth time does not slow the other's first retry.
  uint8_t fails_a = 4, fails_b = 0;
  uint32_t wait_a = 0, wait_b = 0;
  espn::logo_failed(fails_a, wait_a, false, 0, 0);
  espn::logo_failed(fails_b, wait_b, false, 0, 0);
  CHECK_EQ(wait_a, 240000u);
  CHECK_EQ(wait_b, 15000u);
}

// Counts what the parsers allocate through the hook the firmware points at PSRAM
struct CountingAllocator : ArduinoJson::Allocator {
  int calls = 0;
  int live = 0;
  void *allocate(size_t size) override {
    calls++;
    live++;
    return malloc(size);
  }
  void deallocate(void *ptr) override {
    if (ptr != nullptr)
      live--;
    free(ptr);
  }
  void *reallocate(void *ptr, size_t size) override {
    if (ptr == nullptr) {
      calls++;
      live++;
    }
    return realloc(ptr, size);
  }
};

static void test_json_allocator() {
  CountingAllocator counting;
  espn::set_json_allocator(&counting);
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, s));
  CHECK_EQ(s.team_score, 3);
  Schedule sch;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), sch));
  CHECK(counting.calls > 0);
  CHECK_EQ(counting.live, 0);  // every document freed its memory
  espn::set_json_allocator(nullptr);
  int before = counting.calls;
  GameSnapshot again;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, again));
  CHECK_EQ(counting.calls, before);  // back on the default heap
}

static void test_timezone_index() {
  // Names first: the app sends one, so a reordered list can't pick a wrong zone.
  CHECK_EQ(espn::timezone_index("US Central"), 1);
  CHECK_EQ(espn::timezone_index("America/Chicago"), 1);
  CHECK_EQ(espn::timezone_index("Australia Eastern"), 12);
  CHECK_EQ(espn::timezone_index("Australia/Sydney"), 12);
  CHECK_EQ(espn::timezone_index("UTC"), 13);
  // Positions still work for the device page, which is built from this list.
  CHECK_EQ(espn::timezone_index("0"), 0);
  CHECK_EQ(espn::timezone_index("13"), 13);
  CHECK_EQ(espn::timezone_index("14"), -1);
  CHECK_EQ(espn::timezone_index("99999999999999999999"), -1);
  // Nothing close enough to guess at.
  CHECK_EQ(espn::timezone_index("us central"), -1);
  CHECK_EQ(espn::timezone_index("America/Detroit"), -1);
  CHECK_EQ(espn::timezone_index("-1"), -1);
  CHECK_EQ(espn::timezone_index("1a"), -1);
  CHECK_EQ(espn::timezone_index(""), -1);
  CHECK_EQ(espn::timezone_index(nullptr), -1);
  // Every row answers to its own name and IANA name.
  for (size_t i = 0; i < espn::kTimezoneCount; i++) {
    CHECK_EQ(espn::timezone_index(espn::kTimezones[i].name), (int) i);
    CHECK_EQ(espn::timezone_index(espn::kTimezones[i].iana), (int) i);
  }
}

// Recorded 2026-10-08 from the 2025-26 postseason, see fixtures/README.md
static void test_postseason() {
  TickerOptions all;
  // CFP quarterfinal at the Rose Bowl, a neutral site, from both sides
  std::string qf = slurp("fixtures/scoreboard_ncaa_cfp_quarterfinals.json");
  GameSnapshot ala;
  CHECK(parse_scoreboard_str(qf, "401769072", 333, ala));
  CHECK(ala.state == GameState::POST);
  CHECK(ala.completed);
  CHECK_EQ(ala.team_score, 3);
  CHECK_EQ(ala.opp_score, 38);
  CHECK(!ala.team_winner);
  CHECK_EQ(ala.venue, std::string("Rose Bowl"));
  CHECK_EQ(status_text(ala, all, ""), std::string("Final | #9 ALA 11-4 | #1 IU 14-0"));
  GameSnapshot iu;
  CHECK(parse_scoreboard_str(qf, "401769072", 84, iu));
  CHECK(iu.team_winner);
  CHECK_EQ(iu.opp_abbr, std::string("ALA"));
  // The My team poll filters by our conference. ESPN lists a game under
  // both teams' conferences, so SEC (8) finds Alabama's Rose Bowl.
  CHECK_EQ(scoreboard_url(League::NCAA, 8, ala.kickoff_epoch),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard"
                       "?groups=8&dates=20260101"));

  // National championship: 7:30 PM Eastern is 00:30 UTC the next day
  std::string final_json = slurp("fixtures/scoreboard_ncaa_cfp_final.json");
  GameSnapshot mia;
  CHECK(parse_scoreboard_str(final_json, "401769076", 2390, mia));
  CHECK(mia.completed);
  CHECK_EQ(mia.team_score, 21);
  CHECK_EQ(mia.opp_score, 27);
  CHECK(!mia.team_winner);
  CHECK(scoreboard_url(League::NCAA, 1, mia.kickoff_epoch).find("&dates=20260119") != std::string::npos);

  // Conference title game that went to overtime
  GameSnapshot duke;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_ncaa_conf_titles.json"), "401777328", 150, duke));
  CHECK(duke.completed);
  CHECK(duke.team_winner);
  CHECK_EQ(duke.short_detail, std::string("Final/OT"));
  CHECK_EQ(status_text(duke, all, ""), std::string("Final | DUKE 8-5 | #17 UVA 10-3"));

  // NFL wild card and the Super Bowl
  GameSnapshot chi;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl_wild_card.json"), "401772981", 3, chi));
  CHECK(chi.completed);
  CHECK(chi.team_winner);
  CHECK_EQ(chi.team_score, 31);
  CHECK_EQ(chi.opp_score, 27);
  GameSnapshot sea;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl_super_bowl.json"), "401772988", 26, sea));
  CHECK(sea.completed);
  CHECK(sea.team_winner);
  CHECK_EQ(sea.opp_abbr, std::string("NE"));
  CHECK_EQ(sea.venue, std::string("Levi's Stadium"));
  CHECK(scoreboard_url(League::NFL, 0, sea.kickoff_epoch).find("?dates=20260208") != std::string::npos);

  // Game names from competitions[0].notes[0].headline
  CHECK_EQ(ala.game_name, std::string("Rose Bowl"));
  CHECK_EQ(mia.game_name, std::string("CFP Championship"));
  CHECK_EQ(duke.game_name, std::string("ACC Championship"));
  CHECK_EQ(chi.game_name, std::string("NFC Wild Card"));
  CHECK_EQ(sea.game_name, std::string("Super Bowl LX"));
}

// A college team's rank: ESPN's curatedRank on each side of the followed game
// (99 when unranked), in the ticker's pre-game matchup and its records lines.
static void test_ranks() {
  // The Big Ten title game: #2 IU at #1 OSU. The ACC's: DUKE at #17 UVA.
  const std::string titles = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  GameSnapshot iu;
  CHECK(parse_scoreboard_str(titles, "401777353", 84, iu));
  CHECK_EQ((int) iu.team_rank, 2);
  CHECK_EQ((int) iu.opp_rank, 1);
  GameSnapshot osu;
  CHECK(parse_scoreboard_str(titles, "401777353", kOhioState, osu));
  CHECK_EQ((int) osu.team_rank, 1);
  CHECK_EQ((int) osu.opp_rank, 2);
  TickerOptions o;
  CHECK_EQ(status_text(iu, o, ""), std::string("Final | #2 IU 13-0 | #1 OSU 12-1"));
  GameSnapshot duke;
  CHECK(parse_scoreboard_str(titles, "401777328", 150, duke));
  CHECK_EQ((int) duke.team_rank, 0);  // 99: unranked
  CHECK_EQ((int) duke.opp_rank, 17);

  // Before kickoff: the matchup with each side's rank, the unranked one bare.
  const std::string sat = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot miss;
  CHECK(parse_scoreboard_str(sat, "401856661", 145, miss));
  TickerOptions quiet;
  quiet.odds = false;
  CHECK_EQ(status_text(miss, quiet, "Sat 7:30 PM"),
           std::string("Liberty Mutual Music City Kickoff | #9 MISS vs #24 LOU | Sat 7:30 PM | Nissan Stadium"));
  GameSnapshot fsu;
  CHECK(parse_scoreboard_str(sat, "401858212", 52, fsu));
  CHECK_EQ(espn::detail::ranked_matchup(fsu), std::string("FSU vs #19 SMU"));
  // Neither ranked: no matchup, the line as before.
  GameSnapshot fiu;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_live.json"), Sport::FOOTBALL, 2229, fiu, "401871053"));
  CHECK_EQ((int) fiu.team_rank, 0);
  CHECK_EQ((int) fiu.opp_rank, 0);
  CHECK_EQ(espn::detail::ranked_matchup(fiu), std::string(""));
  // Live, the ticker has no matchup; halftime's records line keeps the ranks.
  GameSnapshot ore;
  CHECK(parse_scoreboard_str(sat, kLiveEvent, kOregon, ore));
  CHECK_EQ((int) ore.team_rank, 2);
  CHECK(status_text(ore, o, "").find('#') == std::string::npos);
  ore.short_detail = "Halftime";
  ore.status_name = "STATUS_HALFTIME";
  ore.period = 2;
  CHECK_EQ(status_text(ore, o, ""), std::string("Halftime | #2 ORE 0-0 | BOIS 0-0"));

  // A pro league's competitors carry 99 as well: no rank, no matchup.
  GameSnapshot sea;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl.json"), kNflEvent, kSeahawks, sea));
  CHECK_EQ((int) sea.team_rank, 0);
  CHECK_EQ((int) sea.opp_rank, 0);
  CHECK(status_text(sea, o, "Sun 1:00 PM").find('#') == std::string::npos);

  // Men's college basketball: the national title game between #2 and #1.
  GameSnapshot conn = hoops_event("fixtures/event_mcbb_final.json", kUConn);
  CHECK_EQ((int) conn.team_rank, 2);
  CHECK_EQ((int) conn.opp_rank, 1);
  GameSnapshot pre = conn;
  pre.state = GameState::PRE;
  CHECK_EQ(status_text(pre, quiet, "Mon 9:20 PM").rfind("#2 CONN vs #1 MICH | Mon 9:20 PM", 0), (size_t) 0);

  // The finals rotation and the later-today cards keep each side's rank.
  std::vector<LiveGame> live;
  std::vector<Final> finals;
  CHECK(parse_live_games(titles, live, nullptr, &finals));
  CHECK(!finals.empty());
  if (!finals.empty()) {
    CHECK_EQ((int) finals[0].away_rank, 2);
    CHECK_EQ((int) finals[0].home_rank, 1);
    Final f = finals[0];
    f.league = (uint8_t) League::NCAA;
    GameSnapshot card = final_snapshot(f);
    CHECK_EQ((int) card.team_rank, 2);
    CHECK_EQ((int) card.opp_rank, 1);
  }
  std::vector<LiveGame> on;
  std::vector<Pending> later;
  int64_t next = 0;
  const std::string day = slurp("fixtures/day_ncaa_20261017.json");
  CHECK(parse_live_games(day, on, &next, nullptr, &later));
  bool found = false;
  for (const auto &p : later) {
    if (p.event_id != "401856723")
      continue;
    found = true;
    CHECK_EQ((int) p.away_rank, 6);   // ALA
    CHECK_EQ((int) p.home_rank, 14);  // at TENN
    Pending q = p;
    q.league = (uint8_t) League::NCAA;
    GameSnapshot card = pending_snapshot(q);
    CHECK_EQ((int) card.team_rank, 6);
    CHECK_EQ((int) card.opp_rank, 14);
  }
  CHECK(found);
}

// Where the panel has room for a rank: before an abbreviation on one panel (32
// px), before a record under a logo on two (36 px). Spleen 8 is 5 px a
// character for every glyph the panel draws (the font's BDF), so the panel's
// measure of label width over glyph width comes to 6 and 7 characters.
static void test_rank_width() {
  const size_t abbr = 32 / 5, rec = 36 / 5;
  CHECK_EQ(abbr, (size_t) 6);
  CHECK_EQ(rec, (size_t) 7);
  CHECK_EQ(rank_text(5), std::string("#5"));
  CHECK_EQ(rank_text(0), std::string(""));
  CHECK_EQ(rank_label(5, "OSU", abbr), std::string("#5 OSU"));
  CHECK_EQ(rank_label(25, "IU", abbr), std::string("#25 IU"));
  CHECK_EQ(rank_label(12, "OSU", abbr), std::string("OSU"));  // "#12 OSU" is 7: the rank drops
  CHECK_EQ(rank_label(5, "MICH", abbr), std::string("MICH"));
  CHECK_EQ(rank_label(0, "OSU", abbr), std::string("OSU"));
  CHECK_EQ(rank_label(5, "", abbr), std::string(""));  // no team yet
  CHECK_EQ(rank_label(5, "9-1", rec), std::string("#5 9-1"));
  CHECK_EQ(rank_label(12, "9-1", rec), std::string("#12 9-1"));
  CHECK_EQ(rank_label(9, "10-1", rec), std::string("#9 10-1"));
  CHECK_EQ(rank_label(12, "10-1", rec), std::string("10-1"));  // 8: the record stays
  CHECK_EQ(rank_label(14, "18-6", rec), std::string("18-6"));
  CHECK_EQ(rank_label(5, "9-1", 0), std::string("9-1"));  // a label the panel couldn't measure
}

// Standing next to pro teams: ESPN's playoffSeed and standingSummary from the
// team endpoint, cut short, for the followed team's record's place.
static void test_standings() {
  struct Doc {
    const char *file;
    int seed;
    const char *place;
  };
  const Doc docs[] = {
      {"fixtures/team_mlb_atl.json", 3, "1st NLE"},       {"fixtures/team_mlb_mil_live.json", 1, "1st NLC"},
      {"fixtures/team_nba_ind.json", 5, "2nd Cent"},      {"fixtures/team_nhl_edm.json", 4, "2nd Pac"},
      {"fixtures/team_nhl_pit_post.json", 8, "3rd Metro"}, {"fixtures/team_wnba_ny.json", 4, "4th East"},
      {"fixtures/team_epl_ars.json", 0, "2nd EPL"},       {"fixtures/team_mls_mia.json", 0, "3rd MLS"},
      // A week into the season ESPN had DAL's seed and no summary yet.
      {"fixtures/team_nfl_dal.json", 4, ""},              {"fixtures/team_ncaa_bc.json", 0, "3rd ACC"},
  };
  for (const auto &d : docs) {
    Schedule s;
    CHECK(parse_team_str(slurp(d.file), s));
    CHECK_EQ((int) s.seed, d.seed);
    CHECK_EQ(s.place, std::string(d.place));
  }
  CHECK_EQ(place_short("1st in NFC East"), std::string("1st NFCE"));
  CHECK_EQ(place_short("2nd in AFC North"), std::string("2nd AFCN"));
  // ESPN gives some NFL teams their conference's place, by its full name:
  // the initials keep "Conference".
  CHECK_EQ(place_short("14th in National Football Conference"), std::string("14th NFC"));
  CHECK_EQ(place_short("3rd in American Football Conference"), std::string("3rd AFC"));
  CHECK_EQ(place_short("4th in Eastern Conference Division"), std::string("4th East"));
  {
    // DAL's team document with such a summary
    std::string doc = slurp("fixtures/team_nfl_dal.json");
    const std::string key = "\"team\":{";
    const size_t at = doc.find(key);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) {
      doc.insert(at + key.size(), "\"standingSummary\":\"3rd in American Football Conference\",");
      Schedule conf;
      CHECK(parse_team_str(doc, conf));
      CHECK_EQ(conf.place, std::string("3rd AFC"));
      CHECK_EQ((int) conf.seed, 4);
      CHECK_EQ(standing_text(Standing::PLACE, conf.seed, conf.place), std::string("3rd AFC"));
    }
  }
  CHECK_EQ(place_short("1st in Southeast Division"), std::string("1st SE"));
  CHECK_EQ(place_short("12th in English Premier League"), std::string("12th EPL"));
  CHECK_EQ(place_short("2nd in Western Conference"), std::string("2nd West"));
  CHECK_EQ(place_short(""), std::string(""));
  CHECK_EQ(place_short("First in NFC East"), std::string(""));
  CHECK_EQ(place_short("in NFC East"), std::string(""));
  CHECK_EQ(place_short("3rd in "), std::string("3rd"));
  // A seed that isn't one: left out.
  Schedule odd;
  CHECK(parse_team_str(R"({"team":{"id":"6","record":{"items":[{"summary":"2-1","stats":[{"name":"playoffSeed","value":-3}]}]},"standingSummary":7}})", odd));
  CHECK_EQ((int) odd.seed, 0);
  CHECK_EQ(odd.place, std::string(""));
  CHECK_EQ(odd.team_record, std::string("2-1"));

  CHECK_EQ(standing_text(Standing::SEED, 4, "1st NFCE"), std::string("Seed 4"));
  CHECK_EQ(standing_text(Standing::SEED, 0, "1st NFCE"), std::string(""));  // the record
  CHECK_EQ(standing_text(Standing::PLACE, 4, "1st NFCE"), std::string("1st NFCE"));
  CHECK_EQ(standing_text(Standing::PLACE, 4, ""), std::string(""));
  CHECK_EQ(standing_text(Standing::RECORD, 4, "1st NFCE"), std::string(""));
  // Under a logo on two panels: 7 characters, so a longer place drops its
  // space, else keeps the place alone; beside a 10-character clock, 6.
  const size_t row = record_row_chars(0), beside = record_row_chars(10 * kClockCharPx);
  CHECK_EQ(board_standing("Seed 12", row), std::string("Seed 12"));
  CHECK_EQ(board_standing("2nd Pac", row), std::string("2nd Pac"));
  CHECK_EQ(board_standing("1st NFCE", row), std::string("1stNFCE"));
  CHECK_EQ(board_standing("2nd East", row), std::string("2ndEast"));
  CHECK_EQ(board_standing("15th EPL", row), std::string("15thEPL"));
  CHECK_EQ(board_standing("14th NFC", row), std::string("14thNFC"));
  CHECK_EQ(board_standing("10th West", row), std::string("10th"));
  CHECK_EQ(board_standing("3rd Metro", row), std::string("3rd"));
  CHECK_EQ(board_standing("Seed 12", beside), std::string("Seed12"));
  CHECK_EQ(board_standing("1st NFCE", beside), std::string("1st"));
  CHECK_EQ(board_standing("2nd Pac", beside), std::string("2ndPac"));
  CHECK_EQ(board_standing("Seed 12", 5), std::string(""));  // "Seed" alone: the record
  CHECK_EQ(board_standing("10th West", 3), std::string(""));
  CHECK_EQ(board_standing("", row), std::string(""));

  // The ticker: the followed team's standing in its record's place, the
  // opponent's record as it was.
  GameSnapshot chi;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl_wild_card.json"), "401772981", 3, chi));
  TickerOptions o;
  CHECK_EQ(status_text(chi, o, ""), std::string("Final | CHI 11-6 | GB 9-7-1"));
  TickerOptions seeded;
  seeded.team_standing = "Seed 2";
  CHECK_EQ(status_text(chi, seeded, ""), std::string("Final | CHI Seed 2 | GB 9-7-1"));
  GameSnapshot a = soccer_event("fixtures/event_epl_final.json", kArsenal);
  TickerOptions placed;
  placed.team_standing = "2nd EPL";
  CHECK_EQ(status_text(a, placed, ""),
           std::string("Full time | Goals ARS 58' Guimaraes, 90'+7' Saka (P) | Red cards SUN 1 | ARS 2nd EPL | W-D-L SUN 1-1-2"));

  // A cup's team read ranks the club in the cup's own table under the
  // league's name: on a Champions League night ARS is 2nd in the Premier
  // League, not 15th.
  Schedule league, cup;
  CHECK(parse_team_str(slurp("fixtures/team_epl_ars.json"), league));
  CHECK(parse_team_str(slurp("fixtures/team_ucl_ars.json"), cup));
  CHECK_EQ(league.place, std::string("2nd EPL"));
  CHECK_EQ(cup.place, std::string("15th EPL"));
  adopt_comp(cup, "uefa.champions");
  league.event_id = "740000";
  league.kickoff_epoch = 1793000000;
  cup.event_id = "750000";
  cup.kickoff_epoch = league.kickoff_epoch - 3 * 86400;  // the cup game comes first
  cup.seed = 9;
  std::vector<Schedule> comps{league, cup};
  league_standing(comps);
  const int pick = pick_schedule(comps, cup.kickoff_epoch - 3600, "");
  CHECK_EQ(pick, 1);
  if (pick == 1) {
    CHECK_EQ(comps[1].event_id, std::string("750000"));
    CHECK_EQ(comps[1].place, std::string("2nd EPL"));
    CHECK_EQ((int) comps[1].seed, 0);
    CHECK_EQ(standing_text(Standing::PLACE, comps[1].seed, comps[1].place), std::string("2nd EPL"));
  }
  std::vector<Schedule> alone{league};
  league_standing(alone);  // no cups: nothing to do
  CHECK_EQ(alone[0].place, std::string("2nd EPL"));

  // A read whose seed goes unused (a college team's, a cup's) leaves the
  // record's stats out: no seed, the rest as before.
  Schedule bare;
  const std::string atl = slurp("fixtures/team_mlb_atl.json");
  CHECK(parse_team(atl, bare, false));
  CHECK_EQ((int) bare.seed, 0);
  CHECK_EQ(bare.place, std::string("1st NLE"));
  CHECK_EQ(bare.team_record, std::string("94-68"));
}

// The record row under each logo on two panels beside the clock: 36 px of
// spleen_8 (5 px a character, 7 characters) at the board's edge, the clock in
// spleen_12 (6 px) centered on the 128 px board. A 10-character clock leaves
// 6 characters, so a ranked 7-character record drops its rank there.
static void test_record_row() {
  CHECK_EQ(record_row_chars(0), (size_t) 7);
  CHECK_EQ(record_row_chars(9 * kClockCharPx), (size_t) 7);   // "12:34 2nd"
  CHECK_EQ(record_row_chars(10 * kClockCharPx), (size_t) 6);  // "End of 2nd"
  CHECK_EQ(record_row_chars(11 * kClockCharPx), (size_t) 5);
  CHECK_EQ(record_row_chars(0, 128, 36, 0), (size_t) 0);  // a font the panel couldn't measure
  // Where each row's text lands as LVGL centers it (the clock's label is 64
  // px at x 32): a pixel clear of the clock's text on both sides, for every
  // clock up to the label's 10 characters.
  for (int chars = 1; chars <= 10; chars++) {
    const int clock_px = chars * kClockCharPx;
    const int t = (int) record_row_chars(clock_px) * kRecordCharPx;
    const int clock_left = 32 + (64 - clock_px) / 2, clock_right = clock_left + clock_px;
    const int left_end = (kRecordRowPx - t) / 2 + t;
    const int right_start = kBoardPx - kRecordRowPx + (kRecordRowPx - t) / 2;
    CHECK(left_end < clock_left);
    CHECK(right_start > clock_right);
  }
  // #16 MSST at #22 LSU at the end of the 2nd quarter: the clock reads "End
  // of 2nd", so the records under the logos keep to 6 characters and the
  // ranks drop out until the clock runs again.
  GameSnapshot msst;
  CHECK(parse_scoreboard_str(slurp("fixtures/day_ncaa_20261017.json"), "401856720", 344, msst));
  CHECK_EQ(clock_text(msst), std::string("End of 2nd"));
  CHECK_EQ((int) msst.team_rank, 16);
  CHECK_EQ((int) msst.opp_rank, 22);
  const size_t room = record_row_chars((int) clock_text(msst).size() * kClockCharPx);
  CHECK_EQ(room, (size_t) 6);
  CHECK_EQ(rank_label(msst.team_rank, msst.team_record, room), std::string("4-1"));
  CHECK_EQ(rank_label(msst.opp_rank, msst.opp_record, room), std::string("4-2"));
  CHECK_EQ(rank_label(5, "4-1", room), std::string("#5 4-1"));  // a one-digit rank still fits
  // A 9-character clock ("12:34 3rd") leaves the 7.
  const size_t running = record_row_chars(9 * kClockCharPx);
  CHECK_EQ(rank_label(msst.team_rank, msst.team_record, running), std::string("#16 4-1"));
  // Before and after the game, no clock: 7.
  CHECK_EQ(rank_label(msst.opp_rank, msst.opp_record, record_row_chars(0)), std::string("#22 4-2"));

  // Three and four panels in a row (192 and 256 px): the rows stay 36 px at
  // the board's edges and the clock's label runs from x 32 to 32 px short of
  // the right edge, so every clock the panel sends (up to 10 characters, "End
  // of 2nd" or a countdown) is far from both rows and they keep all 7
  // characters. A 2x2's board is the 128 px one.
  using esphome::gameday::board_px;
  CHECK_EQ(board_px(1), 64);
  CHECK_EQ(board_px(2), kBoardPx);
  CHECK_EQ(board_px(3), 192);
  CHECK_EQ(board_px(4), 256);
  for (int w : {kBoardPx, board_px(3), board_px(4)}) {
    const int label_px = w - 64;
    for (int chars = 1; chars <= 10; chars++) {
      const int clock_px = chars * kClockCharPx;
      const int t = (int) record_row_chars(clock_px, w) * kRecordCharPx;
      const int clock_left = 32 + (label_px - clock_px) / 2, clock_right = clock_left + clock_px;
      const int left_end = (kRecordRowPx - t) / 2 + t;
      const int right_start = w - kRecordRowPx + (kRecordRowPx - t) / 2;
      CHECK(left_end < clock_left);
      CHECK(right_start > clock_right);
      if (w > kBoardPx)
        CHECK_EQ(record_row_chars(clock_px, w), (size_t) 7);
    }
  }
  CHECK_EQ(rank_label(msst.team_rank, msst.team_record, record_row_chars(10 * kClockCharPx, board_px(3))),
           std::string("#16 4-1"));
  // A pro team's place keeps its group beside "End of 2nd" there.
  CHECK_EQ(board_standing("1st NFCE", record_row_chars(10 * kClockCharPx, board_px(4))), std::string("1stNFCE"));
  CHECK_EQ(board_standing("1st NFCE", record_row_chars(10 * kClockCharPx, board_px(2))), std::string("1st"));
  // The idle display's countdown to a game sits where the clock does: "in
  // 23h 59m" is 10 characters, so on 128 px a ranked record keeps clear of
  // it the same way.
  CHECK_EQ(countdown_text(23 * 3600 + 59 * 60).size(), (size_t) 10);
  CHECK_EQ(record_row_chars((int) countdown_text(23 * 3600 + 59 * 60).size() * kClockCharPx), (size_t) 6);

  // The clock row the record rows are sized beside (board_clock_text): the
  // game clock during play, and My team today's countdown before a kickoff
  // that has a time. A pro team's place under its logo keeps clear of the
  // countdown like a rank: "1st NFCE" beside "in 11h 20m" is "1st" on 128
  // px, all seven characters on three or four panels.
  const int64_t now = 1794000000;
  GameSnapshot today = live(0, 0, GameState::PRE);
  today.kickoff_epoch = now + 11 * 3600 + 20 * 60;
  CHECK_EQ(board_clock_text(today, true, now), std::string("in 11h 20m"));
  CHECK_EQ(board_clock_text(today, false, now), std::string(""));  // the mode's own board: the kickoff
  auto standing_beside = [](const GameSnapshot &g, int64_t at, int w) {
    return board_standing("1st NFCE", record_row_chars((int) board_clock_text(g, true, at).size() * kClockCharPx, w));
  };
  CHECK_EQ(standing_beside(today, now, kBoardPx), std::string("1st"));
  CHECK_EQ(standing_beside(today, now, board_px(3)), std::string("1stNFCE"));
  CHECK_EQ(standing_beside(today, now, board_px(4)), std::string("1stNFCE"));
  CHECK_EQ(board_standing("Seed 10", record_row_chars(10 * kClockCharPx)), std::string("Seed10"));
  // A shorter countdown leaves the seven: "in 9h 5m" is 8 characters.
  CHECK_EQ(standing_beside(today, today.kickoff_epoch - (9 * 3600 + 5 * 60), kBoardPx), std::string("1stNFCE"));
  // No countdown without a kickoff time, or without the time of day.
  GameSnapshot tbd = today;
  tbd.time_tbd = true;
  CHECK_EQ(board_clock_text(tbd, true, now), std::string(""));
  CHECK_EQ(board_clock_text(today, true, 0), std::string(""));
  CHECK_EQ(standing_beside(tbd, now, kBoardPx), std::string("1stNFCE"));
  // During play the game clock, countdown or not; nothing after the final.
  GameSnapshot on = live(7, 3);
  on.sport = Sport::FOOTBALL;
  on.short_detail = "End of 2nd";
  CHECK_EQ(board_clock_text(on, true, now), clock_text(on));
  CHECK_EQ(board_clock_text(on, false, now), clock_text(on));
  CHECK_EQ(board_clock_text(live(7, 3, GameState::POST), true, now), std::string(""));
}

// The parts of the scoreboard page that change with the arrangement
// (board_layout.h): a splash over the whole screen with its words and
// fireworks in 64 rows, and what a 2x2's bottom half shows.
static void test_board_layout() {
  using namespace esphome::gameday;
  struct Screen {
    int w, h;
  };
  // One to four panels in a row, and the 2x2.
  for (Screen sc : {Screen{64, 64}, Screen{128, 64}, Screen{192, 64}, Screen{256, 64}, Screen{128, 128}}) {
    const SplashBox b = splash_box(sc.w, sc.h);
    CHECK_EQ(b.height, sc.h);  // the whole screen, a 2x2's bottom half too
    CHECK(b.band_y >= 0 && b.band_y + kBoardRows <= sc.h);
    CHECK_EQ(b.band_y * 2 + kBoardRows, sc.h);  // the middle rows
    // The 128 px fireworks canvas in the middle, clipped on one panel.
    if (sc.w >= kFxPx) {
      CHECK_EQ(b.fx_x * 2 + kFxPx, sc.w);
    } else {
      CHECK_EQ(b.fx_x, 0);
    }
    // Three lines of the words' font end inside the 64 rows: spleen_16 on a
    // wide board, spleen_12 on one panel.
    const int line = sc.w >= 128 ? 16 : 12;
    CHECK(b.text_y >= b.band_y && b.text_y + 3 * line <= b.band_y + kBoardRows);
  }
  // On a board of 64 rows nothing moves: as it was before the 2x2.
  CHECK_EQ(splash_box(128, 64).text_y, 16);
  CHECK_EQ(splash_box(64, 64).text_y, 18);
  CHECK_EQ(splash_box(256, 64).fx_x, 64);
  CHECK_EQ(splash_box(128, 128).text_y, 48);
  CHECK_EQ(splash_chars(64), (size_t) 10);
  CHECK_EQ(splash_chars(128), (size_t) 16);
  CHECK_EQ(splash_chars(192), (size_t) 24);
  CHECK_EQ(splash_chars(256), (size_t) 32);

  // The heat and power warning's 9 rows along the bottom of the screen:
  // over the ticker on one row of panels, as before, and on a 2x2 along its
  // bottom edge. The board's own ticker shows on one row of panels only; a
  // 2x2 has it once, in its bottom half.
  CHECK_EQ(guard_line_y(kBoardRows), 55);
  CHECK_EQ(guard_line_y(2 * kBoardRows), 119);
  CHECK(board_ticker(kBoardRows));
  CHECK(!board_ticker(2 * kBoardRows));

  // A 2x2's bottom half. With the board up: the time, the date and the
  // ticker, the ticker ending above the warning's line.
  const LowerHalf board = lower_half(false, false);
  CHECK(board.time && board.date);
  CHECK_EQ(board.date_y, 23);
  CHECK_EQ(board.ticker_y, 39);
  CHECK(lower_half(false, true).time);  // card_date only counts under the clock card
  // Under the clock card no time twice, and the date only when the card has
  // the next game in its place.
  for (bool card_date : {false, true}) {
    const LowerHalf l = lower_half(true, card_date);
    CHECK(!l.time);
    CHECK_EQ(l.date, !card_date);
  }
  // Nothing overlaps, everything ends inside the 64 rows, and what shows is
  // centered there.
  for (bool card : {false, true})
    for (bool card_date : {false, true}) {
      const LowerHalf l = lower_half(card, card_date);
      const int top = l.time ? kLowerTimeY : l.date ? l.date_y : l.ticker_y;
      CHECK(!l.time || kLowerTimeY + kLowerTimeRows <= l.date_y);
      CHECK(!l.date || l.date_y + kLowerDateRows <= l.ticker_y);
      CHECK(l.ticker_y + kLowerTickerRows <= kBoardRows);
      // The warning's line, on the screen's bottom rows, never cuts the
      // ticker's letters off.
      CHECK(kBoardRows + l.ticker_y + kLowerTickerRows <= guard_line_y(2 * kBoardRows));
      if (card)
        CHECK_EQ(top + (l.ticker_y + kLowerTickerRows), kBoardRows);
    }
}

// Home Assistant's line: no per-play parts, so it only moves on state and score.
static void test_ha_status_text() {
  GameSnapshot s = live(24, 21);
  s.down_distance = "3rd & 10 at BOIS 25";
  s.short_down_distance = "3rd & 10";
  s.short_detail = "15:00 - 4th";
  s.last_play = "End of 3rd quarter.";
  CHECK_EQ(ha_status_text(s, ""), std::string("Live | DAL 24, PHI 21"));
  s.last_play = "Prescott pass to Lamb for 12 yards";
  s.short_detail = "8:12 - 4th";
  CHECK_EQ(ha_status_text(s, ""), std::string("Live | DAL 24, PHI 21"));
  GameSnapshot h = live(14, 7);
  h.short_detail = "Halftime";
  CHECK_EQ(ha_status_text(h, ""), std::string("Halftime | DAL 14, PHI 7"));
  GameSnapshot p = live(0, 0, GameState::PRE);
  p.odds = "SEA -3.5";
  p.venue = "Lumen Field";
  p.short_detail = "10/15 - 8:20 PM EDT";
  CHECK_EQ(ha_status_text(p, "Wed 7:20 PM"), std::string("DAL vs PHI | Wed 7:20 PM"));
  CHECK_EQ(ha_status_text(p, ""), std::string("DAL vs PHI | 10/15 - 8:20 PM EDT"));
  p.game_name = "Rose Bowl";
  CHECK_EQ(ha_status_text(p, "Jan 1 4:00 PM"), std::string("Rose Bowl | DAL vs PHI | Jan 1 4:00 PM"));
  GameSnapshot f = live(31, 28, GameState::POST);
  f.team_record = "5-1";
  CHECK_EQ(ha_status_text(f, ""), std::string("Final | DAL 31, PHI 28"));
  GameSnapshot pp = live(0, 0, GameState::POST);
  pp.completed = false;
  pp.short_detail = "Postponed";
  CHECK_EQ(ha_status_text(pp, ""), std::string("Postponed | DAL 0, PHI 0"));
  CHECK_EQ(ha_status_text(GameSnapshot{}, ""), std::string("No upcoming game"));
}

static void test_short_game_name() {
  // Every headline in the recorded fixtures
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Rose Bowl Presented by Prudential"),
           std::string("Rose Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Allstate Sugar Bowl"),
           std::string("Sugar Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Capital One Orange Bowl"),
           std::string("Orange Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff National Championship Presented by AT&T"),
           std::string("CFP Championship"));
  CHECK_EQ(short_game_name("SEC Championship"), std::string("SEC Championship"));
  CHECK_EQ(short_game_name("Big 12 Championship"), std::string("Big 12 Championship"));
  CHECK_EQ(short_game_name("FCS Championship - Second Round"), std::string("FCS Championship - Second Round"));
  CHECK_EQ(short_game_name("NFC Wild Card Playoffs"), std::string("NFC Wild Card"));
  CHECK_EQ(short_game_name("Super Bowl LX"), std::string("Super Bowl LX"));
  CHECK_EQ(short_game_name("NFL Melbourne Game"), std::string("NFL Melbourne Game"));
  CHECK_EQ(short_game_name("Aflac Kickoff"), std::string("Aflac Kickoff"));
  CHECK_EQ(short_game_name("Liberty Mutual Music City Kickoff"), std::string("Liberty Mutual Music City Kickoff"));
  // Bowl names as ESPN has listed them in past seasons
  CHECK_EQ(short_game_name("Rose Bowl Game Presented by Prudential"), std::string("Rose Bowl"));
  CHECK_EQ(short_game_name("Goodyear Cotton Bowl Classic"), std::string("Cotton Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Semifinal at the Vrbo Fiesta Bowl"), std::string("Fiesta Bowl"));
  CHECK_EQ(short_game_name("SRS Distribution Las Vegas Bowl"), std::string("Las Vegas Bowl"));
  CHECK_EQ(short_game_name("Tony the Tiger Sun Bowl"), std::string("Sun Bowl"));
  CHECK_EQ(short_game_name("Snoop Dogg Arizona Bowl presented by Gin & Juice by Dre and Snoop"),
           std::string("Arizona Bowl"));
  CHECK_EQ(short_game_name("LA Bowl Hosted by Gronk"), std::string("LA Bowl"));
  CHECK_EQ(short_game_name("Duke's Mayo Bowl"), std::string("Duke's Mayo Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff First Round Game"), std::string("CFP First Round Game"));
  CHECK_EQ(short_game_name("AFC Divisional Playoffs"), std::string("AFC Divisional"));
  CHECK_EQ(short_game_name("AFC Championship"), std::string("AFC Championship"));
  CHECK_EQ(short_game_name("Pro Bowl Games"), std::string("Pro Bowl Games"));
  CHECK_EQ(short_game_name("NFL London Games"), std::string("NFL London Games"));
  CHECK_EQ(short_game_name(""), std::string(""));
  // Kickoff windows for TBD college games come in the same notes (2026-10-24 SEC slate)
  CHECK_EQ(short_game_name("FLEX: 3:30 - 4:30pm OR 6 - 8pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("AFTERNOON: 3:30 - 4:30pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("NIGHT: 6 - 8pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("Kickoff 7 PM ET"), std::string(""));
}

// The abbreviation badge's colors (badge.h), the app's rules.
static void test_badge() {
  // Luminance: black, white, and the app's 0.6 text threshold.
  CHECK_EQ(badge_luminance(0x000000), 0);
  CHECK_EQ(badge_luminance(0xFFFFFF), 1000);
  CHECK(badge_luminance(0xF2A900) > kBadgeDarkTextLum);   // gold: black text
  CHECK(badge_luminance(0x1E5AA8) < kBadgeDarkTextLum);   // blue: white text
  CHECK(badge_luminance(0xC8102E) < kBadgeDarkTextLum);   // red: white text
  // A second color that differs from the fill is the ring.
  BadgeStyle b = badge_style(0x1E5AA8, 0xF4F6F8);
  CHECK(b.fill == 0x1E5AA8 && b.has_ring && b.ring == 0xF4F6F8 && !b.dark_text);
  // The same color twice is no ring, and a mid-tone fill needs none.
  b = badge_style(0x1E5AA8, 0x1E5AA8);
  CHECK(!b.has_ring);
  // A near-black fill would vanish on the panel: a gray ring stands in when
  // it has no ring or its ring is as dark.
  b = badge_style(0x000000, 0x000000);
  CHECK(b.has_ring && b.ring == kBadgeNeutralRing);
  b = badge_style(0x101010, 0x080808);
  CHECK(b.has_ring && b.ring == kBadgeNeutralRing);
  // A near-black fill with a visible ring keeps that ring.
  b = badge_style(0x000000, 0xC8102E);
  CHECK(b.has_ring && b.ring == 0xC8102E && !b.dark_text);
  // A light fill takes black text and keeps its own ring.
  b = badge_style(0xF4F6F8, 0x1E5AA8);
  CHECK(b.dark_text && b.has_ring && b.ring == 0x1E5AA8);
}

// The badge thresholds to the step, on grays: black text only above 600, the
// gray ring only below 120, as in the app.
static void test_badge_thresholds() {
  CHECK_EQ(badge_luminance(0x999999), kBadgeDarkTextLum);
  CHECK(!badge_style(0x999999, 0x999999).dark_text);
  CHECK(badge_style(0x9A9A9A, 0x9A9A9A).dark_text);
  CHECK(badge_luminance(0x1E1E1E) < kBadgeNearBlackLum);
  CHECK(badge_luminance(0x1F1F1F) > kBadgeNearBlackLum);
  BadgeStyle b = badge_style(0x1E1E1E, 0x1E1E1E);
  CHECK(b.has_ring && b.ring == kBadgeNeutralRing);
  b = badge_style(0x1F1F1F, 0x1F1F1F);
  CHECK(!b.has_ring);
  // A near-black fill with a ring just light enough keeps it.
  b = badge_style(0x000000, 0x1F1F1F);
  CHECK(b.has_ring && b.ring == 0x1F1F1F);
  // Green weighs most: a pure green is light, a pure blue dark.
  CHECK(badge_style(0x00FF00, 0x00FF00).dark_text);
  CHECK(!badge_style(0x0000FF, 0x0000FF).dark_text);
}

// The demo build's matchups, made-up teams and script (demo.h).
static void test_demo() {
  auto abbrs = [](DemoMatchup m) { return std::string(m.home->abbr) + "-" + m.away->abbr; };
  // No picks: the panel's own team at home when it is in the league, else the stock game.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NFL, 12)) == "KC-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NCAA, 333)) == "DAL-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NCAA, 333)) == "ALA-MICH");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NFL, 6)) == "OSU-MICH");
  // A panel that follows the stock opponent plays the stock home team instead.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NFL, 21)) == "PHI-DAL");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NCAA, 130)) == "MICH-OSU");
  // Picks win, and a pick from the other league is no pick.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 25, League::NFL, 6)) == "SEA-SF");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 333, 0, League::NFL, 6)) == "DAL-PHI");
  // The same team on both sides never happens.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 26, League::NFL, 6)) == "SEA-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 6, League::NFL, 6)) == "DAL-PHI");

  // Made-up teams: unique, and never a real team's abbreviation.
  for (size_t i = 0; i < kMadeUpTeamCount; i++) {
    for (size_t j = i + 1; j < kMadeUpTeamCount; j++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kMadeUpTeams[j].abbr);
    for (size_t t = 0; t < kTeamCount; t++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kTeams[t].abbr);
  }
  CHECK(std::string(demo_made_up(DemoKind::PRO, true).abbr) != demo_made_up(DemoKind::COLLEGE, true).abbr);
  // Nothing real anywhere in the demo's teams: no real team's name, and a
  // second color that gives every badge a ring (badge.h never needs the gray
  // fallback for them).
  for (size_t i = 0; i < kMadeUpTeamCount; i++) {
    for (size_t t = 0; t < kTeamCount; t++)
      CHECK(std::string(kMadeUpTeams[i].name) != kTeams[t].name);
    CHECK(kMadeUpTeams[i].alt != kMadeUpTeams[i].rgb);
    BadgeStyle b = badge_style(kMadeUpTeams[i].rgb, kMadeUpTeams[i].alt);
    CHECK(b.has_ring && b.ring == kMadeUpTeams[i].alt && b.fill == kMadeUpTeams[i].rgb);
  }
  // White letters on the blue, red and green fills; black on the gold one.
  CHECK(!badge_style(demo_made_up(DemoKind::PRO, true).rgb, 0).dark_text);
  CHECK(!badge_style(demo_made_up(DemoKind::PRO, false).rgb, 0).dark_text);
  CHECK(!badge_style(demo_made_up(DemoKind::COLLEGE, true).rgb, 0).dark_text);
  CHECK(badge_style(demo_made_up(DemoKind::COLLEGE, false).rgb, 0).dark_text);
  // One pass of the script is what the shot list is timed against.
  uint32_t total_ms = 0;
  for (size_t i = 0; i < kDemoStepCount; i++)
    total_ms += kDemoSteps[i].hold_ms;
  CHECK(total_ms == 52500);

  // Every football team has its color (the demo is football only), and the
  // color table matches teams.h, where football comes first.
  size_t football = 0;
  while (football < kTeamCount && league_sport(kTeams[football].league) == Sport::FOOTBALL)
    football++;
  CHECK(kTeamColorCount == football);
  for (size_t t = 0; t < football && t < kTeamColorCount; t++)
    CHECK(kTeamColors[t].league == kTeams[t].league && kTeamColors[t].espn_id == kTeams[t].espn_id);
  CHECK(team_rgb(League::NFL, 6) == 0x002A5C);
  CHECK(team_rgb(League::NFL, 999999) == 0xFFFFFF);

  // The script: starts before kickoff, ends final with the home side ahead,
  // scores never go down, and the splashes fire along the way.
  CHECK(kDemoSteps[0].state == GameState::PRE);
  const DemoStep &last = kDemoSteps[kDemoStepCount - 1];
  CHECK(last.state == GameState::POST && last.us > last.them);
  GameSnapshot prev, cur;
  int touchdowns = 0, wins = 0;
  for (size_t i = 0; i < kDemoStepCount; i++) {
    if (i > 0) {
      CHECK(kDemoSteps[i].us >= kDemoSteps[i - 1].us && kDemoSteps[i].them >= kDemoSteps[i - 1].them);
    }
    cur = GameSnapshot{};
    cur.valid = true;
    cur.team_abbr = "HBR";
    cur.opp_abbr = "IRN";
    cur.state = kDemoSteps[i].state;
    cur.completed = cur.state == GameState::POST;
    cur.team_score = kDemoSteps[i].us;
    cur.opp_score = kDemoSteps[i].them;
    cur.team_winner = cur.completed && cur.team_score > cur.opp_score;
    if (i > 0) {
      Splash sp = decide_splash(prev, cur, true, false);
      if (sp.text.find("TOUCHDOWN") != std::string::npos)
        touchdowns++;
      if (sp.text == "HBR WINS!")
        wins++;
    }
    prev = cur;
  }
  CHECK(touchdowns == 4);
  CHECK(wins == 1);
}

static Final mkfinal(uint8_t league, int rank, int64_t kick, const char *abbr) {
  Final f;
  f.league = league;
  f.rank = (uint8_t) rank;
  f.kickoff_epoch = kick;
  f.away_abbr = abbr;
  return f;
}

static void test_recap() {
  const uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA;
  // Finals ride along with the scan only when asked for
  std::string titles = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  std::vector<LiveGame> live;
  std::vector<Final> finals;
  int64_t next = -1;
  CHECK(parse_live_games(titles, live, &next, &finals));
  CHECK_EQ(live.size(), (size_t) 0);
  CHECK_EQ(next, (int64_t) 0);
  CHECK_EQ(finals.size(), (size_t) 6);
  if (finals.size() == 6) {
    // Big Ten title: IU (away, #2) 13, OSU (home, #1) 10
    const Final &f = finals[0];
    CHECK_EQ(f.away_abbr, std::string("IU"));
    CHECK_EQ(f.away_id, (uint32_t) 84);
    CHECK_EQ(f.away_score, 13);
    CHECK_EQ(f.home_abbr, std::string("OSU"));
    CHECK_EQ(f.home_id, kOhioState);
    CHECK_EQ(f.home_score, 10);
    CHECK_EQ((int) f.rank, 1);
    CHECK_EQ(f.detail, std::string("Final"));
    CHECK_EQ(f.kickoff_epoch, parse_iso8601_z("2025-12-07T01:00Z"));
    CHECK(!f.away_color.empty());
    CHECK_EQ(finals[3].detail, std::string("Final/OT"));
    CHECK_EQ((int) finals[3].rank, 17);  // UVA #17, Duke unranked (99)
    CHECK_EQ((int) finals[4].rank, 99);  // neither ranked
    // The card: away side first like a broadcast, logos from the ids
    Final c = f;
    c.league = (uint8_t) League::NCAA;
    GameSnapshot g = final_snapshot(c);
    CHECK(g.valid);
    CHECK(g.state == GameState::POST);
    CHECK_EQ(g.team_abbr, std::string("IU"));
    CHECK_EQ(g.opp_score, 10);
    CHECK_EQ(g.team_logo, team_logo_url(League::NCAA, 84, "IU"));
    CHECK_EQ(g.opp_logo, team_logo_url(League::NCAA, kOhioState, "OSU"));
    CHECK_EQ(status_text(g, TickerOptions{}, "").substr(0, 5), std::string("Final"));
  }
  // Postponed and canceled games are "post" but not completed: no card
  std::string mixed = R"({"events":[)"
                      R"({"id":"1","date":"2026-10-10T16:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Postponed"}},"competitions":[{"competitors":[{"id":"1","homeAway":"home","score":"0","team":{"abbreviation":"AAA"}},{"id":"2","homeAway":"away","score":"0","team":{"abbreviation":"BBB"}}]}]},)"
                      R"({"id":"2","date":"2026-10-10T19:30Z","status":{"type":{"state":"post","completed":true,"shortDetail":"Final"}},"competitions":[{"competitors":[{"id":"3","homeAway":"home","score":"21","team":{"abbreviation":"CCC"}},{"id":"4","homeAway":"away","score":"24","team":{"abbreviation":"DDD"}}]}]}]})";
  finals.clear();
  CHECK(parse_live_games(mixed, live, nullptr, &finals));
  CHECK_EQ(finals.size(), (size_t) 1);
  if (finals.size() == 1) {
    CHECK_EQ(finals[0].away_abbr, std::string("DDD"));
    CHECK_EQ(finals[0].away_score, 24);
    CHECK_EQ((int) finals[0].rank, 99);
  }
  // Without the finals list nothing changes for the plain scan
  CHECK(parse_live_games(mixed, live, &next));
  CHECK_EQ(live.size(), (size_t) 0);

  // Days, Eastern like scan_url()
  int32_t sat = espn_day(parse_iso8601_z("2025-12-06T21:00Z"), NCAA);
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T01:00Z"), NCAA), sat);  // 8 PM Eastern Saturday
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T06:00Z"), NCAA), sat + 1);
  CHECK(scan_url(League::NCAA, espn_day_epoch(sat)).find("dates=20251206") != std::string::npos);
  CHECK_EQ(recap_day_word(sat, sat), std::string("Today's"));
  CHECK_EQ(recap_day_word(sat, sat + 1), std::string("Yesterday's"));
  CHECK_EQ(recap_day_word(sat, sat + 2), std::string("Saturday's"));
  CHECK_EQ(recap_day_word(sat - 1, sat + 2), std::string("Friday's"));
  RecapDay hd;
  hd.league = NCAA;
  hd.day = sat;
  CHECK_EQ(recap_header(hd, espn_day_epoch(sat + 2)), std::string("Saturday's college scores"));
  hd.league = NFL;
  CHECK_EQ(recap_header(hd, espn_day_epoch(sat)), std::string("Today's NFL scores"));

  // Merge: newest day first, ranked games first, a rescan replaces the day
  std::vector<RecapDay> days;
  RecapDay d1;
  d1.league = NCAA;
  d1.day = sat;
  d1.finals = {mkfinal(NCAA, 99, 100, "U1"), mkfinal(NCAA, 5, 200, "R5"), mkfinal(NCAA, 2, 300, "R2"),
               mkfinal(NCAA, 99, 50, "U0")};
  recap_merge(days, d1);
  CHECK_EQ(days.size(), (size_t) 1);
  if (days.size() == 1 && days[0].finals.size() == 4) {
    CHECK_EQ(days[0].finals[0].away_abbr, std::string("R2"));
    CHECK_EQ(days[0].finals[1].away_abbr, std::string("R5"));
    CHECK_EQ(days[0].finals[2].away_abbr, std::string("U0"));
    CHECK_EQ(days[0].finals[3].away_abbr, std::string("U1"));
  }
  RecapDay n1;
  n1.league = NFL;
  n1.day = sat + 2;  // Monday night
  n1.finals = {mkfinal(NFL, 99, 400, "MNF")};
  recap_merge(days, n1);
  RecapDay n0;
  n0.league = NFL;
  n0.day = sat + 1;  // Sunday
  for (int i = 0; i < 13; i++)
    n0.finals.push_back(mkfinal(NFL, 99, 300 + i, "SUN"));
  recap_merge(days, n0);
  CHECK_EQ(days.size(), (size_t) 3);
  if (days.size() == 3) {
    CHECK_EQ(days[0].day, sat + 2);
    CHECK_EQ(days[1].day, sat + 1);
    CHECK_EQ(days[2].day, sat);
  }
  RecapDay again = n1;
  again.finals.push_back(mkfinal(NFL, 99, 500, "MNF2"));
  recap_merge(days, again);
  CHECK_EQ(days.size(), (size_t) 3);
  CHECK_EQ(days[0].finals.size(), (size_t) 2);
  RecapDay empty;
  empty.league = NFL;
  empty.day = sat + 3;
  recap_merge(days, empty);
  CHECK_EQ(days.size(), (size_t) 3);  // an empty day is not stored
  CHECK_EQ(recap_count(days, NFL), (size_t) 15);
  CHECK_EQ(recap_count(days, NCAA), (size_t) 4);

  // Trim: Monday's 2 are short of 4, so Sunday stays; a new NFL day with 4
  // pushes both out. Anything a week old goes.
  recap_trim(days, espn_day_epoch(sat + 3));
  CHECK_EQ(days.size(), (size_t) 3);
  RecapDay thu;
  thu.league = NFL;
  thu.day = sat + 5;
  for (int i = 0; i < 4; i++)
    thu.finals.push_back(mkfinal(NFL, 99, 600 + i, "THU"));
  recap_merge(days, thu);
  recap_trim(days, espn_day_epoch(sat + 5));
  CHECK_EQ(recap_count(days, NFL), (size_t) 4);
  CHECK_EQ(recap_count(days, NCAA), (size_t) 4);
  recap_trim(days, espn_day_epoch(sat + 7));
  CHECK_EQ(recap_count(days, NCAA), (size_t) 0);
  CHECK_EQ(recap_count(days, NFL), (size_t) 4);

  // Cards: header, finals, then the next kickoff. The cap is split between
  // the leagues that have finals.
  std::vector<RecapDay> two;
  RecapDay c;
  c.league = NCAA;
  c.day = sat;
  for (int i = 0; i < 30; i++)
    c.finals.push_back(mkfinal(NCAA, 99, i, "C"));
  recap_merge(two, c);
  CHECK_EQ(two[0].finals.size(), (size_t) 22);  // a day keeps what one loop can show
  const uint8_t both = kRecapNfl | kRecapCollege;
  std::vector<RecapCard> cards = recap_cards(two, both, true);
  CHECK_EQ(cards.size(), (size_t) (1 + 22 + 1));
  CHECK(cards.front().kind == RecapKind::HEADER);
  CHECK(cards[1].kind == RecapKind::FINAL);
  CHECK(cards.back().kind == RecapKind::NEXT);
  RecapDay mon;
  mon.league = NFL;
  mon.day = sat + 2;
  mon.finals = {mkfinal(NFL, 99, 1, "N")};
  RecapDay sun;
  sun.league = NFL;
  sun.day = sat + 1;
  for (int i = 0; i < 13; i++)
    sun.finals.push_back(mkfinal(NFL, 99, i, "N"));
  recap_merge(two, mon);
  recap_merge(two, sun);
  cards = recap_cards(two, both, false);
  size_t headers = 0, nfl = 0, college = 0;
  for (const auto &k : cards) {
    if (k.kind == RecapKind::HEADER)
      headers++;
    else if (k.kind == RecapKind::FINAL)
      (two[k.day].league == NFL ? nfl : college)++;
  }
  CHECK_EQ(headers, (size_t) 3);
  CHECK_EQ(nfl, (size_t) 11);
  CHECK_EQ(college, (size_t) 11);
  CHECK(cards.back().kind == RecapKind::FINAL);
  // A league the mode does not read gets no cards, and the other one the
  // whole cap
  cards = recap_cards(two, kRecapNfl, false);
  CHECK_EQ(cards.size(), (size_t) (2 + 14));
  for (const auto &k : cards)
    CHECK_EQ((int) two[k.day].league, (int) NFL);
  CHECK_EQ(recap_cards(two, kRecapCollege, false).size(), (size_t) 23);
  CHECK_EQ(recap_cards({}, both, true).size(), (size_t) 1);
  CHECK_EQ(recap_cards({}, both, false).size(), (size_t) 0);
  CHECK_EQ(recap_card_ms(RecapKind::FINAL), (uint32_t) 7000);

  // Panels from before "no team" saved Dallas on first boot
  CHECK(legacy_team_unpicked(NFL, 6, false, false));
  CHECK(legacy_team_unpicked(NFL, 6, true, true));
  CHECK(!legacy_team_unpicked(NFL, 6, true, false));
  CHECK(!legacy_team_unpicked(NFL, 17, false, true));
  CHECK(!legacy_team_unpicked(NCAA, 6, false, false));
}

// The walk back for older finals reads 1 to kRecapBackDays days before today,
// and recap_trim keeps every one of those days and none older, so no read is
// thrown away the moment it lands.
static void test_recap_back_days() {
  const uint8_t NHL = (uint8_t) League::NHL;
  CHECK_EQ(kRecapBackDays, 6);
  const int64_t now = parse_iso8601_z("2026-10-19T16:00Z");
  const int32_t today = espn_day(now, NHL);
  CHECK(recap_day_kept(today, today));
  for (int w = 1; w <= kRecapBackDays; w++)
    CHECK(recap_day_kept(today - w, today));
  CHECK(!recap_day_kept(today - kRecapBackDays - 1, today));
  // The walk reads steps 1 to kRecapBackDays (gameday.cpp uses recap_back_reads
  // for both its read and its stop), and every day it reads is one kept.
  CHECK(!recap_back_reads(0));
  for (int w = 1; w <= kRecapBackDays; w++) {
    CHECK(recap_back_reads(w));
    CHECK(recap_day_kept(today - w, today));
  }
  CHECK(!recap_back_reads(kRecapBackDays + 1));
  CHECK(!recap_back_reads(kRecapMaxDays + 1));  // the walk's done value
  // One final on the oldest day the walk reads and one the day before it:
  // short of kRecapMinFinals, so only age decides.
  std::vector<RecapDay> days;
  for (int w : {kRecapBackDays, kRecapBackDays + 1}) {
    RecapDay d;
    d.league = NHL;
    d.day = today - w;
    d.finals = {mkfinal(NHL, 99, espn_day_epoch(d.day), "OLD")};
    recap_merge(days, d);
  }
  CHECK_EQ(days.size(), (size_t) 2);
  recap_trim(days, now);
  CHECK_EQ(days.size(), (size_t) 1);
  if (days.size() == 1)
    CHECK_EQ(days[0].day, today - kRecapBackDays);
}

// How a loop's cards are shared between leagues: one each in league order,
// round and round, so the split is as even as it can be and a league with
// fewer games leaves the rest to the others.
static void test_recap_share() {
  const size_t NFL = (size_t) League::NFL, NCAA = (size_t) League::NCAA, NHL = (size_t) League::NHL;
  size_t have[kLeagueCount] = {}, got[kLeagueCount];
  have[NFL] = 20;
  have[NCAA] = 20;
  recap_share(have, 22, got);
  CHECK_EQ(got[NFL], (size_t) 11);
  CHECK_EQ(got[NCAA], (size_t) 11);
  // An odd card goes to the league that comes first.
  recap_share(have, 7, got);
  CHECK_EQ(got[NFL], (size_t) 4);
  CHECK_EQ(got[NCAA], (size_t) 3);
  // A league with one game keeps it and the other takes the rest.
  have[NFL] = 1;
  recap_share(have, 22, got);
  CHECK_EQ(got[NFL], (size_t) 1);
  CHECK_EQ(got[NCAA], (size_t) 20);
  // Room for more than there is: everything, and nothing for a league with none.
  have[NHL] = 3;
  recap_share(have, 100, got);
  CHECK_EQ(got[NFL] + got[NCAA] + got[NHL], (size_t) 24);
  CHECK_EQ(got[(size_t) League::MLB], (size_t) 0);
  // Three leagues, eight later-today cards.
  have[NFL] = have[NCAA] = have[NHL] = 8;
  recap_share(have, kRecapLaterCap, got);
  CHECK_EQ(got[NFL], (size_t) 3);
  CHECK_EQ(got[NCAA], (size_t) 3);
  CHECK_EQ(got[NHL], (size_t) 2);
  // No room, or nothing to show: every share is 0, whatever got held before.
  recap_share(have, 0, got);
  size_t total = 0;
  for (size_t l = 0; l < kLeagueCount; l++)
    total += got[l];
  CHECK_EQ(total, (size_t) 0);
  size_t none[kLeagueCount] = {};
  recap_share(none, 22, got);
  for (size_t l = 0; l < kLeagueCount; l++)
    total += got[l];
  CHECK_EQ(total, (size_t) 0);
}

static Pending mkpending(uint8_t league, int rank, int64_t kick, const char *abbr) {
  Pending p;
  p.league = league;
  p.rank = (uint8_t) rank;
  p.kickoff_epoch = kick;
  p.away_abbr = abbr;
  p.home_abbr = "HOM";
  return p;
}

static bool sorted_by_kickoff(const std::vector<Pending> &v) {
  for (size_t i = 1; i < v.size(); i++)
    if (v[i - 1].kickoff_epoch > v[i].kickoff_epoch)
      return false;
  return true;
}

// The rotation's later-today cards and finals for every league.
static void test_later_today() {
  const uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA, NBA = (uint8_t) League::NBA,
                NHL = (uint8_t) League::NHL, MLB = (uint8_t) League::MLB, MCBB = (uint8_t) League::MCBB;
  const uint32_t lo = 2 * 60 * 1000, hi = 3 * 3600 * 1000;

  // NHL, 10/9: four games to come, in kickoff order, with what a card needs
  std::string nhl_day = slurp("fixtures/day_nhl_20261009.json");
  std::vector<LiveGame> live;
  std::vector<Final> finals;
  std::vector<Pending> nhl;
  int64_t next = -1;
  CHECK(parse_live_games(nhl_day, live, &next, &finals, &nhl));
  CHECK_EQ(live.size(), (size_t) 0);
  CHECK_EQ(finals.size(), (size_t) 0);
  CHECK_EQ(nhl.size(), (size_t) 4);
  CHECK(sorted_by_kickoff(nhl));
  if (nhl.size() == 4) {
    CHECK_EQ(nhl[0].event_id, std::string("401892465"));
    CHECK_EQ(nhl[0].away_abbr, std::string("SEA"));
    CHECK_EQ(nhl[0].home_abbr, std::string("DET"));
    CHECK_EQ(nhl[0].away_id, (uint32_t) 124292);
    CHECK(nhl[0].home_id != 0);
    CHECK_EQ(nhl[0].away_record, std::string("2-2-0"));
    CHECK(!nhl[0].home_record.empty());
    CHECK_EQ(nhl[0].away_color, std::string("000d33"));
    CHECK(!nhl[0].home_color.empty());
    CHECK_EQ(nhl[0].tv, std::string("ESPN+"));
    CHECK_EQ((int) nhl[0].rank, 99);
    CHECK_EQ(nhl[0].kickoff_epoch, parse_iso8601_z("2026-10-09T23:00Z"));
    CHECK_EQ(nhl[1].away_abbr, std::string("NYR"));
    CHECK_EQ(nhl[1].tv, std::string("NHL Net"));
    CHECK_EQ(nhl[3].away_abbr, std::string("ANA"));
    CHECK_EQ(nhl[3].kickoff_epoch, parse_iso8601_z("2026-10-10T00:00Z"));
    // The rescan is unchanged: it still waits for the first of them
    CHECK_EQ(next, nhl[0].kickoff_epoch);
    int64_t five_pm = parse_iso8601_z("2026-10-09T21:00Z");
    CHECK_EQ(no_live_rescan_ms(five_pm, next, lo, hi), (uint32_t) ((nhl[0].kickoff_epoch - five_pm) * 1000));
    CHECK_EQ(no_live_rescan_ms(five_pm, next, lo, hi), (uint32_t) 2 * 3600 * 1000);
    // The card: away side first, logos from the ids, the league's sport
    Pending p = nhl[0];
    p.league = NHL;
    GameSnapshot g = pending_snapshot(p);
    CHECK(g.valid);
    CHECK(g.state == GameState::PRE);
    CHECK(g.sport == Sport::HOCKEY);
    CHECK_EQ(g.team_abbr, std::string("SEA"));
    CHECK_EQ(g.opp_abbr, std::string("DET"));
    CHECK_EQ(g.kickoff_epoch, p.kickoff_epoch);
    CHECK_EQ(g.tv, std::string("ESPN+"));
    CHECK_EQ(g.team_logo, team_logo_url(League::NHL, 124292, "SEA"));
    CHECK_EQ(g.opp_logo, team_logo_url(League::NHL, p.home_id, "DET"));
  }
  // Without the list the scan's wait is the same
  next = -1;
  CHECK(parse_live_games(nhl_day, live, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-09T23:00Z"));

  // MLB, 10/12: the live game is not one to come; the one after it is
  std::string mlb_day = slurp("fixtures/day_mlb_20261012.json");
  std::vector<Pending> mlb;
  live.clear();
  CHECK(parse_live_games(mlb_day, live, &next, &finals, &mlb));
  CHECK_EQ(live.size(), (size_t) 1);
  CHECK_EQ(mlb.size(), (size_t) 1);
  if (mlb.size() == 1 && live.size() == 1) {
    CHECK_EQ(live[0].event_id, std::string("401908031"));
    CHECK_EQ(mlb[0].event_id, std::string("401908023"));
    CHECK_EQ(mlb[0].away_abbr, std::string("CLE"));
    CHECK_EQ(mlb[0].home_abbr, std::string("TB"));
    CHECK_EQ(mlb[0].tv, std::string("TBS"));
    CHECK_EQ(next, mlb[0].kickoff_epoch);
    int64_t two_pm = parse_iso8601_z("2026-10-12T18:00Z");
    CHECK_EQ(no_live_rescan_ms(two_pm, next, lo, hi), hi);  // six hours out: the cap
  }

  // NBA, 10/15: two to come, one with no TV listed
  std::string nba_day = slurp("fixtures/day_nba_20261015.json");
  std::vector<Pending> nba;
  live.clear();
  CHECK(parse_live_games(nba_day, live, &next, nullptr, &nba));
  CHECK_EQ(nba.size(), (size_t) 2);
  if (nba.size() == 2) {
    CHECK_EQ(nba[0].away_abbr, std::string("TOR"));
    CHECK_EQ(nba[0].home_abbr, std::string("NY"));
    CHECK_EQ(nba[0].tv, std::string(""));
    CHECK_EQ(nba[1].away_abbr, std::string("OKC"));
    CHECK_EQ(nba[1].tv, std::string("NBA TV"));
    CHECK_EQ(nba[1].kickoff_epoch, parse_iso8601_z("2026-10-16T00:30Z"));
    CHECK_EQ(next, nba[0].kickoff_epoch);
    // Before TOR at NY the walk forward waits for it
    int64_t six_pm = parse_iso8601_z("2026-10-15T23:00Z");
    CHECK_EQ(recap_scan_next(next, nba, six_pm), nba[0].kickoff_epoch);
    // 6:35 PM Central, ESPN still showing TOR at NY to come: OKC at HOU is
    // still ahead, so no read for tomorrow (bench, 10/15). The rescan's
    // floor while ESPN lags is unchanged.
    int64_t lagging = parse_iso8601_z("2026-10-15T23:35Z");
    CHECK_EQ(recap_scan_next(next, nba, lagging), nba[1].kickoff_epoch);
    CHECK_EQ(no_live_rescan_ms(lagging, next, lo, hi), lo);
    // Both kickoffs past: nothing left today, so the walk may look ahead
    CHECK_EQ(recap_scan_next(next, nba, parse_iso8601_z("2026-10-16T00:35Z")), (int64_t) 0);
    CHECK_EQ(recap_scan_next(0, {}, lagging), (int64_t) 0);
  }

  // NFL week 1, sixteen to come: only the first eight are kept, ties in
  // ESPN's order
  std::string nfl_week = slurp("fixtures/scoreboard_nfl.json");
  std::vector<Pending> nfl;
  CHECK(parse_live_games(nfl_week, live, &next, nullptr, &nfl));
  CHECK_EQ(nfl.size(), kPendingKeep);
  CHECK(sorted_by_kickoff(nfl));
  if (nfl.size() == kPendingKeep) {
    CHECK_EQ(nfl[0].away_abbr, std::string("NE"));
    CHECK_EQ(nfl[1].away_abbr, std::string("SF"));
    CHECK_EQ(nfl[2].away_abbr, std::string("TB"));
    CHECK_EQ(nfl[7].away_abbr, std::string("CHI"));
    CHECK_EQ(nfl[7].kickoff_epoch, parse_iso8601_z("2026-09-13T17:00Z"));
    CHECK_EQ(next, nfl[0].kickoff_epoch);
  }

  // College, a Saturday with 36 to come: the ranked ones are kept first,
  // and none of them is a game on now or a final. The rescan still waits
  // for the earliest kickoff, ranked or not.
  std::string ncaa = slurp("fixtures/scoreboard_ncaa_live.json");
  std::vector<Pending> college;
  live.clear();
  finals.clear();
  CHECK(parse_live_games(ncaa, live, &next, &finals, &college));
  CHECK_EQ(college.size(), kPendingKeep);
  CHECK_EQ(next, parse_iso8601_z("2026-09-05T23:00Z"));
  for (size_t i = 0; i < college.size(); i++) {
    CHECK(college[i].rank < 99);
    if (i > 0)
      CHECK(!pending_before(college[i], college[i - 1]));
    for (const auto &l : live)
      CHECK(l.event_id != college[i].event_id);
    CHECK(college[i].event_id != kPostEvent);
  }
  if (college.size() == kPendingKeep) {
    CHECK_EQ(college[0].away_abbr, std::string("WIS"));
    CHECK_EQ((int) college[0].rank, 4);
    CHECK_EQ((int) college[7].rank, 17);
  }

  // TBD, postponed and canceled games are not to come; a live game and a
  // final are not either
  std::string mixed = R"({"events":[)"
                      R"({"id":"1","date":"2026-10-24T04:00Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":false,"competitors":[{"id":"1","homeAway":"home","team":{"abbreviation":"AAA"}},{"id":"2","homeAway":"away","team":{"abbreviation":"BBB"}}]}]},)"
                      R"({"id":"2","date":"2026-10-24T16:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Postponed"}},"competitions":[{"timeValid":true,"competitors":[{"id":"3","homeAway":"home","team":{"abbreviation":"CCC"}},{"id":"4","homeAway":"away","team":{"abbreviation":"DDD"}}]}]},)"
                      R"({"id":"3","date":"2026-10-24T17:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Canceled"}},"competitions":[{"timeValid":true,"competitors":[{"id":"5","homeAway":"home","team":{"abbreviation":"EEE"}},{"id":"6","homeAway":"away","team":{"abbreviation":"FFF"}}]}]},)"
                      R"({"id":"4","date":"2026-10-24T19:30Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":true,"competitors":[{"id":"7","homeAway":"home","team":{"abbreviation":"GGG"}},{"id":"8","homeAway":"away","team":{"abbreviation":"HHH"}}],"broadcasts":[{"names":["ESPN","ABC"]}]}]},)"
                      R"({"id":"5","date":"2026-10-24T18:00Z","status":{"type":{"state":"in"}},"competitions":[{"timeValid":true,"competitors":[{"id":"9","homeAway":"home","team":{"abbreviation":"III"}},{"id":"10","homeAway":"away","team":{"abbreviation":"JJJ"}}]}]},)"
                      R"({"id":"6","date":"2026-10-24T15:00Z","status":{"type":{"state":"post","completed":true,"shortDetail":"Final"}},"competitions":[{"timeValid":true,"competitors":[{"id":"11","homeAway":"home","score":"3","team":{"abbreviation":"KKK"}},{"id":"12","homeAway":"away","score":"1","team":{"abbreviation":"LLL"}}]}]}]})";
  std::vector<Pending> one;
  live.clear();
  finals.clear();
  CHECK(parse_live_games(mixed, live, &next, &finals, &one));
  CHECK_EQ(one.size(), (size_t) 1);
  CHECK_EQ(live.size(), (size_t) 1);
  CHECK_EQ(finals.size(), (size_t) 1);
  if (one.size() == 1) {
    CHECK_EQ(one[0].event_id, std::string("4"));
    CHECK_EQ(one[0].away_abbr, std::string("HHH"));
    CHECK_EQ(one[0].away_id, (uint32_t) 8);
    CHECK_EQ(one[0].tv, std::string("ESPN"));
    CHECK_EQ(next, one[0].kickoff_epoch);
  }

  // Days per sport. Thursday 10/15 2026, Eastern daylight time: at 1 AM
  // Friday football has turned over and the other leagues have not (a West
  // Coast game ending then is still Thursday's); by 5 AM all of them have.
  int64_t noon = parse_iso8601_z("2026-10-15T16:00Z");
  int64_t one_am = parse_iso8601_z("2026-10-16T05:00Z");
  int64_t four_am = parse_iso8601_z("2026-10-16T08:00Z");
  int64_t five_am = parse_iso8601_z("2026-10-16T09:00Z");
  for (const auto &info : kLeagues) {
    uint8_t l = (uint8_t) info.league;
    bool football = info.sport == Sport::FOOTBALL;
    int32_t thu = espn_day(noon, l);
    CHECK_EQ(espn_day(espn_day_epoch(thu), l), thu);
    CHECK_EQ(espn_day(one_am, l), football ? thu + 1 : thu);
    CHECK_EQ(espn_day(four_am, l), football ? thu + 1 : thu);
    CHECK_EQ(espn_day(five_am, l), thu + 1);
    // The same day the scan reads at that moment
    CHECK(scan_url(info.league, one_am).find(football ? "dates=20261016" : "dates=20261015") != std::string::npos);
    CHECK(scan_url(info.league, five_am).find("dates=20261016") != std::string::npos);
  }
  const int32_t thu = espn_day(noon, NBA);
  CHECK_EQ(espn_day(noon, NFL), thu);

  // Header words per league, each in its own day
  CHECK_EQ(std::string(recap_league_word(NFL)), std::string("NFL"));
  CHECK_EQ(std::string(recap_league_word(NCAA)), std::string("college"));
  CHECK_EQ(std::string(recap_league_word(NBA)), std::string("NBA"));
  CHECK_EQ(std::string(recap_league_word(NHL)), std::string("NHL"));
  CHECK_EQ(std::string(recap_league_word(MLB)), std::string("MLB"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::WNBA)), std::string("WNBA"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::MLS)), std::string("MLS"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::EPL)), std::string("EPL"));
  CHECK_EQ(std::string(recap_league_word(MCBB)), std::string("NCAAM"));
  RecapDay hd;
  hd.league = NHL;
  hd.day = thu - 1;
  CHECK_EQ(recap_header(hd, noon), std::string("Yesterday's NHL scores"));
  hd.league = MLB;
  hd.day = thu - 5;
  CHECK_EQ(recap_header(hd, noon), std::string("Saturday's MLB scores"));
  // At 1 AM Friday a late NBA final is still today's; Thursday's college
  // game is yesterday's
  hd.league = NBA;
  hd.day = thu;
  CHECK_EQ(recap_header(hd, one_am), std::string("Today's NBA scores"));
  hd.league = NCAA;
  CHECK_EQ(recap_header(hd, one_am), std::string("Yesterday's college scores"));
  Pending tv = mkpending(NHL, 99, 0, "BOS");
  tv.tv = "ESPN+";
  CHECK_EQ(recap_later_ticker(tv, "Today 7:00 PM"), std::string("Later today's NHL games | Today 7:00 PM | ESPN+"));
  tv.tv.clear();
  CHECK_EQ(recap_later_ticker(tv, "Today 7:00 PM"), std::string("Later today's NHL games | Today 7:00 PM"));
  tv.home_abbr = "NYR";
  CHECK_EQ(recap_matchup(tv), std::string("BOS at NYR"));
  CHECK_EQ(recap_next_text(recap_matchup(tv), "Thu 6:00 PM"), std::string("Next: BOS at NYR Thu 6:00 PM"));
  CHECK_EQ(recap_next_text("", "Thu 6:00 PM"), std::string("Next: Thu 6:00 PM"));
  Final hockey = mkfinal(NHL, 99, 0, "BOS");
  CHECK(final_snapshot(hockey).sport == Sport::HOCKEY);
  CHECK(final_snapshot(mkfinal(NCAA, 99, 0, "OSU")).sport == Sport::FOOTBALL);

  // Three leagues at noon Thursday: seven games to come and yesterday's
  // finals.
  const int64_t h = 3600;
  std::vector<RecapDay> days;
  auto finals_day = [](uint8_t league, int32_t day, int n) {
    RecapDay d;
    d.league = league;
    d.day = day;
    for (int i = 0; i < n; i++)
      d.finals.push_back(mkfinal(league, 99, i, "F"));
    return d;
  };
  recap_merge(days, finals_day(NBA, thu - 1, 3));
  recap_merge(days, finals_day(NHL, thu - 1, 13));
  recap_merge(days, finals_day(MLB, thu - 1, 5));
  std::vector<Pending> later = {
      mkpending(NBA, 99, noon + 7 * h + 1800, "N1"), mkpending(NBA, 99, noon + 8 * h + 1800, "N2"),
      mkpending(NHL, 99, noon + 7 * h, "H1"),        mkpending(NHL, 99, noon + 7 * h, "H2"),
      mkpending(NHL, 99, noon + 7 * h, "H3"),        mkpending(NHL, 99, noon + 8 * h, "H4"),
      mkpending(MLB, 99, noon + 4 * h, "B1"),
  };
  recap_absorb(days, {}, later, noon);
  const uint16_t three = recap_bit(NBA) | recap_bit(NHL) | recap_bit(MLB);
  std::vector<RecapCard> cards = recap_cards(days, three, true, noon);
  // "Later today" and the seven in kickoff order, then the finals: the
  // other 15 of the 22 shared out one by one (NBA has only 3), no Next card
  CHECK_EQ(cards.size(), (size_t) (1 + 7 + 3 + 15));
  size_t later_n = 0, finals_n[kLeagueCount] = {}, headers = 0, first_header = cards.size();
  int64_t last_kick = 0;
  bool ordered = true;
  for (size_t i = 0; i < cards.size(); i++) {
    const RecapCard &k = cards[i];
    if (k.kind == RecapKind::LATER) {
      later_n++;
      int64_t kick = days[k.day].later[k.final].kickoff_epoch;
      if (kick < last_kick || i > first_header)
        ordered = false;
      last_kick = kick;
    } else if (k.kind == RecapKind::HEADER) {
      headers++;
      if (first_header == cards.size())
        first_header = i;
    } else if (k.kind == RecapKind::FINAL) {
      finals_n[days[k.day].league]++;
    }
    CHECK(k.kind != RecapKind::NEXT);
  }
  CHECK(cards[0].kind == RecapKind::LATER_HEADER);
  CHECK_EQ(later_n, (size_t) 7);
  CHECK(ordered);
  CHECK_EQ(first_header, (size_t) 8);
  CHECK_EQ(headers, (size_t) 3);
  CHECK_EQ(finals_n[NBA], (size_t) 3);
  CHECK_EQ(finals_n[NHL], (size_t) 7);
  CHECK_EQ(finals_n[MLB], (size_t) 5);
  CHECK_EQ(recap_card_ms(RecapKind::LATER_HEADER), (uint32_t) 4000);
  CHECK_EQ(recap_card_ms(RecapKind::LATER), (uint32_t) 7000);
  if (cards.size() > 1)
    CHECK_EQ(days[cards[1].day].later[cards[1].final].away_abbr, std::string("B1"));
  // A league the mode does not read has no cards
  cards = recap_cards(days, recap_bit(NHL), false, noon);
  for (const auto &k : cards)
    if (k.kind == RecapKind::LATER || k.kind == RecapKind::FINAL || k.kind == RecapKind::HEADER)
      CHECK_EQ((int) days[k.day].league, (int) NHL);
  CHECK_EQ(cards.size(), (size_t) (1 + 4 + 1 + 13));

  // A game that has kicked off is gone the next time the cards are made
  cards = recap_cards(days, three, true, noon + 7 * h);
  later_n = 0;
  for (const auto &k : cards)
    if (k.kind == RecapKind::LATER)
      later_n++;
  CHECK_EQ(later_n, (size_t) 3);  // N1, N2 and H4 (B1 and H1-H3 have started)
  // With nothing left today the Next card comes back, after the finals
  cards = recap_cards(days, three, true, noon + 9 * h);
  CHECK(cards.front().kind == RecapKind::HEADER);
  CHECK(cards.back().kind == RecapKind::NEXT);
  CHECK_EQ(cards.size(), (size_t) (3 + 21 + 1));
  // A cap that leaves a league out gives it no header
  cards = recap_cards(days, three, false, noon + 9 * h, 2);
  CHECK_EQ(cards.size(), (size_t) 4);
  for (const auto &k : cards)
    CHECK((int) days[k.day].league != (int) MLB);

  // A later scan at 7:01 PM: B1 is final and H1 and H2 are under way. ESPN
  // still lists H3 as not started, but its time has passed, so it is not
  // kept; N1, N2 and H4 are. Today's MLB day now holds the final.
  std::vector<Final> today_finals = {mkfinal(MLB, 99, noon + 4 * h, "B1")};
  std::vector<Pending> still = {later[0], later[1], later[4], later[5]};
  recap_absorb(days, today_finals, still, noon + 7 * h + 60);
  CHECK_EQ(recap_count(days, MLB), (size_t) 6);
  size_t stored = 0;
  for (const auto &d : days)
    stored += d.later.size();
  CHECK_EQ(stored, (size_t) 3);
  // The next day the games to come are gone; the finals stay
  recap_trim(days, noon + 24 * h);
  stored = 0;
  for (const auto &d : days)
    stored += d.later.size();
  CHECK_EQ(stored, (size_t) 0);
  CHECK_EQ(recap_count(days, NHL), (size_t) 13);
  CHECK_EQ(recap_count(days, NBA), (size_t) 3);

  // Every league with two to come and a full NFL day of finals: eight
  // games to come, one from each of the first eight leagues, and the
  // finals come down to the 14 left of the 22
  std::vector<RecapDay> all;
  std::vector<Pending> two_each;
  uint16_t every = 0;
  for (const auto &info : kLeagues) {
    uint8_t l = (uint8_t) info.league;
    every |= recap_bit(l);
    two_each.push_back(mkpending(l, 99, noon + 2 * h, "A"));
    two_each.push_back(mkpending(l, 99, noon + 3 * h, "B"));
  }
  recap_merge(all, finals_day(NFL, espn_day(noon, NFL) - 4, 20));
  recap_absorb(all, {}, two_each, noon);
  stored = 0;
  for (const auto &d : all) {
    stored += d.later.size();
    CHECK(d.league != MCBB);  // the one the eight left out is not kept
  }
  CHECK_EQ(stored, kRecapLaterCap);
  cards = recap_cards(all, every, true, noon);
  later_n = 0;
  size_t nfl_finals = 0;
  for (const auto &k : cards) {
    if (k.kind == RecapKind::LATER)
      later_n++;
    else if (k.kind == RecapKind::FINAL)
      nfl_finals++;
  }
  CHECK_EQ(later_n, kRecapLaterCap);
  CHECK_EQ(nfl_finals, kRecapCap - kRecapLaterCap);
  CHECK_EQ(nfl_finals, (size_t) 14);
}

static void test_backoff() {
  // Jitter lands in the upper half of the doubled delay.
  CHECK_EQ(backoff_ms(1, false, 0, 0), 15000u);
  CHECK_EQ(backoff_ms(1, false, 0, 15000), 30000u);
  CHECK_EQ(backoff_ms(1, false, 0, 15001), 15000u);  // wraps, never past the delay
  CHECK_EQ(backoff_ms(2, false, 0, 30000), 60000u);
  CHECK_EQ(backoff_ms(3, false, 0, 60000), 120000u);
  // Ordinary failures stop doubling at 10 minutes, even after a long run.
  CHECK_EQ(backoff_ms(6, false, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(200, false, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(200, false, 0, 0), 300000u);
  // 403/429 start at 5 minutes and stop at an hour.
  CHECK_EQ(backoff_ms(1, true, 0, 150000), 300000u);
  CHECK_EQ(backoff_ms(1, true, 0, 0), 150000u);
  CHECK_EQ(backoff_ms(2, true, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(250, true, 0, 1800000), 3600000u);
  // Every jitter value stays inside its window.
  for (uint32_t r = 0; r < 100000; r += 977) {
    uint32_t w = backoff_ms(4, false, 0, r * 7919u);
    CHECK(w >= 120000u && w <= 240000u);
  }
  // Retry-After is honored when it is longer, capped at the hour, with up to
  // a quarter on top. A shorter one does not cut the backoff.
  CHECK_EQ(backoff_ms(1, true, 900000, 0), 900000u);
  CHECK_EQ(backoff_ms(1, true, 900000, 225000), 1125000u);
  CHECK_EQ(backoff_ms(1, true, 9000000, 0), 3600000u);
  CHECK_EQ(backoff_ms(1, true, 10000, 0), 150000u);
  CHECK_EQ(backoff_ms(1, false, 900000, 0), 15000u);  // only a refusal carries it
  CHECK_EQ(parse_retry_after_ms("120"), 120000u);
  CHECK_EQ(parse_retry_after_ms("0"), 0u);
  CHECK_EQ(parse_retry_after_ms(""), 0u);
  CHECK_EQ(parse_retry_after_ms("Wed, 21 Oct 2015 07:28:00 GMT"), 0u);
  CHECK_EQ(parse_retry_after_ms("1234567"), 0u);
  CHECK(is_throttle_status(403));
  CHECK(is_throttle_status(429));
  CHECK(!is_throttle_status(404));
  CHECK(!is_throttle_status(503));
  // ESPN's edge refuses most other agents: every read and logo sends this one.
  CHECK_EQ(std::string(kUserAgent), std::string("curl/8.0"));
}

// The miss count the ticker's "no update" and the page's warning go by (three
// or more). A panel cut off from ESPN for a day or two reached 255 and wrapped
// to 0, and the warning went away while it was still cut off.
static void test_miss_count() {
  uint8_t m = 0;
  m = add_miss(m);
  CHECK_EQ((int) m, 1);
  m = add_miss(add_miss(m));
  CHECK_EQ((int) m, 3);
  for (int i = 0; i < 1000; i++)
    m = add_miss(m);
  CHECK_EQ((int) m, 255);
  CHECK(m >= 3);
}

// Every wait lands in the upper half of its doubled delay, for any run length
// and any random number, so a fleet that failed together spreads out and no
// panel ever waits past its schedule. Retry-After only lengthens a refusal's
// wait, to at most the cap plus a quarter.
static void test_backoff_windows() {
  const uint32_t rnds[] = {0u, 1u, 7919u, 150000u, 2654435761u, 0xFFFFFFFFu};
  for (int throttled = 0; throttled < 2; throttled++) {
    const uint32_t base = throttled ? kThrottleBaseMs : kBackoffBaseMs;
    const uint32_t cap = throttled ? kThrottleCapMs : kBackoffCapMs;
    uint32_t d = base;  // the doubled delay for this many failures in a row
    bool inside = true;
    for (uint32_t fails = 1; fails <= 300 && inside; fails++) {
      for (uint32_t rnd : rnds) {
        uint32_t w = backoff_ms(fails, throttled != 0, 0, rnd);
        if (w < d / 2 || w > d) {
          printf("  fails %u throttled %d rnd %u: %u outside %u to %u\n", (unsigned) fails, throttled,
                 (unsigned) rnd, (unsigned) w, (unsigned) (d / 2), (unsigned) d);
          inside = false;
        }
      }
      d = d >= cap / 2 ? cap : d * 2;
    }
    CHECK(inside);
  }
  // No failure counted yet waits like the first one.
  CHECK_EQ(backoff_ms(0, false, 0, 0), backoff_ms(1, false, 0, 0));
  CHECK_EQ(backoff_ms(0, true, 0, 0xFFFFFFFFu), backoff_ms(1, true, 0, 0xFFFFFFFFu));

  // A first refusal with a Retry-After past its window: the Retry-After, up
  // to the hour, plus up to a quarter, whatever the random number.
  const uint32_t ras[] = {300001u, 900000u, kThrottleCapMs - 1, kThrottleCapMs, kThrottleCapMs + 1, 999999000u};
  for (uint32_t ra : ras) {
    const uint32_t lo = ra < kThrottleCapMs ? ra : kThrottleCapMs;
    for (uint32_t rnd : rnds) {
      uint32_t w = backoff_ms(1, true, ra, rnd);
      CHECK(w >= lo && w <= lo + lo / 4);
    }
  }
  // The longest Retry-After the header can carry still stops at the hour.
  CHECK_EQ(backoff_ms(1, true, parse_retry_after_ms("999999"), 0), kThrottleCapMs);

  // Retry-After is whole seconds and nothing else.
  CHECK_EQ(parse_retry_after_ms("999999"), 999999000u);
  CHECK_EQ(parse_retry_after_ms("000120"), 120000u);
  CHECK_EQ(parse_retry_after_ms(" 120"), 0u);
  CHECK_EQ(parse_retry_after_ms("120 "), 0u);
  CHECK_EQ(parse_retry_after_ms("+120"), 0u);
  CHECK_EQ(parse_retry_after_ms("-1"), 0u);
  CHECK_EQ(parse_retry_after_ms("1.5"), 0u);
  CHECK_EQ(parse_retry_after_ms("12a"), 0u);
}

static void test_cup_backoff() {
  // A cup job backs off when its last read failed or was refused.
  CHECK(cup_read_backs_off(true, true, false, false));   // the poll failed
  CHECK(cup_read_backs_off(true, true, false, true));    // the poll was refused
  CHECK(!cup_read_backs_off(true, true, true, false));   // the poll landed
  CHECK(!cup_read_backs_off(false, true, true, false));  // a good poll after a failed cup read
  CHECK(cup_read_backs_off(false, false, false, false)); // the cup read failed, no poll
  CHECK(cup_read_backs_off(true, false, false, true));   // today's scoreboard refused, no poll
  CHECK(!cup_read_backs_off(true, false, false, false)); // a clean cup read with no poll

  // A run of failed cup polls doubles from 30 s to the 10 minute cap.
  // rnd 0 is the low end of each window, half the doubled delay.
  Backoff b;
  const uint32_t plain[] = {15000, 30000, 60000, 120000, 240000, 300000, 300000};
  for (uint32_t want : plain) {
    CHECK(cup_read_backs_off(true, true, false, false));
    CHECK_EQ(b.fail(false, 0, 0), want);
  }
  CHECK_EQ(b.streak, (uint8_t) 7);
  CHECK(!b.throttled);
  // A 403 mid-run moves the rest of the run to the refusal schedule and keeps
  // it there while later reads fail plainly.
  CHECK_EQ(b.fail(true, 0, 0), 1800000u);
  CHECK(b.throttled);
  CHECK_EQ(b.fail(false, 0, 0), 1800000u);
  // A good poll starts the next run over.
  b.clear();
  CHECK_EQ(b.fail(false, 0, 0), 15000u);
  // A refused cup read with no poll starts at 5 minutes and honors
  // Retry-After.
  b.clear();
  CHECK(cup_read_backs_off(true, false, false, true));
  CHECK_EQ(b.fail(true, 0, 0), 150000u);
  CHECK_EQ(b.fail(true, 0, 0), 300000u);
  b.clear();
  CHECK_EQ(b.fail(true, 900000, 0), 900000u);
  // The streak stops counting at 255 and the wait stays at the cap.
  b.clear();
  for (int i = 0; i < 300; i++)
    b.fail(false, 0, 300000);
  CHECK_EQ(b.streak, (uint8_t) 255);
  CHECK_EQ(b.fail(false, 0, 300000), 600000u);
}

static void test_auto_update() {
  CHECK(version_newer("2026.10.0", "1.5.5"));
  CHECK(version_newer("1.5.6", "1.5.5"));
  CHECK(version_newer("1.10.0", "1.9.9"));
  CHECK(version_newer("2026.10.1", "2026.10.0"));
  CHECK(!version_newer("1.5.5", "1.5.5"));
  CHECK(!version_newer("1.5.5", "2026.10.0"));  // a test build ahead of the release
  CHECK(!version_newer("", "1.5.5"));
  CHECK(!version_newer("v1.5.6", "1.5.5"));
  CHECK(version_newer("1.5.5.1", "1.5.5"));
  CHECK(!version_newer("1.5.5-beta", "1.5.5"));

  CHECK(auto_update_slot(0, 0) == 0);
  CHECK(auto_update_slot(0xA9, 0x94) == (0xA994 % 60));
  for (int i = 0; i < 256; i++)
    CHECK(auto_update_slot(0x12, (uint8_t) i) < kAutoUpdateSpreadMinutes);
  CHECK(auto_update_day(2026, 286) != auto_update_day(2026, 287));
  CHECK(auto_update_day(2026, 365) != auto_update_day(2027, 1));

  // 3:20 AM, slot minute 15, tonight's read done, a newer release out
  AutoUpdateInputs in;
  in.enabled = true;
  in.clock_valid = true;
  in.hour = 3;
  in.minute = 20;
  in.today = auto_update_day(2026, 286);
  in.tried_day = auto_update_day(2026, 280);
  in.checked_day = in.today;
  in.slot = 15;
  in.check_done = true;
  in.available = true;
  in.newer = true;
  CHECK(auto_update_step(in) == AutoUpdateStep::INSTALL);

  AutoUpdateInputs t = in;
  t.enabled = false;  // off: nothing, ever
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.clock_valid = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::WAIT_GAME);
  t.hour = 5;  // the game ran past the window: not tonight
  t.minute = 0;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.tried_day = in.today;  // tried tonight already (failed, or cut off)
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.today = auto_update_day(2026, 287);  // the next night it goes again
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t = in;
  t.minute = 14;  // before this panel's minute
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.hour = 2;
  t.minute = 59;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.hour = 4;  // the second hour still counts
  t.minute = 59;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t.hour = 5;
  t.minute = 0;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.hour = 15;  // 3 PM is not 3 AM
  t.minute = 30;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.checked_day = auto_update_day(2026, 285);  // not read tonight yet
  CHECK(auto_update_step(t) == AutoUpdateStep::CHECK);
  t = in;
  t.check_done = false;  // read still out
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.available = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.newer = false;  // a different version, but not a newer one
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);

  // Daylight saving in a zone that changes at 3 or 4 AM. Spring forward
  // skips 3:00 to 3:59: a panel whose minute was in it still goes at 4:00.
  t = in;
  t.slot = 59;
  t.checked_day = auto_update_day(2026, 285);
  t.hour = 4;
  t.minute = 0;
  CHECK(auto_update_step(t) == AutoUpdateStep::CHECK);
  // Fall back repeats an hour: the try counts for the date, so the second
  // pass through 3 AM installs nothing.
  t = in;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t.tried_day = t.today;
  t.hour = 3;
  t.minute = 30;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  // New Year: a try on December 31 leaves January 1 its own night.
  t = in;
  t.tried_day = auto_update_day(2026, 365);
  t.today = auto_update_day(2027, 1);
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t.tried_day = auto_update_day(2028, 366);  // a leap year's last night
  t.today = auto_update_day(2029, 1);
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);

  // Beta channel versions: betas order by their build number after "-beta.",
  // and all come before the release they lead up to.
  CHECK(version_newer("2026.10.0-beta.813", "2026.10.0-beta.812"));
  CHECK(!version_newer("2026.10.0-beta.812", "2026.10.0-beta.812"));
  CHECK(!version_newer("2026.10.0-beta.811", "2026.10.0-beta.812"));
  CHECK(!version_newer("2026.10.1-beta.812", "2026.10.1"));  // a panel on the release skips its betas
  CHECK(version_newer("2026.10.1", "2026.10.1-beta.812"));  // Follow beta off: the release replaces its beta
  CHECK(version_newer("2026.10.1", "2026.10.1-beta.999999999"));
  CHECK(version_newer("2026.10.1-beta.1", "2026.10.0"));  // the next release's first beta
  CHECK(!version_newer("2026.10.0", "2026.10.1-beta.1"));
  CHECK(version_newer("2026.10.1-beta.1", "2026.10.1-beta.0"));  // a bar panel flashed from a dev build
  CHECK(!version_newer("2026.10.1-beta.0", "2026.10.1"));  // "-beta.0" still comes before the release
  CHECK(version_newer("2026.10.1-beta.900", "2026.10.0-beta.950"));
  CHECK(version_newer("2026.10.1-beta.900", "2026.10.0"));
  CHECK(version_newer("2026.10-beta.2", "2026.10.0-beta.1"));  // a missing part is still 0
  CHECK(!version_newer("2026.10.1-beta", "2026.10.1"));  // no dot: not the beta tag, so ignored
  CHECK(!version_newer("2026.10.1", "2026.10.1-rc.1"));  // only "-beta." marks a beta
  CHECK(!version_newer("2026.10.1-rc.1", "2026.10.1"));
  CHECK(!version_newer("1.5.5.1-beta.3", "1.5.5.1"));  // the tag also follows a fourth part
  CHECK(version_newer("1.5.5.1", "1.5.5.1-beta.3"));
  CHECK(version_newer("2026.10.0-beta.1000", "2026.10.0-beta.999"));
  CHECK(!version_newer("2026.10.0-beta.", "2026.10.0"));
  CHECK(!version_newer("2026.10.0-dev", "2026.10.0"));
  // Malformed: a number must be plain digits, right after its separator.
  CHECK(!version_newer("2026.+11.0", "2026.10.0"));
  CHECK(!version_newer("2026. 11.0", "2026.10.0"));
  CHECK(!version_newer("2026..1", "2026.0.0"));
  CHECK(!version_newer("2026.10.0-beta.+5", "2026.10.0"));
  CHECK(!version_newer("2026.10.0-beta. 5", "2026.10.0"));
  CHECK(!version_newer("2026.10.1.", "2026.10.0"));
  CHECK(!version_newer("2026.10.0-beta.1234567890", "2026.10.0-beta.5"));
  CHECK(!version_newer("2026.10.1", "2026.-10.0"));  // or the panel's own
  CHECK(version_newer("2026.10.0-beta.123456789", "2026.10.0-beta.5"));
  CHECK(version_newer("2026.10.1", "2026.10.0-dev"));  // a bench build's suffix still ends the numbers
  CHECK(!version_newer("2026.10.0", "2026.10.1-dev"));

  // Install now: names in the release summary.
  const std::string me = "gameday-aba970";
  CHECK(install_now_listed("Beta 2026.10.0-beta.812. Install now: gameday-aba970", me));
  CHECK(install_now_listed("Install now: gameday-500ebc, gameday-aba970", me));
  CHECK(install_now_listed("Install now: gameday-500ebc,gameday-aba970 ", me));
  CHECK(install_now_listed("Beta 2026.10.0-beta.812. Install now: all", me));
  CHECK(!install_now_listed("Install now: gameday-500ebc", me));
  CHECK(!install_now_listed("Install now: gameday-aba9700", me));
  CHECK(!install_now_listed("Install now: gameday-aba97", me));
  CHECK(!install_now_listed("Install now:", me));
  CHECK(!install_now_listed("Beta 2026.10.0-beta.812", me));
  CHECK(!install_now_listed("gameday-aba970", me));  // named, but not in the list
  CHECK(!install_now_listed("Install now: all", ""));
  // The publish form takes capitals (build.yml), but a panel's name is lower
  // case and network names match either way.
  CHECK(install_now_listed("Install now: Gameday-ABA970", me));
  CHECK(install_now_listed("Install now: gameday-500ebc, GAMEDAY-aba970", me));
  CHECK(install_now_listed("Install now: All", me));
  CHECK(install_now_listed("Install now: ALL", me));
  CHECK(!install_now_listed("Install now: GAMEDAY-500EBC", me));
  CHECK(!install_now_listed("Install now: ALLX", me));

  // Listed: installs at any hour, even off, unless a game is live.
  t = in;
  t.hour = 15;
  t.now = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);
  t.clock_valid = false;  // no clock needed
  t.tried_day = in.today;  // and a night try doesn't block it
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::WAIT_GAME);
  t.live = false;
  t.newer = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.newer = true;
  t.available = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.available = true;
  t.enabled = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.now = true;  // in the window too, the try counts for this version, not the night
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);

  // A release that rolled back here: skipped every night, live game or not,
  // and Install now doesn't override it.
  t = in;
  t.failed = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.live = false;
  t.today = auto_update_day(2026, 293);  // a week later, still skipped
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.check_done = false;  // nothing to say before tonight's read is back
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.failed = true;
  t.now = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.hour = 15;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
}

// Versions with a part missing or a fifth part, the publisher's list without
// a space, and a panel whose minute is the last of the first hour.
static void test_auto_update_edges() {
  CHECK(version_newer("2026.10.1", "2026.10"));  // a missing part counts as 0
  CHECK(!version_newer("2026.10", "2026.10.0"));
  CHECK(!version_newer("2026.10.0", "2026.10"));
  CHECK(version_newer("2026.010.0", "2026.9.0"));  // parts are numbers, not text
  CHECK(version_newer("10.0.0", "9.99.99"));
  CHECK(!version_newer("1.5.6", "v1.5.5"));  // a running version that isn't a number never updates
  CHECK(!version_newer("1.5.5.1.1", "1.5.5.1"));  // four parts are compared, no more

  CHECK(install_now_listed("Install now:gameday-aba970", "gameday-aba970"));
  CHECK(install_now_listed("Install now: gameday-500ebc all", "gameday-aba970"));
  CHECK(!install_now_listed("install now: gameday-aba970", "gameday-aba970"));  // the tag as CI writes it
  CHECK(install_now_listed("Install now: ALL", "gameday-aba970"));  // names and "all" match in any case

  CHECK_EQ(auto_update_slot(0xFF, 0xFF), 0xFFFF % kAutoUpdateSpreadMinutes);
  AutoUpdateInputs in;
  in.enabled = true;
  in.clock_valid = true;
  in.today = auto_update_day(2026, 300);
  in.tried_day = auto_update_day(2026, 299);
  in.checked_day = in.today;
  in.check_done = true;
  in.available = true;
  in.newer = true;
  in.slot = kAutoUpdateSpreadMinutes - 1;
  in.hour = kAutoUpdateStartHour;
  in.minute = 58;
  CHECK(auto_update_step(in) == AutoUpdateStep::IDLE);
  in.minute = 59;
  CHECK(auto_update_step(in) == AutoUpdateStep::INSTALL);
  in.hour = kAutoUpdateEndHour - 1;
  in.minute = 0;
  CHECK(auto_update_step(in) == AutoUpdateStep::INSTALL);
  in.slot = 0;
  in.hour = kAutoUpdateStartHour;
  CHECK(auto_update_step(in) == AutoUpdateStep::INSTALL);
  in.hour = kAutoUpdateStartHour - 1;
  in.minute = 59;
  CHECK(auto_update_step(in) == AutoUpdateStep::IDLE);
}

static void test_update_mirror() {
  // On the primary: nothing until a read or install fails.
  MirrorInputs in;
  in.clock_valid = true;
  CHECK(mirror_step(in) == MirrorStep::NONE);
  in.answered = true;  // a good read on the primary is none of the mirror's business
  CHECK(mirror_step(in) == MirrorStep::NONE);
  in.error = true;
  CHECK(mirror_step(in) == MirrorStep::TRY_MIRROR);
  // The boot check, before the clock is set, fails TLS everywhere: no mirror read.
  in.clock_valid = false;
  CHECK(mirror_step(in) == MirrorStep::NO_CLOCK);
  in.error = false;
  CHECK(mirror_step(in) == MirrorStep::NONE);

  // On the mirror, its read in flight (the error was cleared to start it).
  MirrorInputs m;
  m.on_mirror = true;
  CHECK(mirror_step(m) == MirrorStep::NONE);
  m.waited_ms = kMirrorWaitMs - 1;
  CHECK(mirror_step(m) == MirrorStep::NONE);
  m.waited_ms = kMirrorWaitMs;
  CHECK(mirror_step(m) == MirrorStep::TIMED_OUT);
  m.waited_ms = 5000;
  m.answered = true;
  CHECK(mirror_step(m) == MirrorStep::ANSWERED);
  m.answered = false;
  m.error = true;
  CHECK(mirror_step(m) == MirrorStep::FAILED);
  m.answered = true;  // an error wins: a failed read publishes nothing
  CHECK(mirror_step(m) == MirrorStep::FAILED);
  m.waited_ms = kMirrorWaitMs * 2;
  CHECK(mirror_step(m) == MirrorStep::FAILED);
  // millis() wraps every 49 days; the unsigned difference still counts up.
  uint32_t since = 0xFFFFF000u;
  uint32_t now = since + kMirrorWaitMs;
  m = MirrorInputs{};
  m.on_mirror = true;
  m.waited_ms = now - since;
  CHECK(mirror_step(m) == MirrorStep::TIMED_OUT);
}

static void test_update_mark() {
  UpdateMark m{};
  CHECK(update_mark_boot(m, "2026.10.0", false) == UpdateOutcome::NONE);
  CHECK(!update_failed_here(m, "2026.10.0"));
  CHECK(!update_failed_here(m, ""));

  // Installed and running it: settled only once the boot is good.
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.1", false) == UpdateOutcome::PENDING);
  CHECK_EQ(std::string(m.installing), std::string("2026.10.1"));
  CHECK(m.failed[0] == '\0');
  // The bootloader's flag can be left over from an older rollback; running
  // the new version still means it took.
  CHECK(update_mark_boot(m, "2026.10.1", true) == UpdateOutcome::PENDING);

  // Rolled back: the old firmware boots and remembers the version.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(m.installing[0] == '\0');
  CHECK_EQ(std::string(m.failed), std::string("2026.10.1"));
  CHECK(update_failed_here(m, "2026.10.1"));
  CHECK(!update_failed_here(m, "2026.10.2"));  // a later release still installs
  CHECK(!update_failed_here(m, "2026.10.1.1"));
  // Later boots keep it while the panel stays behind it.
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::NONE);
  CHECK(update_failed_here(m, "2026.10.1"));

  // A manual install of it that takes, or anything newer, drops it.
  UpdateMark k = m;
  CHECK(update_mark_boot(k, "2026.10.1", false) == UpdateOutcome::NONE);
  CHECK(k.failed[0] == '\0');
  k = m;
  update_mark_set(k.installing, "2026.10.2");
  CHECK(update_mark_boot(k, "2026.10.2", false) == UpdateOutcome::PENDING);
  CHECK(k.failed[0] == '\0');
  // A later release that rolls back too replaces it.
  k = m;
  update_mark_set(k.installing, "2026.10.2");
  CHECK(update_mark_boot(k, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK_EQ(std::string(k.failed), std::string("2026.10.2"));

  // Still on the old firmware with no rollback (power cut mid-download): not a failure.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.0", false) == UpdateOutcome::NOT_INSTALLED);
  CHECK(m.installing[0] == '\0');
  CHECK(m.failed[0] == '\0');

  // A rollback from a version that isn't newer (a manual downgrade) is
  // nothing automatic installs anyway.
  m = UpdateMark{};
  update_mark_set(m.installing, "1.5.5");
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(m.failed[0] == '\0');

  // Beta versions fit, a longer string is cut and still ends.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.0-beta.812");
  CHECK(update_mark_boot(m, "2026.10.0-beta.811", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(update_failed_here(m, "2026.10.0-beta.812"));
  CHECK(!update_failed_here(m, "2026.10.0-beta.813"));
  update_mark_set(m.installing, std::string(40, '9'));
  CHECK_EQ(strlen(m.installing), kUpdateVersionLen - 1);
  // Flash that holds garbage with no end still reads as a string.
  memset(m.failed, '7', sizeof(m.failed));
  m.installing[0] = '\0';
  update_mark_boot(m, "1.0", false);
  CHECK(strlen(m.failed) < kUpdateVersionLen);
  CHECK(m.failed[0] == '\0');  // no readable version, so it never holds back a release
  // Both fields full with no end in either: each reads as its first
  // kUpdateVersionLen - 1 bytes, never on into the next field or past the
  // mark. The bytes after the mark end so a misread stays inside `g`.
  struct {
    UpdateMark m;
    char after[8];
  } g;
  auto garbage = [&g]() {
    memset(&g, '9', sizeof(g));
    memset(g.m.failed, '7', sizeof(g.m.failed));
    g.after[sizeof(g.after) - 1] = '\0';
  };
  garbage();
  CHECK(update_mark_boot(g.m, std::string(kUpdateVersionLen - 1, '9'), false) == UpdateOutcome::PENDING);
  CHECK_EQ(strlen(g.m.installing), kUpdateVersionLen - 1);
  CHECK(g.m.failed[0] == '\0');  // 23 sevens is no version
  garbage();
  CHECK(update_mark_boot(g.m, "1.0", false) == UpdateOutcome::NOT_INSTALLED);
  CHECK(g.m.installing[0] == '\0');
  CHECK(g.m.failed[0] == '\0');
  garbage();
  CHECK(update_mark_boot(g.m, "1.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(g.m.installing[0] == '\0');
  CHECK(g.m.failed[0] == '\0');  // nor is 23 nines, so nothing is held back
  garbage();
  update_mark_terminate(g.m);
  CHECK_EQ(strlen(g.m.installing), kUpdateVersionLen - 1);
  CHECK_EQ(strlen(g.m.failed), kUpdateVersionLen - 1);
}

// Every byte of a panel_text() result is printable ASCII or part of a Latin-1
// character the fonts list (U+00A1 to U+00FF without the soft hyphen), the
// only glyphs the panel's fonts carry.
static bool panel_drawable(const std::string &s) {
  for (size_t i = 0; i < s.size(); i++) {
    const uint8_t c = (uint8_t) s[i];
    if (c >= 0x20 && c < 0x7F)
      continue;
    if ((c == 0xC2 || c == 0xC3) && i + 1 < s.size() && ((uint8_t) s[i + 1] & 0xC0) == 0x80) {
      if (c == 0xC2 && ((uint8_t) s[i + 1] < 0xA1 || (uint8_t) s[i + 1] == 0xAD))
        return false;
      i++;
      continue;
    }
    return false;
  }
  return true;
}

static void test_boot_log() {
  const int64_t now = 1792000000;  // 2026-10-16
  const uint32_t build_a = 111, build_b = 222;
  // RTC memory after a power cut is noise; the log starts over.
  BootLog log;
  memset(&log, 0xA5, sizeof(log));
  CHECK(!boot_log_valid(log));
  CHECK(boot_log_start(log, kResetPowerOn, build_a, 0) == Restart::POWER);
  CHECK(boot_log_valid(log));
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  CHECK_EQ(boot_log_fault_ago(log, now, 50), (int64_t) -1);

  // A crash: kept, and as old as the uptime while it's this start.
  CHECK(boot_log_start(log, kResetPanic, build_a, now) == Restart::CRASH);
  CHECK_EQ(log.boots, 2u);
  CHECK(log.fault == (uint8_t) Restart::CRASH);
  CHECK_EQ(boot_log_fault_ago(log, now + 600, 600), (int64_t) 600);
  // The owner presses Reboot: the start says restart, the crash stays.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetSoftware, build_a, now + 3600) == Restart::RESTART);
  CHECK(log.fault == (uint8_t) Restart::CRASH);
  CHECK_EQ(boot_log_fault_ago(log, now + 7200, 3600), (int64_t) 7200);
  // Without a clock, an earlier fault's age isn't known.
  CHECK_EQ(boot_log_fault_ago(log, 0, 3600), (int64_t) -1);

  // The S3 sometimes reports esp_restart() as a watchdog; the mark says not.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::RESTART);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::WATCHDOG);  // unmarked
  CHECK(log.fault == (uint8_t) Restart::WATCHDOG);

  // A planned restart into a different build is an update, either way.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::UPDATE);
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::UPDATE);
  // An unmarked software reset is still a restart, and the mark is used once.
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::RESTART);
  CHECK_EQ((int) log.planned, 0);

  // A brownout at a start with no clock yet gets its time once it's set.
  CHECK(boot_log_start(log, kResetBrownout, build_b, 0) == Restart::BROWNOUT);
  CHECK_EQ(log.fault_at, (int64_t) 0);
  boot_log_clock(log, 1000);  // not a real time
  CHECK_EQ(log.fault_at, (int64_t) 0);
  boot_log_clock(log, now);
  CHECK_EQ(log.fault_at, (int64_t) now);
  boot_log_clock(log, now + 5);  // only the first
  CHECK_EQ(log.fault_at, (int64_t) now);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now + 60) == Restart::RESTART);
  boot_log_clock(log, now + 60);  // not this start's fault
  CHECK_EQ(log.fault_at, (int64_t) now);
  CHECK_EQ(boot_log_fault_ago(log, now + 90, 30), (int64_t) 90);
  CHECK(boot_log_start(log, kResetPowerGlitch, build_b, now) == Restart::BROWNOUT);
  // A fault at a start that already had the clock keeps that time.
  boot_log_clock(log, now + 30);
  CHECK_EQ(log.fault_at, (int64_t) now);
  // A clock that reads before the fault (set back, or wrong) gives no age.
  CHECK(boot_log_start(log, kResetSoftware, build_b, now + 60) == Restart::RESTART);
  CHECK_EQ(boot_log_fault_ago(log, now - 100, 10), (int64_t) -1);
  CHECK_EQ(boot_log_fault_ago(log, now + 100, 10), (int64_t) 100);

  // Power: a log that survived it is cleared anyway, faults and all.
  CHECK(boot_log_start(log, kResetPowerOn, build_b, now) == Restart::POWER);
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  // A flipped bit and the log starts over.
  CHECK(boot_log_start(log, kResetPanic, build_b, now) == Restart::CRASH);
  log.boots ^= 4;
  CHECK(!boot_log_valid(log));
  CHECK_EQ(boot_log_fault_ago(log, now, 10), (int64_t) -1);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::RESTART);
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  memset(&log, 0, sizeof(log));
  boot_log_planned(log);  // no effect on an invalid log
  CHECK(!boot_log_valid(log));

  CHECK(classify_restart(kResetTaskWdt, true, false) == Restart::WATCHDOG);
  CHECK(classify_restart(kResetIntWdt, false, false) == Restart::WATCHDOG);
  CHECK(classify_restart(kResetCpuLockup, false, false) == Restart::CRASH);
  CHECK(classify_restart(kResetUsb, false, false) == Restart::USB);
  CHECK(classify_restart(kResetExternal, false, false) == Restart::POWER);
  CHECK(classify_restart(kResetDeepSleep, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetUnknown, true, true) == Restart::OTHER);
  CHECK(classify_restart(kResetJtag, false, false) == Restart::USB);
  CHECK(classify_restart(kResetSdio, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetEfuse, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetSoftware, true, false) == Restart::RESTART);
  CHECK(classify_restart(kResetSoftware, false, true) == Restart::RESTART);  // unmarked: not an update
  CHECK(classify_restart(99, false, false) == Restart::OTHER);
  CHECK_EQ(std::string(restart_key(Restart::BROWNOUT)), std::string("brownout"));
  CHECK_EQ(std::string(restart_key(Restart::OTHER)), std::string("other"));
}

// Every reset reason the chip can give lands somewhere, and the keys are the
// ones the device page and the app name (RESTARTS in app.js).
static void test_restart_reasons() {
  CHECK(classify_restart(kResetUnknown, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetSdio, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetEfuse, false, false) == Restart::OTHER);
  CHECK(classify_restart(kResetJtag, false, false) == Restart::USB);
  CHECK(classify_restart(-1, true, true) == Restart::OTHER);
  // The restart mark only tells a software reset or the S3's stray watchdog
  // apart. A crash, a hang or a power dip on the way down is still one, and
  // power on is power on.
  CHECK(classify_restart(kResetPanic, true, true) == Restart::CRASH);
  CHECK(classify_restart(kResetIntWdt, true, false) == Restart::WATCHDOG);
  CHECK(classify_restart(kResetBrownout, true, false) == Restart::BROWNOUT);
  CHECK(classify_restart(kResetPowerGlitch, true, true) == Restart::BROWNOUT);
  CHECK(classify_restart(kResetPowerOn, true, true) == Restart::POWER);
  CHECK(classify_restart(kResetUsb, true, false) == Restart::USB);
  // Unmarked, a new build is no update: the firmware didn't ask for it.
  CHECK(classify_restart(kResetSoftware, false, true) == Restart::RESTART);
  CHECK(classify_restart(kResetWdt, false, true) == Restart::WATCHDOG);
  // Only a crash, a hang or a power dip is kept as the last fault.
  for (uint8_t r = 0; r <= (uint8_t) Restart::OTHER; r++) {
    const Restart k = (Restart) r;
    CHECK_EQ(restart_is_fault(k), k == Restart::CRASH || k == Restart::WATCHDOG || k == Restart::BROWNOUT);
  }
  const char *keys[] = {"none", "power", "restart", "update", "crash", "watchdog", "brownout", "usb", "other"};
  for (uint8_t r = 0; r <= (uint8_t) Restart::OTHER; r++)
    CHECK_EQ(std::string(restart_key((Restart) r)), std::string(keys[r]));
  // A sealed log is valid only with a fault this firmware knows.
  BootLog log{};
  log.magic = kBootLogMagic;
  log.fault = (uint8_t) Restart::OTHER;
  boot_log_seal(log);
  CHECK(boot_log_valid(log));
  log.fault = (uint8_t) Restart::OTHER + 1;
  boot_log_seal(log);
  CHECK(!boot_log_valid(log));
}

static void test_health_json() {
  Health h{};
  h.uptime_s = 7260;
  h.start = Restart::RESTART;
  h.boots = 3;
  h.fault = Restart::BROWNOUT;
  h.fault_ago = 9000;
  h.heap_kb = 143.24f;
  h.block_kb = 40.0f;
  h.psram_kb = 4100.5f;
  h.wifi = true;
  h.rssi = -58;
  h.espn_status = 200;
  h.espn_ago = 9;
  h.version = "2026.10.0";
  h.built = 1792285260;  // 2026-10-18 1:01 AM UTC
  h.channel = "beta";
  char buf[512];
  int n = health_json(buf, sizeof(buf), h);
  CHECK(n > 0 && n < (int) sizeof(buf));
  CHECK_EQ(std::string(buf),
           std::string("{\"up\":7260,\"start\":\"restart\",\"boots\":3,\"fault\":\"brownout\",\"fault_ago\":9000,"
                       "\"heap_kb\":143.2,\"block_kb\":40.0,\"psram_kb\":4100.5,\"rssi\":-58,\"espn\":200,"
                       "\"espn_ago\":9,\"version\":\"2026.10.0\",\"built\":1792285260,"
                       "\"channel\":\"beta\"}"));
  // Parses, and the optional keys go when they mean nothing.
  JsonDocument doc;
  CHECK(!deserializeJson(doc, buf));
  CHECK_EQ(doc["rssi"].as<int>(), -58);
  h.fault_ago = -1;
  h.wifi = false;
  h.espn_ago = -1;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  CHECK(!deserializeJson(doc, buf));
  CHECK_EQ(std::string(doc["fault"].as<const char *>()), std::string("brownout"));
  CHECK(doc["fault_ago"].isNull());
  CHECK(doc["rssi"].isNull());
  CHECK(doc["espn"].isNull());
  h.fault = Restart::NONE;
  h.espn_status = 0;  // no answer
  h.espn_ago = 30;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  CHECK(!deserializeJson(doc, buf));
  CHECK(doc["fault"].isNull());
  CHECK_EQ(doc["espn"].as<int>(), 0);
  CHECK_EQ(doc["espn_ago"].as<int>(), 30);

  // The longest it gets fits the firmware's buffer: every optional key, the
  // widest numbers each field can hold (an ESPN read's age is millis() / 1000
  // and its status three digits) and a version as long as the crash report
  // keeps.
  h.uptime_s = INT64_MAX;
  h.start = Restart::WATCHDOG;
  h.boots = UINT32_MAX;
  h.fault = Restart::WATCHDOG;
  h.fault_ago = INT64_MAX;
  h.heap_kb = 512.0f;
  h.block_kb = 512.0f;
  h.psram_kb = 32768.0f;
  h.wifi = true;
  h.rssi = INT32_MIN;
  h.espn_status = 999;
  h.espn_ago = UINT32_MAX / 1000;
  h.mlb_status = 999;
  h.mlb_ago = UINT32_MAX / 1000;
  h.version = "2026.12.31-beta.1234567";
  h.built = INT64_MAX;
  h.channel = "stable";
  n = health_json(buf, sizeof(buf), h);
  CHECK(n > 0 && n < (int) kHealthJsonSize);
  doc.clear();
  CHECK(!deserializeJson(doc, buf));
  CHECK_EQ(std::string(doc["channel"].as<const char *>()), std::string("stable"));
  CHECK_EQ(doc["espn_ago"].as<int64_t>(), (int64_t) (UINT32_MAX / 1000));
  CHECK_EQ(doc["fault_ago"].as<int64_t>(), INT64_MAX);
  // Too small a buffer says so, as snprintf does.
  CHECK(health_json(buf, 64, h) >= 64);
}

// The crash handler's console line for one of its ESP_LOGE calls, as
// ESPHome's logger hands it to a log listener: color, header, text, reset.
static std::string crash_line(const std::string &text) {
  return "\033[1;31m[E][esp32.crash:439]: " + text + "\033[0m";
}

static size_t crash_feed(char *report, size_t size, size_t len, const std::string &line) {
  return esphome::crash_report::report_append(report, size, len, line.data(), line.size());
}

static void test_crash_capture() {
  using namespace esphome::crash_report;
  // What the crash handler logs for a fault on core 0, with the other core's
  // backtrace and the two addr2line hints (ESPHome 2026.10).
  const char *lines[] = {
      "*** CRASH DETECTED ON PREVIOUS BOOT ***",
      "  Reason: Fault - LoadProhibited (cause 28)",
      "  Crashed core: 0",
      "  PC:  0x40055EBF  (fault location)",
      "  EXCVADDR: 0x00000008  (faulting address)",
      "  BT0: 0x40386CDF  (backtrace)",
      "  BT1: 0x421736DD  (backtrace)",
      "  Other core (1) backtrace:",
      "  BT0: 0x4038665D  (backtrace)",
      "Use: addr2line -pfiaC -e firmware.elf 0x40055EBF 0x40386CDF 0x421736DD",
      "Other core: addr2line -pfiaC -e firmware.elf 0x4038665D",
  };
  char report[REPORT_SIZE];
  report[0] = '\0';
  size_t len = 0;
  for (const char *l : lines)
    len = crash_feed(report, sizeof(report), len, crash_line(l));
  // The text of each line, one per line, without the hints; the same text
  // scripts/crash_report.py --selftest reads.
  const std::string want =
      "*** CRASH DETECTED ON PREVIOUS BOOT ***\n"
      "  Reason: Fault - LoadProhibited (cause 28)\n"
      "  Crashed core: 0\n"
      "  PC:  0x40055EBF  (fault location)\n"
      "  EXCVADDR: 0x00000008  (faulting address)\n"
      "  BT0: 0x40386CDF  (backtrace)\n"
      "  BT1: 0x421736DD  (backtrace)\n"
      "  Other core (1) backtrace:\n"
      "  BT0: 0x4038665D  (backtrace)\n";
  CHECK_EQ(std::string(report), want);
  CHECK_EQ(len, want.size());

  // A line without the header's ": " is not one of the handler's; one
  // without the color reset keeps all its text.
  len = crash_feed(report, sizeof(report), len, "no header here\033[0m");
  CHECK_EQ(len, want.size());
  len = crash_feed(report, sizeof(report), len, "[E][esp32.crash:1]: plain");
  CHECK_EQ(std::string(report), want + "plain\n");

  // The longest report the handler writes still fits: the longest reason,
  // a faulting address and 16 backtrace addresses for each core.
  std::string full = crash_line("*** CRASH DETECTED ON PREVIOUS BOOT ***");
  len = 0;
  len = crash_feed(report, sizeof(report), len, full);
  len = crash_feed(report, sizeof(report), len,
                   crash_line("  Reason: Debug exception - Interrupt wdt timeout on CPU0 (cause 63)"));
  len = crash_feed(report, sizeof(report), len, crash_line("  Crashed core: 1"));
  len = crash_feed(report, sizeof(report), len, crash_line("  PC:  0x40055EBF  (fault location)"));
  len = crash_feed(report, sizeof(report), len, crash_line("  EXCVADDR: 0x3FCEBF00  (faulting address)"));
  for (int core = 0; core < 2; core++) {
    if (core == 1)
      len = crash_feed(report, sizeof(report), len, crash_line("  Other core (0) backtrace:"));
    for (int i = 0; i < 16; i++) {
      char bt[64];
      snprintf(bt, sizeof(bt), "  BT%d: 0x%08X  (backtrace)", i, 0x42000000u + i * 0x100u);
      len = crash_feed(report, sizeof(report), len, crash_line(bt));
    }
  }
  CHECK(len < REPORT_SIZE);
  const std::string last = "  BT15: 0x42000F00  (backtrace)\n";
  CHECK_EQ(std::string(report).substr(len - last.size()), last);

  // A line that doesn't fit is left out, and the report stays terminated.
  char small[64];
  small[0] = '\0';
  len = crash_feed(small, sizeof(small), 0, crash_line("*** CRASH DETECTED ON PREVIOUS BOOT ***"));
  len = crash_feed(small, sizeof(small), len, crash_line("  Reason: Abort"));
  CHECK_EQ(len, (size_t) 56);
  len = crash_feed(small, sizeof(small), len, crash_line("  Crashed core: 0"));
  CHECK_EQ(len, (size_t) 56);
  CHECK_EQ(std::string(small), std::string("*** CRASH DETECTED ON PREVIOUS BOOT ***\n  Reason: Abort\n"));

  // The starts that read the crash handler, and their names.
  CHECK(reset_crashed(kResetPanic) && reset_crashed(kResetIntWdt) && reset_crashed(kResetTaskWdt) &&
        reset_crashed(kResetWdt));
  CHECK(!reset_crashed(kResetPowerOn) && !reset_crashed(kResetSoftware) && !reset_crashed(kResetBrownout));
  CHECK_EQ(std::string(reason_name(kResetPanic)), std::string("crash"));
  CHECK_EQ(std::string(reason_name(kResetTaskWdt)), std::string("task watchdog"));
  CHECK_EQ(std::string(reason_name(kResetBrownout)), std::string("brownout"));
  CHECK_EQ(std::string(reason_name(kResetSoftware)), std::string("other"));
}

static void test_crash_save() {
  using namespace esphome::crash_report;
  CHECK(save_for_start(kResetPanic, false) == Save::REPORT);
  CHECK(save_for_start(kResetPanic, true) == Save::REPORT);  // a crash on the way down is still one
  CHECK(save_for_start(kResetTaskWdt, true) == Save::REPORT);
  CHECK(save_for_start(kResetIntWdt, false) == Save::REPORT);
  CHECK(save_for_start(kResetWdt, false) == Save::REPORT);
  // The owner's Reboot or an update that came back as the RTC watchdog.
  CHECK(save_for_start(kResetWdt, true) == Save::NOTHING);
  CHECK(save_for_start(kResetBrownout, false) == Save::REASON);
  CHECK(save_for_start(kResetBrownout, true) == Save::REASON);
  CHECK(save_for_start(kResetPowerOn, false) == Save::NOTHING);
  CHECK(save_for_start(kResetSoftware, true) == Save::NOTHING);
  CHECK(save_for_start(kResetUsb, false) == Save::NOTHING);
  // The crash report saves exactly the starts the health block calls a
  // fault (health.h), for every reason the S3 gives (up to kResetEfuse).
  for (int reason = kResetUnknown; reason <= kResetEfuse; reason++) {
    for (int planned = 0; planned < 2; planned++) {
      const bool saves = save_for_start(reason, planned != 0) != Save::NOTHING;
      const bool fault = restart_is_fault(classify_restart(reason, planned != 0, false));
      if (saves != fault)
        printf("  reason %d planned %d: crash report %d, health %d\n", reason, planned, (int) saves, (int) fault);
      CHECK(saves == fault);
    }
  }
  // The time saved is the start's, also when the clock comes minutes later.
  const int64_t now = 1792000000;  // 2026-10-16
  CHECK_EQ(boot_time(now, 2), (uint32_t) (now - 2));
  CHECK_EQ(boot_time(now + 3600, 3600), (uint32_t) now);
  CHECK_EQ(boot_time(1000, 5), 0u);  // no clock yet
  CHECK_EQ(boot_time(CLOCK_VALID - 1, 0), 0u);
}

static void test_heat_guard() {
  HeatGuard g;
  uint32_t t = 1000;
  CHECK(!g.feed(NAN, t));
  CHECK(!g.feed(60.0f, t));
  CHECK_EQ((int) g.step, 0);
  // The first hot reading steps down at once, then one step per kHotStepMs.
  CHECK(g.feed(kHotC, t += 15000));
  CHECK_EQ((int) g.step, 1);
  CHECK(!g.feed(kHotC + 1, t += kHotStepMs - 1));
  CHECK(g.feed(kHotC + 1, t += 1));
  CHECK_EQ((int) g.step, 2);
  CHECK(g.feed(kHotC + 1, t += kHotStepMs));
  CHECK(!g.feed(kHotC + 1, t += kHotStepMs));  // already the dimmest
  CHECK_EQ((int) g.step, (int) kHeatSteps);
  // Between the two limits it holds, however long.
  CHECK(!g.feed(kCoolC + 1, t += 10 * kCoolStepMs));
  CHECK_EQ((int) g.step, (int) kHeatSteps);
  // Under kCoolC it waits a whole kCoolStepMs before each step back.
  CHECK(!g.feed(kCoolC - 1, t += 15000));
  CHECK(!g.feed(kCoolC - 1, t += kCoolStepMs - 15001));
  CHECK(g.feed(kCoolC - 1, t += 15001));
  CHECK_EQ((int) g.step, (int) kHeatSteps - 1);
  CHECK(!g.feed(kCoolC - 1, t += kCoolStepMs - 1));
  CHECK(g.feed(kCoolC - 1, t += 1));
  // Warm again in between restarts the wait.
  CHECK(!g.feed(kCoolC, t += kCoolStepMs));
  CHECK(!g.feed(kCoolC - 1, t += 15000));
  CHECK_EQ((int) g.step, 1);
  CHECK(g.feed(kCoolC - 1, t += kCoolStepMs));
  CHECK_EQ((int) g.step, 0);
  CHECK(!g.feed(20.0f, t += kCoolStepMs));
  // Past the floor it goes straight to the dimmest step.
  CHECK(g.feed(kHotFloorC, t += 15000));
  CHECK_EQ((int) g.step, (int) kHeatSteps);
  CHECK(g.last_c == kHotFloorC);
  // millis() wrapping doesn't stall it.
  HeatGuard w;
  uint32_t m = 0xFFFFFFFFu - 1000;
  CHECK(w.feed(kHotC, m));
  CHECK(w.feed(kHotC, m + kHotStepMs));
  CHECK_EQ((int) w.step, 2);
}

// The first cool reading landing on millis() == 0 (at the wrap) starts the
// cool wait there, the same as at any other moment: no step back up until it
// has read cool for a whole kCoolStepMs.
static void test_heat_guard_cool_at_zero() {
  HeatGuard g;
  const uint32_t hot_at = 0u - 6 * 60 * 1000;  // six minutes before the wrap
  CHECK(g.feed(kHotC, hot_at));
  CHECK_EQ((int) g.step, 1);
  CHECK(!g.feed(kCoolC - 1, 0));
  CHECK_EQ((int) g.step, 1);
  CHECK(!g.feed(kCoolC - 1, kCoolStepMs - 1));
  CHECK(g.feed(kCoolC - 1, kCoolStepMs));
  CHECK_EQ((int) g.step, 0);
}

static void test_power_log() {
  PowerLog log{};
  CHECK(!power_log_start(log, false));
  CHECK(power_log_start(log, true));
  CHECK_EQ(log.brownouts, 1u);
  CHECK_EQ((int) log.capped, 0);
  // A settled run ends the streak; the count stays.
  CHECK(power_log_settled(log));
  CHECK(!power_log_settled(log));
  CHECK(power_log_start(log, true));
  CHECK_EQ((int) log.capped, 0);
  // Two in a row cap it, and power cuts in between don't count either way.
  CHECK(!power_log_start(log, false));
  CHECK(power_log_start(log, true));
  CHECK_EQ((int) log.capped, 1);
  CHECK_EQ(log.brownouts, 3u);
  // Settling doesn't lift the cap; only the owner does.
  CHECK(power_log_settled(log));
  CHECK_EQ((int) log.capped, 1);
  CHECK(power_log_clear(log));
  CHECK(!power_log_clear(log));
  CHECK_EQ((int) log.capped, 0);
  CHECK_EQ(log.brownouts, 3u);
  // The streak doesn't wrap.
  log.streak = 255;
  power_log_start(log, true);
  CHECK_EQ((int) log.streak, 255);
}

static void test_guarded_brightness() {
  CHECK_EQ((int) guarded_brightness(128, 0, false), 128);
  CHECK_EQ((int) guarded_brightness(255, 0, false), 255);
  CHECK_EQ((int) guarded_brightness(200, 1, false), 140);
  CHECK_EQ((int) guarded_brightness(200, kHeatSteps, false), 50);
  CHECK_EQ((int) guarded_brightness(200, 9, false), 50);  // past the last step
  CHECK_EQ((int) guarded_brightness(200, 0, true), (int) kPowerCap);
  CHECK_EQ((int) guarded_brightness(40, 0, true), 40);  // already under the cap
  CHECK_EQ((int) guarded_brightness(255, kHeatSteps, true), (int) kPowerCap);
  CHECK_EQ((int) guarded_brightness(1, kHeatSteps, true), 1);  // never off
  CHECK(guard_mark(0, false) == GuardMark::NONE);
  CHECK(guard_mark(2, false) == GuardMark::HEAT);
  CHECK(guard_mark(2, true) == GuardMark::POWER);

  char buf[96];
  PowerLog log{};
  log.brownouts = 4;
  log.capped = 1;
  guard_json_keys(buf, sizeof(buf), 47.25f, 2, log);
  CHECK_EQ(std::string(buf), std::string(",\"temp\":47.2,\"heat\":2,\"brownouts\":4,\"power\":\"capped\""));
  log.capped = 0;
  guard_json_keys(buf, sizeof(buf), NAN, 0, log);
  CHECK_EQ(std::string(buf), std::string(",\"heat\":0,\"brownouts\":4,\"power\":\"ok\""));
}

// Full brightness at each heat step, the cap's edge, a panel turned off, and
// a missed temperature reading in the middle of a cool-down.
static void test_guard_steps() {
  const int full[kHeatSteps + 1] = {255, 179, 115, 64};
  for (uint8_t s = 0; s <= kHeatSteps; s++)
    CHECK_EQ((int) guarded_brightness(255, s, false), full[s]);
  CHECK_EQ((int) guarded_brightness(kPowerCap, 0, true), (int) kPowerCap);
  CHECK_EQ((int) guarded_brightness(kPowerCap + 1, 0, true), (int) kPowerCap);
  CHECK_EQ((int) guarded_brightness(0, kHeatSteps, true), 0);  // off stays off
  CHECK_EQ((int) guarded_brightness(2, kHeatSteps, false), 1);
  CHECK(guard_mark(0, true) == GuardMark::POWER);

  // A reading the sensor missed (NAN) neither restarts the cool-down nor
  // counts as warm.
  HeatGuard g;
  uint32_t t = 5000;
  CHECK(g.feed(kHotFloorC, t));
  CHECK_EQ((int) g.step, (int) kHeatSteps);
  t += kCoolStepMs;
  CHECK(!g.feed(kCoolC - 5, t));  // cool from here
  CHECK(!g.feed(NAN, t + kCoolStepMs / 2));
  CHECK(g.last_c == kCoolC - 5);
  CHECK(g.feed(kCoolC - 5, t + kCoolStepMs));
  CHECK_EQ((int) g.step, (int) kHeatSteps - 1);

  // The keys fit the buffer guard.h sets for health_json_, cold or hot.
  char buf[kGuardJsonSize];
  PowerLog log{};
  log.brownouts = 0xFFFFFFFFu;
  log.capped = 1;
  int n = guard_json_keys(buf, sizeof(buf), -40.0f, kHeatSteps, log);
  CHECK(n > 0 && n < (int) sizeof(buf));
  CHECK_EQ(std::string(buf), std::string(",\"temp\":-40.0,\"heat\":3,\"brownouts\":4294967295,\"power\":\"capped\""));
  n = guard_json_keys(buf, sizeof(buf), 125.0f, 0, log);
  CHECK(n > 0 && n < (int) sizeof(buf));
}

// JSON has no inf or nan, so a reading that is neither a number nor finite
// leaves temp out like no reading at all, and the state document stays JSON.
static void test_guard_json_finite() {
  char buf[kGuardJsonSize];
  PowerLog log{};
  const std::string no_temp = ",\"heat\":3,\"brownouts\":0,\"power\":\"ok\"";
  for (float c : {INFINITY, -INFINITY, NAN}) {
    int n = guard_json_keys(buf, sizeof(buf), c, kHeatSteps, log);
    CHECK(n > 0 && n < (int) sizeof(buf));
    CHECK_EQ(std::string(buf), no_temp);
  }
  guard_json_keys(buf, sizeof(buf), 0.0f, kHeatSteps, log);
  CHECK_EQ(std::string(buf), std::string(",\"temp\":0.0") + no_temp);
}

static void test_offseason() {
  const int64_t now = 1792080000;  // any fixed instant
  const int64_t day = 24 * 3600;
  const int64_t football = season_horizon(Sport::FOOTBALL);
  const int64_t hockey = season_horizon(Sport::HOCKEY);
  CHECK_EQ(football, (int64_t) 0);
  CHECK_EQ(hockey, kSeasonHorizon);
  CHECK_EQ(season_horizon(Sport::BASEBALL), kSeasonHorizon);
  // The team endpoint named no game: the clock, whatever the sport.
  CHECK(off_season(GameState::NOT_FOUND, 0, now, football));
  CHECK(off_season(GameState::NOT_FOUND, 0, now, hockey));
  // Football has no horizon: a game 20 days out stays on its card, and so
  // does an old final.
  CHECK(!off_season(GameState::PRE, now + 20 * day, now, football));
  CHECK(!off_season(GameState::POST, now - 60 * day, now, football));
  // An NHL team 11 days out reaches the clock; 9 days, or exactly 10, does not.
  CHECK(off_season(GameState::PRE, now + 11 * day, now, hockey));
  CHECK(!off_season(GameState::PRE, now + 9 * day, now, hockey));
  CHECK(!off_season(GameState::PRE, now + 10 * day, now, hockey));
  // A final older than the horizon is the end of the season.
  CHECK(off_season(GameState::POST, now - 11 * day, now, hockey));
  CHECK(!off_season(GameState::POST, now - 2 * day, now, hockey));
  CHECK(!off_season(GameState::IN, now - 11 * day, now, hockey));

  // The Next line. Wed Sep 9 2026, 10:00 AM.
  struct tm wed = mk(2026, 251, 3, 10, 0, 8, 9);
  CHECK_EQ(next_line("DAL", "PHI", true, mk(2026, 255, 0, 15, 25, 8, 13), wed),
           std::string("Next: DAL vs PHI Sun 3:25 PM"));
  CHECK_EQ(next_line("DAL", "PHI", true, mk(2026, 251, 3, 19, 0, 8, 9), wed),
           std::string("Next: DAL vs PHI Today 7:00 PM"));
  CHECK_EQ(next_line("BOS", "NYR", false, mk(2026, 252, 4, 0, 5, 8, 10), wed),
           std::string("Next: at NYR Tomorrow 12:05 AM"));
  // Months out, the date: Thu Oct 15 2026 to Fri Mar 26 2027.
  struct tm oct15 = mk(2026, 287, 4, 21, 30, 9, 15);
  struct tm mar26 = mk(2027, 84, 5, 19, 5, 2, 26);
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15), std::string("Next: at PHI Mar 26 7:05 PM"));
  // The board puts the kickoff on its own line.
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, "\n"), std::string("Next: at PHI\nMar 26 7:05 PM"));
  CHECK_EQ(next_line("COL", "LAD", true, mar26, oct15, "\n"), std::string("Next: COL vs LAD\nMar 26 7:05 PM"));
  // An opponent ESPN has not named yet.
  CHECK_EQ(next_line("COL", "", true, mar26, oct15), std::string("Next game Mar 26 7:05 PM"));
  // A game with no time yet shows TBD, not ESPN's midnight placeholder.
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, " ", true), std::string("Next: at PHI Mar 26 TBD"));
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, "\n", true), std::string("Next: at PHI\nMar 26 TBD"));

  // The clock itself: 12-hour, no seconds, no leading zero.
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 0, 5, 9, 15)), std::string("12:05 AM"));
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 9, 7, 9, 15)), std::string("9:07 AM"));
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 12, 0, 9, 15)), std::string("12:00 PM"));
  CHECK_EQ(clock_time_text(oct15), std::string("9:30 PM"));
  CHECK_EQ(clock_date_text(oct15), std::string("Thu, Oct 15"));
  CHECK_EQ(clock_date_text(mar26), std::string("Fri, Mar 26"));

  // Team colors, lifted when dark; neutral gray with no team.
  CHECK_EQ(clock_color(false, 0xE31837), kClockNeutral);
  CHECK_EQ(clock_color(false, 0xFFFFFF), kClockNeutral);
  CHECK_EQ(clock_color(true, 0xE31837), (uint32_t) 0xE31837);
  CHECK_EQ(clock_color(true, 0x002A5C), (uint32_t) 0x0057C0);
  CHECK_EQ(clock_color(true, 0x000000), (uint32_t) 0xFFFFFF);
  CHECK_EQ(clock_color(true, parse_color("")), (uint32_t) 0xFFFFFF);  // a team with no color from ESPN
}

// The off-season clock's edges: the horizon to the second on either side of
// a season, a home game with no abbreviation of our own, the last minute of
// the day, and colors at the lift's edge.
static void test_offseason_edges() {
  const int64_t now = 1792080000;
  const int64_t h = kSeasonHorizon;
  CHECK(!off_season(GameState::POST, now - h, now, h));
  CHECK(off_season(GameState::POST, now - h - 1, now, h));
  CHECK(!off_season(GameState::PRE, now + h, now, h));
  CHECK(off_season(GameState::PRE, now + h + 1, now, h));
  CHECK(!off_season(GameState::PRE, now + 400 * 86400, now, -1));  // no horizon either way
  CHECK(off_season(GameState::NOT_FOUND, 0, now, -1));

  struct tm wed = mk(2026, 251, 3, 10, 0, 8, 9);
  CHECK_EQ(next_line("", "PHI", true, mk(2026, 251, 3, 19, 0, 8, 9), wed),
           std::string("Next: vs PHI Today 7:00 PM"));
  CHECK_EQ(next_line("", "", false, mk(2026, 252, 4, 19, 0, 8, 10), wed),
           std::string("Next game Tomorrow 7:00 PM"));
  CHECK_EQ(clock_time_text(mk(2026, 251, 3, 23, 59, 8, 9)), std::string("11:59 PM"));
  CHECK_EQ(clock_time_text(mk(2026, 251, 3, 11, 59, 8, 9)), std::string("11:59 AM"));

  // One channel carries the lift; a color already at the floor stays as it is.
  CHECK_EQ(clock_color(true, 0x00007F), (uint32_t) 0x0000C0);
  CHECK_EQ(clock_color(true, 0xC0C0C0), (uint32_t) 0xC0C0C0);
  CHECK_EQ(clock_color(true, 0x010101), (uint32_t) 0xC0C0C0);
}

static void test_panel_text() {
  using esphome::gameday::panel_text;
  // An iPhone hotspot name
  CHECK_EQ(panel_text("Sam\xE2\x80\x99s iPhone"), std::string("Sam's iPhone"));
  CHECK_EQ(panel_text("\xE2\x80\x98Hi\xE2\x80\x99"), std::string("'Hi'"));
  CHECK_EQ(panel_text("Hawai\xCA\xBBi"), std::string("Hawai'i"));
  CHECK_EQ(panel_text("\xE2\x80\x9CHail Mary\xE2\x80\x9D"), std::string("\"Hail Mary\""));
  CHECK_EQ(panel_text("10\xE2\x80\x93" "7 \xE2\x80\x94 run \xE2\x88\x92" "3"), std::string("10-7 - run -3"));
  CHECK_EQ(panel_text("Wait\xE2\x80\xA6"), std::string("Wait..."));
  CHECK_EQ(panel_text("a\xE2\x80\xA2" "b"), std::string("a\xC2\xB7" "b"));
  // Latin-1 letters are in the fonts and pass through untouched.
  CHECK_EQ(panel_text("S\xC3\xA3o Paulo"), std::string("S\xC3\xA3o Paulo"));
  CHECK_EQ(panel_text("Atl\xC3\xA9tico"), std::string("Atl\xC3\xA9tico"));
  CHECK_EQ(panel_text("San Jos\xC3\xA9 State, M\xC3\xBCnchen, Ni\xC3\xB1o, \xC3\x85" "s"),
           std::string("San Jos\xC3\xA9 State, M\xC3\xBCnchen, Ni\xC3\xB1o, \xC3\x85" "s"));
  // Letters past Latin-1 lose the accent.
  CHECK_EQ(panel_text("Don\xC4\x8D" "i\xC4\x87"), std::string("Doncic"));
  CHECK_EQ(panel_text("Be\xC5\x9Fikta\xC5\x9F"), std::string("Besiktas"));
  CHECK_EQ(panel_text("\xC5\x81\xC3\xB3" "d\xC5\xBA"), std::string("L\xC3\xB3" "dz"));
  CHECK_EQ(panel_text("Kad\xC4\xB1k\xC3\xB6y"), std::string("Kadik\xC3\xB6y"));
  CHECK_EQ(panel_text("\xC5\x92uvre"), std::string("OEuvre"));
  CHECK_EQ(panel_text("Ia\xC8\x99i"), std::string("Iasi"));
  // An accent sent as its own character is dropped, keeping the letter.
  CHECK_EQ(panel_text("Pe\xCC\x81rez"), std::string("Perez"));
  // Spaces and invisible characters
  CHECK_EQ(panel_text("a\xC2\xA0" "b\xE2\x80\x89" "c"), std::string("a b c"));
  CHECK_EQ(panel_text("a\xE2\x80\x8B" "b\xC2\xAD" "c\xEF\xBB\xBF"), std::string("abc"));
  CHECK_EQ(panel_text("line\none\ttwo"), std::string("line one two"));
  // A splash keeps its line break ("PHI" over "FUMBLE RECOVERY"), nothing else.
  CHECK_EQ(panel_text("PHI\nFUMBLE RECOVERY", true), std::string("PHI\nFUMBLE RECOVERY"));
  CHECK_EQ(panel_text("a\r\nb\tc", true), std::string("a \nb c"));
  CHECK_EQ(panel_text("Mu\xC3\xB1oz\nGOAL", true), std::string("Mu\xC3\xB1oz\nGOAL"));
  // Anything else is a '?', never a box.
  CHECK_EQ(panel_text("Home \xF0\x9F\x8F\x88"), std::string("Home ?"));
  CHECK_EQ(panel_text("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), std::string("?"));
  CHECK_EQ(panel_text("\xD0\x9C\xD0\xB8\xD1\x80"), std::string("???"));
  // Broken UTF-8 is one '?' per bad byte and doesn't swallow what follows.
  CHECK_EQ(panel_text("a\x80" "b"), std::string("a?b"));
  CHECK_EQ(panel_text("a\xC3"), std::string("a?"));
  CHECK_EQ(panel_text("a\xE2\x80" "b"), std::string("a??b"));
  CHECK_EQ(panel_text("\xC0\xAF"), std::string("??"));
  CHECK_EQ(panel_text("\xED\xA0\x80"), std::string("???"));
  CHECK_EQ(panel_text(""), std::string(""));
  CHECK_EQ(panel_text("TOUCHDOWN!"), std::string("TOUCHDOWN!"));
  // Every code point up to U+FFFF, and some past it, comes out drawable.
  for (uint32_t cp = 0; cp < 0x20000; cp++) {
    if (cp >= 0xD800 && cp <= 0xDFFF)
      continue;
    std::string s;
    if (cp < 0x80) {
      s += (char) cp;
    } else if (cp < 0x800) {
      s += (char) (0xC0 | (cp >> 6));
      s += (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += (char) (0xE0 | (cp >> 12));
      s += (char) (0x80 | ((cp >> 6) & 0x3F));
      s += (char) (0x80 | (cp & 0x3F));
    } else {
      s += (char) (0xF0 | (cp >> 18));
      s += (char) (0x80 | ((cp >> 12) & 0x3F));
      s += (char) (0x80 | ((cp >> 6) & 0x3F));
      s += (char) (0x80 | (cp & 0x3F));
    }
    const std::string out = panel_text(s);
    if (!panel_drawable(out)) {
      CHECK(panel_drawable(out));
      printf("  U+%04X\n", (unsigned) cp);
      break;
    }
  }
}

// A game with no kickoff time yet (fixtures recorded 2026-10-14): ESPN lists
// it at midnight Eastern with timeValid false. Every parse carries the flag,
// so the panel says "TBD" and never schedules off the placeholder.
static struct tm utc_tm(int64_t epoch) {
  time_t t = (time_t) epoch;
  return *gmtime(&t);
}

static void test_tbd_kickoff() {
  const uint32_t kIowa = 2294, kMichigan = 130;
  const int64_t placeholder = parse_iso8601_z("2026-10-24T04:00Z");  // midnight Eastern, Sat 10/24

  // The team endpoint: Iowa's next game has no time yet.
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_iowa_tbd.json"), s));
  CHECK_EQ(s.event_id, std::string("401858497"));
  CHECK_EQ(s.kickoff_epoch, placeholder);
  CHECK(s.time_tbd);
  CHECK_EQ(s.group, (uint32_t) 5);
  Schedule dal;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), dal));
  CHECK(!dal.time_tbd);

  // The game's scoreboard is the Eastern day of the game, not the day before
  // (20261023 has no Big Ten games at all).
  CHECK(scoreboard_url(League::NCAA, s.group, s.kickoff_epoch, s.time_tbd).find("dates=20261024") !=
        std::string::npos);
  CHECK(scoreboard_url(League::NCAA, s.group, s.kickoff_epoch).find("dates=20261023") != std::string::npos);
  // Standard time: the placeholder is 05:00Z, still the same day.
  CHECK(scoreboard_url(League::NCAA, 5, parse_iso8601_z("2026-11-07T05:00Z"), true).find("dates=20261107") !=
        std::string::npos);

  // The event in the day's scoreboard, and on its own.
  std::string sb = slurp("fixtures/scoreboard_ncaa_tbd.json");
  GameSnapshot g;
  CHECK(parse_scoreboard_str(sb, "401858497", kIowa, g));
  CHECK(g.valid);
  CHECK(g.state == GameState::PRE);
  CHECK(g.time_tbd);
  CHECK_EQ(g.kickoff_epoch, placeholder);
  CHECK_EQ(g.short_detail, std::string("TBD"));
  CHECK_EQ(g.opp_abbr, std::string("MINN"));
  GameSnapshot mich;
  CHECK(parse_scoreboard_str(sb, "401858496", kMichigan, mich));
  CHECK(!mich.time_tbd);
  CHECK_EQ(mich.kickoff_epoch, parse_iso8601_z("2026-10-24T16:00Z"));
  GameSnapshot ev;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_tbd.json"), Sport::FOOTBALL, kIowa, ev));
  CHECK(ev.valid);
  CHECK(ev.time_tbd);
  CHECK_EQ(ev.event_id, std::string("401858497"));

  // The day's scoreboard read for the team's game, and the live scan's wait.
  Schedule day;
  CHECK(parse_team_game_str(sb, kIowa, day));
  CHECK_EQ(day.event_id, std::string("401858497"));
  CHECK(day.time_tbd);
  Schedule mday;
  CHECK(parse_team_game_str(sb, kMichigan, mday));
  CHECK(!mday.time_tbd);
  std::vector<LiveGame> live;
  int64_t next = -1;
  CHECK(parse_live_games(sb, live, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-24T16:00Z"));

  // The season schedule: five TBD games, then one with a time.
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_ncaa_iowa_tbd.json"), kIowa, 8, up));
  CHECK_EQ(up.size(), (size_t) 6);
  if (up.size() == 6) {
    for (size_t i = 0; i < 5; i++)
      CHECK(up[i].time_tbd);
    CHECK_EQ(up[0].event_id, std::string("401858497"));
    CHECK_EQ(up[0].opp_abbr, std::string("MINN"));
    CHECK(!up[5].time_tbd);
    CHECK_EQ(up[5].kickoff_epoch, parse_iso8601_z("2026-11-27T17:00Z"));
  }

  // The label: the game's day and "TBD". The panel takes the day from noon
  // Eastern; in US Central (UTC-5 in October) the placeholder itself is
  // Friday 11 PM, which is what the ticker used to say.
  const int64_t central = -5 * 3600;
  struct tm wed = utc_tm(parse_iso8601_z("2026-10-14T20:00Z") + central);
  struct tm tue = utc_tm(parse_iso8601_z("2026-10-20T15:00Z") + central);
  struct tm fri = utc_tm(parse_iso8601_z("2026-10-23T15:00Z") + central);
  struct tm kick = utc_tm(tbd_day_epoch(placeholder) + central);
  CHECK_EQ(kickoff_label(utc_tm(placeholder + central), tue), std::string("Fri 11:00 PM"));
  CHECK_EQ(kickoff_label(kick, wed, true), std::string("Oct 24 TBD"));
  CHECK_EQ(kickoff_label(kick, tue, true), std::string("Sat TBD"));
  CHECK_EQ(kickoff_label(kick, fri, true), std::string("Tomorrow TBD"));
  TickerOptions o;
  std::string text = status_text(g, o, kickoff_label(kick, tue, true));
  CHECK(text.find("Sat TBD") != std::string::npos);
  CHECK(text.find(" PM") == std::string::npos);
  CHECK(text.find(" AM") == std::string::npos);
  CHECK_EQ(status_text(g, o, ""), status_text(g, o, "TBD"));  // ESPN's own text when there is no label

  // Favorites: no lock-on 15 minutes before the placeholder, and the today
  // filter goes by the game's day, not Friday night.
  FavRules r;
  r.lockon_s = 15 * 60;
  FavGame f;
  f.valid = true;
  f.state = GameState::PRE;
  f.kickoff_epoch = placeholder;
  CHECK(fav_locked(f, placeholder - 10 * 60, r));
  f.time_tbd = true;
  CHECK(!fav_locked(f, placeholder - 10 * 60, r));
  CHECK(!fav_locked(f, placeholder + 3600, r));
  f.state = GameState::IN;
  CHECK(fav_locked(f, placeholder + 16 * 3600, r));
  f.state = GameState::PRE;
  FavRules friday = r;  // Friday 10/23 in US Central
  friday.day_start = parse_iso8601_z("2026-10-23T05:00Z");
  friday.day_end = friday.day_start + 86400;
  FavRules saturday = r;
  saturday.day_start = friday.day_end;
  saturday.day_end = saturday.day_start + 86400;
  int64_t fri_night = parse_iso8601_z("2026-10-24T02:00Z");
  CHECK(!fav_idle_ok(f, fri_night, friday, true));
  CHECK(fav_idle_ok(f, fri_night, saturday, true));
  f.time_tbd = false;
  CHECK(fav_idle_ok(f, fri_night, friday, true));  // the old reading of the placeholder

  // The boot cache has no room for the flag: a TBD game saves a blank record,
  // so the next boot reads the team endpoint.
  TeamCache c;
  CHECK(team_cache_save(s, League::NCAA, kIowa, placeholder - 5 * 86400, c));
  Schedule back;
  CHECK(!team_cache_load(c, League::NCAA, kIowa, placeholder - 5 * 86400 + 60, back));
}

// Each ESP-IDF disconnect reason wifi_cause() names, and the keys the device
// page reads (WIFI_CAUSES in app.js).
static void test_wifi_causes() {
  const int signal[] = {200, 201, 205, 209, 212, 34, 39};
  const int router[] = {2, 3, 4, 5, 6, 7, 47, 206};
  const int password[] = {14, 15, 16, 23, 202, 204};
  for (int r : signal)
    CHECK(wifi_cause(r) == WifiCause::SIGNAL);
  for (int r : router)
    CHECK(wifi_cause(r) == WifiCause::ROUTER);
  for (int r : password)
    CHECK(wifi_cause(r) == WifiCause::PASSWORD);
  CHECK(wifi_cause(12) == WifiCause::ROAM);
  CHECK(wifi_cause(207) == WifiCause::ROAM);
  CHECK(wifi_cause(8) == WifiCause::PANEL);
  // A link lost with no disconnect event comes in as 0.
  CHECK(wifi_cause(0) == WifiCause::OTHER);
  CHECK(wifi_cause(-1) == WifiCause::OTHER);
  CHECK(wifi_cause(203) == WifiCause::OTHER);  // ASSOC_FAIL
  const char *keys[] = {"signal", "router", "password", "roam", "other", "panel"};
  for (uint8_t c = 0; c <= (uint8_t) WifiCause::PANEL; c++)
    CHECK_EQ(std::string(wifi_cause_key((WifiCause) c)), std::string(keys[c]));
  CHECK_EQ((int) kWifiCauses, (int) WifiCause::PANEL);  // the counted ones come before PANEL
}

static void test_wifi_history() {
  CHECK(wifi_cause(200) == WifiCause::SIGNAL);  // beacon timeout
  CHECK(wifi_cause(3) == WifiCause::ROUTER);    // deauth from the router
  CHECK(wifi_cause(206) == WifiCause::ROUTER);  // the router restarted
  CHECK(wifi_cause(15) == WifiCause::PASSWORD);
  CHECK(wifi_cause(207) == WifiCause::ROAM);
  CHECK(wifi_cause(8) == WifiCause::PANEL);
  CHECK(wifi_cause(1) == WifiCause::OTHER);
  CHECK_EQ(std::string(wifi_cause_key(WifiCause::PASSWORD)), std::string("password"));

  static int8_t rssi[kWifiMinutes];
  static WifiDrop drops[kWifiDrops];

  // No buffers (no PSRAM): no history, only the total.
  WifiHistory none;
  none.sample(0, true, -60);
  none.up(0);
  none.down(10, WifiCause::SIGNAL);
  none.up(15);
  CHECK_EQ(none.json(20, true, -60), std::string("{\"rssi\":-60,\"step\":300,\"hist\":[],\"weak\":false,"
                                                 "\"drops\":1,\"day\":0,\"causes\":{}}"));

  // A panel that drops all the time still answers whole JSON.
  {
    static int8_t r2[kWifiMinutes];
    static WifiDrop d2[kWifiDrops];
    WifiHistory flap;
    flap.attach(r2, d2);
    for (uint32_t i = 0; i < 1000000; i++) {
      flap.up(i * 2);
      flap.down(i * 2 + 1, WifiCause::OTHER);
    }
    std::string fj = flap.json(2000000, false, -127);
    CHECK(fj.find("\"weak\":false,\"drops\":1000000,\"day\":64,\"causes\":{\"other\":64},\"last\":\"other\",\"last_ago\":1}") !=
          std::string::npos);
    CHECK(fj.back() == '}');
  }

  // A minute keeps the mean of its readings; a minute off Wi-Fi keeps 0.
  WifiHistory h;
  h.attach(rssi, drops);
  h.up(0);
  h.sample(0, true, -60);
  h.sample(10, true, -62);
  h.sample(20, true, -65);
  CHECK_EQ(h.minutes(), 0u);
  h.sample(60, false, -127);
  CHECK_EQ(h.minutes(), 1u);
  CHECK_EQ((int) rssi[0], -62);
  h.sample(120, true, -70);
  CHECK_EQ((int) rssi[1], 0);
  CHECK_EQ(h.json(130, false, -127), std::string("{\"step\":300,\"hist\":[-62],\"weak\":false,"
                                                  "\"drops\":0,\"day\":0,\"causes\":{}}"));

  // A loop that stalled past several minutes fills them in as off Wi-Fi.
  h.sample(400, true, -70);
  CHECK_EQ(h.minutes(), 6u);
  CHECK_EQ((int) rssi[2], -70);
  CHECK_EQ((int) rssi[3], 0);
  // Points end at the newest minute: [-62] then [off, -70, off, off, off]
  CHECK(h.json(400, true, -70).find("\"hist\":[-62,-70]") != std::string::npos);

  // Drops: the cause, how long it lasted, and the panel leaving on purpose
  // or a roam not counted as trouble.
  h.down(1000, WifiCause::SIGNAL);
  std::string j = h.json(1010, false, -127);
  CHECK(j.find("\"drops\":1,\"day\":1,\"causes\":{\"signal\":1},\"last\":\"signal\",\"last_ago\":10}") !=
        std::string::npos);
  h.up(1042);
  CHECK(h.json(1100, true, -60).find("\"last_ago\":100,\"last_down\":42}") != std::string::npos);
  h.up(1050);  // already up
  CHECK(h.json(1100, true, -60).find("\"last_down\":42}") != std::string::npos);
  h.down(1200, WifiCause::PANEL);
  h.up(1210);
  h.down(1300, WifiCause::ROAM);
  h.up(1301);
  h.down(1400, WifiCause::ROUTER);
  h.down(1401, WifiCause::SIGNAL);  // already down: the first cause stands
  h.up(1460);
  CHECK_EQ(h.total(), 2u);
  j = h.json(1500, true, -60);
  CHECK(j.find("\"drops\":2,\"day\":2,\"causes\":{\"signal\":1,\"router\":1,\"roam\":1},"
               "\"last\":\"router\",\"last_ago\":100,\"last_down\":60}") != std::string::npos);
  // A day later the drops are history: still in the total, out of the day.
  j = h.json(1400 + 24 * 3600, true, -60);
  CHECK(j.find("\"drops\":2,\"day\":0,\"causes\":{},\"last\":\"router\"") != std::string::npos);

  // The weak-signal hint: a median under -75 dBm over the last half hour,
  // from at least ten minutes on Wi-Fi.
  WifiHistory w;
  w.attach(rssi, drops);
  uint32_t t = 0;
  // n minutes of one reading each, then a reading-less sample that closes
  // the last of them
  auto run = [&](int n, bool on, int8_t v) {
    for (int m = 0; m < n; m++, t += 60)
      w.sample(t, on, v);
    w.sample(t, false, -127);
  };
  run(9, true, -80);
  CHECK(!w.weak());  // nine minutes so far
  run(1, true, -80);
  CHECK(w.weak());
  run(9, true, -60);
  CHECK(w.weak());  // 10 weak of 19
  run(2, true, -60);
  CHECK(!w.weak());  // 10 weak of 21
  run(30, true, -60);
  CHECK(!w.weak());
  run(25, false, -127);
  run(5, true, -80);
  CHECK(!w.weak());  // five readings in the last half hour: too few to say
  run(30, false, -127);
  CHECK(!w.weak());  // off Wi-Fi: no readings, no hint
  CHECK(w.json(t, false, -127).find("-80,null,null,null,null,null,null]") != std::string::npos);

  // The ring holds a day: after 25 hours the JSON has 288 points.
  WifiHistory d;
  d.attach(rssi, drops);
  for (uint32_t m = 0; m <= 25 * 60; m++)
    d.sample(m * 60, true, (int8_t) (m < 24 * 60 ? -50 : -77));
  CHECK_EQ(d.minutes(), 25u * 60);
  j = d.json(25 * 3600, true, -77);
  size_t points = 1;
  for (char c : j.substr(j.find("\"hist\":["), j.find(']') - j.find("\"hist\":[")))
    if (c == ',')
      points++;
  CHECK_EQ(points, (size_t) (kWifiMinutes / kWifiStep));
  CHECK(j.find(",-77]") != std::string::npos);
  CHECK(d.weak());
}

// ---- golf ------------------------------------------------------------------------
// A tour followed like a team, from its scoreboard header (fixtures recorded
// 2026-11-07 03:18Z, see fixtures/README.md).

// By value: the parse templates read from an lvalue.
static int golf_str(std::string json, GameSnapshot &s) { return parse_golf(json, s); }
static int golf_cal_str(std::string json, int64_t now, const std::string &after, GolfNext &n) {
  return parse_golf_calendar(json, now, after, n);
}

static GameSnapshot golf_doc(const char *file) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK_EQ(golf_str(json, s), 1);
  return s;
}

static bool contains(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

static void golf_tee_labels(GameSnapshot &s) {
  for (auto &r : s.golf.rows)
    r.set_tee(r.tee_epoch > 0 ? golf_tee_label(utc_tm(r.tee_epoch)) : "");
}

static std::string golf_dates_of(int64_t start, int64_t end) {
  return golf_dates(utc_tm(tbd_day_epoch(start)), utc_tm(tbd_day_epoch(end)));
}

static void test_golf_urls() {
  CHECK(is_golf(League::PGA) && is_golf(League::LPGA) && !is_golf(League::NFL));
  CHECK(league_sport(League::LPGA) == Sport::GOLF);
  CHECK_EQ(std::string(league_key(League::PGA)), std::string("pga"));
  CHECK(league_by_key("lpga") != nullptr && league_by_key("lpga")->league == League::LPGA);
  CHECK_EQ(header_url(League::PGA),
           std::string("https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league=pga"));
  CHECK_EQ(header_url(League::LPGA),
           std::string("https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league=lpga"));
  // Sunday evening: the coming Tuesday. On a Tuesday, that day.
  const std::string cal = "https://site.api.espn.com/apis/site/v2/sports/golf/pga/scoreboard?dates=";
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-08T23:00Z"), false), cal + "20261110");
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-10T15:00Z"), false), cal + "20261110");
  // 10 PM Eastern on Sunday and on Tuesday are still those days
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-09T03:00Z"), false), cal + "20261110");
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-11T03:00Z"), false), cal + "20261110");
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-11T15:00Z"), false), cal + "20261117");
  // Across the new year
  CHECK_EQ(golf_calendar_url(League::LPGA, parse_iso8601_z("2026-12-31T15:00Z"), false),
           std::string("https://site.api.espn.com/apis/site/v2/sports/golf/lpga/scoreboard?dates=20270105"));
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2026-12-14T15:00Z"), true), cal + "2027");
  CHECK_EQ(golf_calendar_url(League::PGA, parse_iso8601_z("2027-01-01T03:00Z"), true), cal + "2027");  // Dec 31 Eastern
  // Never this year's whole list (dates=2026 is 29 MB): a Tuesday in the
  // coming week, or next year.
  const int64_t day = 86400;
  for (int64_t t = parse_iso8601_z("2026-01-01T00:00Z"); t < parse_iso8601_z("2027-02-01T00:00Z"); t += 7 * 3600) {
    for (bool next : {false, true}) {
      std::string url = golf_calendar_url(League::PGA, t, next);
      std::string dates = url.substr(url.find("dates=") + 6);
      struct tm east = utc_tm(t - 5 * 3600);
      if (next) {
        CHECK_EQ(dates, std::to_string(east.tm_year + 1901));
        continue;
      }
      CHECK_EQ(dates.size(), (size_t) 8);
      int64_t tue = parse_iso8601_z(dates.substr(0, 4) + "-" + dates.substr(4, 2) + "-" + dates.substr(6, 2) + "T12:00Z");
      CHECK_EQ(utc_tm(tue).tm_wday, 2);
      int64_t ahead = tue - (t - 5 * 3600);
      CHECK(ahead > -day && ahead < 7 * day);
    }
  }
  set_site_root("https://bench.invalid/sports/");
  CHECK_EQ(header_url(League::PGA),
           std::string("https://bench.invalid/apis/v2/scoreboard/header?sport=golf&league=pga"));
  CHECK(golf_calendar_url(League::PGA, parse_iso8601_z("2026-11-08T23:00Z"), false).rfind("https://bench.invalid/", 0) == 0);
  set_site_root("https://site.api.espn.com/apis/site/v2/sports/");
  CHECK_EQ(team_logo_url(League::PGA, 1106, "PGA"), std::string(""));
  // The tours are rows in the team table: Home Assistant's options and /gameday/set
  bool pga = false, lpga = false;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::PGA && kTeams[i].espn_id == 1106) {
      pga = true;
      CHECK_EQ(team_option(kTeams[i]), std::string("Golf: PGA Tour"));
    }
    if (kTeams[i].league == League::LPGA && kTeams[i].espn_id == 1107) {
      lpga = true;
      CHECK_EQ(team_option(kTeams[i]), std::string("Golf: LPGA Tour"));
      // appended: every earlier option keeps its place, and the racing series follow
      CHECK(i + 1 < kTeamCount && is_racing(kTeams[i + 1].league));
    }
  }
  CHECK(pga && lpga);
}

// LPGA, TOTO Japan Classic, round 3 in progress
static void test_golf_live() {
  GameSnapshot s = golf_doc("fixtures/golf_header_lpga_live.json");
  CHECK(s.valid);
  CHECK(s.sport == Sport::GOLF);
  CHECK(s.golf.phase == GolfPhase::LIVE);
  CHECK(s.state == GameState::IN);
  CHECK(!s.completed);
  CHECK_EQ(s.period, 3);
  CHECK_EQ(s.event_id, std::string("401835175"));
  CHECK_EQ(s.game_name, std::string("TOTO Japan Classic"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-05T05:00:00Z"));
  CHECK_EQ(s.golf.end_epoch, parse_iso8601_z("2026-11-08T05:00:00Z"));
  CHECK_EQ(s.venue, std::string("Taiheiyo Club Minori Course - Omitama, Japan"));
  CHECK_EQ(s.tv, std::string("Golf Chnl"));
  CHECK_EQ(s.golf.purse, std::string("$2,100,000"));
  CHECK_EQ(s.golf.defending, std::string("Nasa Hataoka"));
  CHECK_EQ(s.golf.rows.size(), kGolfRows);
  const GolfRow &r0 = s.golf.rows[0];
  CHECK_EQ((int) r0.place, 1);
  CHECK(r0.tie);
  CHECK_EQ(r0.name, std::string("Iwai"));
  CHECK_EQ(r0.score, std::string("-12"));
  CHECK_EQ(r0.to_par, -12);
  CHECK(r0.par_known);
  CHECK_EQ(r0.state, 'i');
  CHECK_EQ((int) r0.thru, 7);
  CHECK(!r0.winner);
  CHECK_EQ((int) s.golf.rows[2].place, 3);
  CHECK(!s.golf.rows[2].tie);
  CHECK_EQ(s.golf.rows[2].name, std::string("Hara"));
  BoardLines l = golf_lines(s, false);
  CHECK_EQ(l.status, std::string("Round 3"));
  CHECK_EQ(l.status_color, (uint32_t) 0x40FF40);
  CHECK(l.value_narrow);  // golf's score column on one panel
  CHECK(l.title.empty());
  CHECK_EQ(l.pos.size(), kGolfBoardRows);
  CHECK_EQ(l.pos[0], std::string("T1"));
  CHECK_EQ(l.pos[2], std::string("3"));
  CHECK_EQ(l.name[4], std::string("Takahas"));
  CHECK_EQ(l.value[0], std::string("-12"));
  CHECK_EQ(l.value_color[0], 0xFF3030);
  CHECK_EQ(l.name_color[0], 0xFFFFFF);
  CHECK_EQ(l.detail[0], std::string(""));
  BoardLines w = golf_lines(s, true);
  CHECK_EQ(w.title, std::string("TOTO Japan"));  // 17 beside "Round 3"
  CHECK_EQ(w.name[4], std::string("Takahashi"));
  CHECK_EQ(w.detail[0], std::string("7"));
  CHECK_EQ(w.detail[1], std::string("4"));
  TickerOptions o;
  std::string t = status_text(s, o, "");
  CHECK_EQ(t.rfind("TOTO Japan Classic | Round 3 | T1 Iwai -12 thru 7, T1 Hataoka -12 thru 4, 3 Hara -11 thru 5", 0),
           (size_t) 0);
  CHECK(contains(t, "| Golf Chnl"));
  o.odds = false;
  CHECK(!contains(status_text(s, o, ""), "Golf Chnl"));
  CHECK_EQ(ha_status_text(s, ""), std::string("TOTO Japan Classic | Round 3 | Leader: Iwai -12"));
  CHECK_EQ(clock_text(s), std::string(""));
  // A playoff on the course
  GameSnapshot p = s;
  p.golf.playoff = true;
  CHECK_EQ(golf_round_text(p), std::string("Playoff"));
  CHECK_EQ(golf_round_long(p), std::string("Playoff"));
  // Live with no round number
  p.golf.playoff = false;
  p.period = 0;
  CHECK_EQ(golf_round_text(p), std::string("Live"));
}

// PGA Tour, World Wide Technology Championship, round 2 suspended
static void test_golf_suspended() {
  GameSnapshot s = golf_doc("fixtures/golf_header_pga_suspended.json");
  CHECK(s.golf.phase == GolfPhase::BREAK);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(s.period, 2);
  CHECK(s.golf.status == GolfStatus::SUSPENDED);
  CHECK_EQ(golf_round_text(s), std::string("R2 suspended"));
  CHECK_EQ(golf_round_color(s), (uint32_t) 0xFFA000);
  CHECK_EQ(golf_round_long(s), std::string("Round 2 suspended"));
  for (const auto &r : s.golf.rows) {
    CHECK_EQ(r.state, 'f');
    CHECK_EQ(golf_thru_text(r, s.golf.phase), std::string("F"));
  }
  const char *pos[] = {"1", "2", "3", "T4", "T4", "T6"};
  BoardLines l = golf_lines(s, false), w = golf_lines(s, true);
  for (size_t i = 0; i < kGolfBoardRows; i++)
    CHECK_EQ(l.pos[i], std::string(pos[i]));
  CHECK_EQ(l.name[5], std::string("Yellama"));
  CHECK_EQ(w.name[5], std::string("Yellamaraju"));
  CHECK_EQ(w.title, std::string("World Wide"));
  CHECK_EQ(w.status, std::string("R2 suspended"));
  CHECK_EQ(w.detail[3], std::string("F"));
  // The whole name, cut only in the board's columns
  CHECK_EQ(s.golf.rows[6].name, std::string("Neergaard-Petersen"));
  CHECK_EQ(w.name.size(), (size_t) 6);
  std::string t = status_text(s, TickerOptions{}, "");
  CHECK(contains(t, "T6 Yellamaraju -10, T6 Neergaard-Petersen -10"));
  CHECK(contains(t, "World Wide Technology Championship | Round 2 suspended | 1 James -15, 2 Malnati -14,"));
  CHECK(contains(t, "T4 Jaeger -11, T4 McCarthy -11"));
  CHECK(!contains(t, " F,"));  // between rounds the rows have no thru
  CHECK(!contains(t, "Leaders tee off"));  // nobody has a tee time to come
  CHECK_EQ(ha_status_text(s, ""),
           std::string("World Wide Technology Championship | Round 2 suspended | Leader: James -15"));
  // Delayed reads the same way
  GameSnapshot d = s;
  d.golf.status = GolfStatus::DELAYED;
  CHECK_EQ(golf_round_text(d), std::string("R2 delayed"));
  CHECK_EQ(golf_round_long(d), std::string("Round 2 delayed"));
  CHECK_EQ(golf_round_color(d), (uint32_t) 0xFFA000);
  d.period = 12;  // no room for two digits: the word alone
  CHECK_EQ(golf_round_text(d), std::string("Delayed"));
  d.golf.status = GolfStatus::SUSPENDED;
  CHECK_EQ(golf_round_text(d), std::string("Suspended"));
  d.golf.status = GolfStatus::OTHER;  // a status name the panel doesn't know
  d.short_detail = "Round 2 - Weather delay";
  CHECK_EQ(golf_round_text(d), std::string("Round 2"));  // whole words
  CHECK_EQ(golf_round_long(d), std::string("Round 2 - Weather delay"));
  // ESPN's words, folded to the board's characters
  std::string doc = slurp("fixtures/golf_header_pga_suspended.json");
  const std::string from = "\"shortDetail\":\"";
  size_t at = doc.find(from);
  CHECK(at != std::string::npos);
  doc.replace(at + from.size(), doc.find('"', at + from.size()) - at - from.size(), "Round 2 \u2013 Fr\u00e9d\u00e9ric");
  doc.replace(doc.find("STATUS_SUSPENDED"), 16, "STATUS_UNKNOWN");  // a name the panel doesn't know, state post
  GameSnapshot u;
  CHECK_EQ(golf_str(doc, u), 1);
  CHECK(u.golf.phase == GolfPhase::BREAK);
  CHECK_EQ(u.short_detail, std::string("Round 2 - Frederic"));
  CHECK_EQ(golf_round_text(u), std::string("Round 2"));
}

// DP World Tour, Abu Dhabi, round 2 play complete with round 3's tee times
static void test_golf_after_round() {
  GameSnapshot s = golf_doc("fixtures/golf_header_eur_after_round.json");
  CHECK(s.golf.phase == GolfPhase::BREAK);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(golf_round_text(s), std::string("After R2"));
  CHECK_EQ(golf_round_long(s), std::string("After round 2"));
  CHECK_EQ(golf_round_color(s), (uint32_t) 0xFFFFFF);
  CHECK_EQ(s.golf.rows[0].state, 'p');
  CHECK_EQ((int) s.golf.rows[0].thru, 0);
  CHECK_EQ(s.golf.rows[0].tee_epoch, parse_iso8601_z("2026-11-07T07:50Z"));
  CHECK_EQ(golf_tee_label(utc_tm(s.golf.rows[0].tee_epoch)), std::string("7:50a"));
  // Before the main loop fills the tee times in: a dash
  CHECK_EQ(golf_thru_text(s.golf.rows[0], s.golf.phase), std::string("-"));
  golf_tee_labels(s);
  CHECK_EQ(golf_thru_text(s.golf.rows[0], s.golf.phase), std::string("7:50a"));
  CHECK_EQ(golf_lines(s, true).detail[3], std::string("7:39a"));
  CHECK_EQ(golf_lines(s, true).title, std::string("Abu Dhabi Aldar"));
  std::string t = status_text(s, TickerOptions{}, "");
  CHECK(contains(t, "Abu Dhabi Aldar Championship | After round 2 | T1 Hatton -12, T1 Rahm -12, T1 Laporta -12"));
  CHECK(contains(t, "| Leaders tee off 7:50a"));
  CHECK_EQ(ha_status_text(s, ""), std::string("Abu Dhabi Aldar Championship | After round 2 | Leader: Hatton -12"));
  // Tee time labels
  CHECK_EQ(golf_tee_label(mk(2026, 0, 0, 20, 27)), std::string("8:27p"));
  CHECK_EQ(golf_tee_label(mk(2026, 0, 0, 11, 40)), std::string("11:40a"));
  CHECK_EQ(golf_tee_label(mk(2026, 0, 0, 12, 5)), std::string("12:05p"));
  CHECK_EQ(golf_tee_label(mk(2026, 0, 0, 0, 5)), std::string("12:05a"));
  CHECK_EQ(golf_tee_label(mk(2026, 0, 0, -1, 5)), std::string(""));
}

// Korn Ferry Tour Championship, final, ESPN's winner flag false
static void test_golf_final() {
  GameSnapshot s = golf_doc("fixtures/golf_header_kft_final.json");
  CHECK(s.golf.phase == GolfPhase::FINAL);
  CHECK(s.state == GameState::POST);
  CHECK(s.completed);
  CHECK(s.golf.rows[0].winner);
  CHECK_EQ(s.golf.rows[0].name, std::string("Hitt"));
  CHECK(!s.golf.rows[1].winner);
  CHECK_EQ(golf_round_text(s), std::string("Final"));
  CHECK_EQ(golf_round_color(s), (uint32_t) 0xFFD000);
  BoardLines l = golf_lines(s, false), w = golf_lines(s, true);
  CHECK_EQ(l.name_color[0], 0xFFD000);
  CHECK_EQ(l.name_color[1], 0xFFFFFF);
  CHECK_EQ(l.name[2], std::string("Dochert"));
  CHECK_EQ(l.pos[3], std::string("T4"));
  CHECK_EQ(w.title, std::string("Korn Ferry Tour"));
  for (const auto &t : w.detail)
    CHECK_EQ(t, std::string(""));  // a final has no thru column
  std::string t = status_text(s, TickerOptions{}, "");
  CHECK_EQ(t, std::string("Korn Ferry Tour Championship | Final | Hitt wins at -20 | 2 Brown -18, 3 Docherty -17, "
                          "T4 Gabrelcik -16, T4 Westmoreland -16"));
  CHECK_EQ(ha_status_text(s, ""), std::string("Korn Ferry Tour Championship | Final | Hitt wins at -20"));
  GameSnapshot n = s;
  n.golf.next.valid = true;
  n.golf.next.event_id = "401850981";
  n.golf.next.name = "The RSM Classic";
  CHECK(contains(status_text(n, TickerOptions{}, "Nov 19-22"), "| Next: The RSM Classic, Nov 19-22"));
  n.golf.playoff = true;
  CHECK(contains(status_text(n, TickerOptions{}, ""), "Hitt wins in a playoff at -20"));
  // The splash: once, on the read that goes final
  GameSnapshot prev = s;
  prev.golf.phase = GolfPhase::LIVE;
  prev.state = GameState::IN;
  Splash sp = golf_splash(prev, s, false);
  CHECK_EQ(sp.text, std::string("HITT WINS!"));
  CHECK_EQ(sp.color, (uint32_t) 0xFFD000);
  CHECK_EQ(decide_splash(prev, s, true, false).text, std::string("HITT WINS!"));
  prev.golf.phase = GolfPhase::BREAK;
  CHECK_EQ(golf_splash(prev, s, true).text, std::string("HITT WINS!"));
  CHECK(golf_splash(s, s, false).text.empty());              // final to final
  CHECK(golf_splash(GameSnapshot{}, s, false).text.empty());  // a boot or a team change
  // A long name: each word fits a line of the splash, 10 characters on one
  // panel and 16 on two, and a hyphenated one breaks at its hyphen
  GameSnapshot longer = s;
  GolfRow::put(longer.golf.rows[0].name, sizeof(longer.golf.rows[0].name), "Yellamaraju");
  CHECK_EQ(golf_splash(prev, longer, false).text, std::string("YELLAMARAJ WINS!"));
  CHECK_EQ(golf_splash(prev, longer, true).text, std::string("YELLAMARAJU WINS!"));
  GolfRow::put(longer.golf.rows[0].name, sizeof(longer.golf.rows[0].name), "Neergaard-Petersen");
  CHECK_EQ(golf_splash(prev, longer, false).text, std::string("NEERGAARD-PETERSEN WINS!"));
  GolfRow::put(longer.golf.rows[0].name, sizeof(longer.golf.rows[0].name), "Abcdefghijkl-Mnopqrstuvwxyz");
  CHECK_EQ(longer.golf.rows[0].name, std::string("Abcdefghijkl-Mnopqrstuv"));  // 23 characters kept
  CHECK_EQ(golf_splash(prev, longer, false).text, std::string("ABCDEFGHI-MNOPQRSTUV WINS!"));
  CHECK_EQ(golf_splash(prev, longer, true).text, std::string("ABCDEFGHIJKL-MNOPQRSTUV WINS!"));
  prev.event_id = "1";
  CHECK(golf_splash(prev, s, false).text.empty());  // another event
  // Two players listed at place 1 in a final and no flag: no winner named
  GameSnapshot two = s;
  two.golf.rows[1].place = 1;
  two.golf.rows[0].tie = two.golf.rows[1].tie = true;
  two.golf.rows[0].winner = false;
  prev = s;
  prev.golf.phase = GolfPhase::LIVE;
  CHECK(golf_splash(prev, two, false).text.empty());
  CHECK_EQ(ha_status_text(two, ""), std::string("Korn Ferry Tour Championship | Final"));
}

// Charles Schwab Cup Championship (PGA Tour Champions), scheduled
static void test_golf_pre() {
  GameSnapshot s = golf_doc("fixtures/golf_header_champions_pre.json");
  CHECK(s.golf.phase == GolfPhase::PRE);
  CHECK(s.state == GameState::PRE);
  CHECK(s.golf.rows.empty());
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-12T05:00:00Z"));
  CHECK_EQ(s.golf.end_epoch, parse_iso8601_z("2026-11-15T05:00:00Z"));
  const std::string dates = golf_dates_of(s.kickoff_epoch, s.golf.end_epoch);
  CHECK_EQ(dates, std::string("Nov 12-15"));
  CHECK_EQ(golf_card_text(s, false, dates), std::string("Charles Schwab Cup Championship\nNov 12-15"));
  CHECK_EQ(golf_card_text(s, true, dates), std::string("Charles Schwab Cup Championship\nNov 12-15"));
  BoardLines l = golf_lines(s, false);
  CHECK(l.name.empty());
  CHECK_EQ(l.status, std::string("Upcoming"));
  CHECK_EQ(status_text(s, TickerOptions{}, dates),
           std::string("Charles Schwab Cup Championship | Nov 12-15 | Phoenix Country Club - Phoenix, AZ | Purse "
                       "$3,000,000 | Defending champion: Stewart Cink | Golf Chnl"));
  TickerOptions o;
  o.odds = false;
  CHECK_EQ(status_text(s, o, dates), std::string("Charles Schwab Cup Championship | Nov 12-15 | Phoenix Country Club - "
                                                 "Phoenix, AZ | Defending champion: Stewart Cink"));
  CHECK_EQ(ha_status_text(s, dates), std::string("Charles Schwab Cup Championship | Nov 12-15"));
  // Dates across a month, and one day
  CHECK_EQ(golf_dates_of(parse_iso8601_z("2026-10-29T04:00:00Z"), parse_iso8601_z("2026-11-01T04:00:00Z")),
           std::string("Oct 29-Nov 1"));
  CHECK_EQ(golf_dates_of(parse_iso8601_z("2026-08-30T04:00:00Z"), parse_iso8601_z("2026-08-30T04:00:00Z")),
           std::string("Aug 30"));
  CHECK_EQ(golf_dates_of(parse_iso8601_z("2026-12-31T05:00:00Z"), parse_iso8601_z("2027-01-03T05:00:00Z")),
           std::string("Dec 31-Jan 3"));
  CHECK_EQ(golf_dates(mk(1900, -1, -1, 0, 0, -1, 0), mk(1900, -1, -1, 0, 0, -1, 0)), std::string(""));
}

// LIV Golf Michigan, canceled
static void test_golf_canceled() {
  GameSnapshot s = golf_doc("fixtures/golf_header_liv_canceled.json");
  CHECK(s.golf.phase == GolfPhase::OFF);
  CHECK(s.state == GameState::POST);
  CHECK(!s.completed);
  CHECK_EQ(golf_round_text(s), std::string("Canceled"));
  CHECK_EQ(golf_round_color(s), (uint32_t) 0xFF3030);
  CHECK(golf_lines(s, false).name.empty());
  CHECK_EQ(golf_card_text(s, false, ""), std::string("LIV Golf Michigan - Stroke Play\nCanceled"));
  CHECK_EQ(s.venue, std::string("The Cardinal at Saint John's - Plymouth, MI"));  // a curly apostrophe
  CHECK_EQ(ha_status_text(s, ""), std::string("LIV Golf Michigan - Stroke Play | Canceled"));
  CHECK_EQ(status_text(s, TickerOptions{}, ""), std::string("LIV Golf Michigan - Stroke Play | Canceled"));
  GameSnapshot p = s;
  p.golf.status = GolfStatus::POSTPONED;
  CHECK_EQ(golf_round_text(p), std::string("Postponed"));
}

static void test_golf_documents() {
  GameSnapshot s;
  CHECK_EQ(golf_str(slurp("fixtures/golf_header_none.json"), s), 0);
  CHECK(!s.valid);
  CHECK_EQ(golf_str(std::string("{}"), s), -1);
  CHECK_EQ(golf_str(std::string("garbage"), s), -1);
  CHECK_EQ(golf_str(std::string("[1,2]"), s), -1);
  CHECK_EQ(golf_str(std::string(R"({"sports":[{"leagues":[{"events":[]}]}]})"), s), 0);
  CHECK_EQ(golf_str(std::string(R"({"sports":[{"leagues":[{"events":[{"name":"No id"}]}]}]})"), s), -1);
  CHECK_EQ(golf_str(std::string(R"({"sports":[{"leagues":[{"events":[7]}]}]})"), s), -1);
  // An opposite-field week: the primary event, wherever it is listed
  const std::string two = R"({"sports":[{"leagues":[{"events":[
      {"id":"1","name":"Opposite Open","primary":false},
      {"id":"2","name":"Main Championship","primary":true}]}]}]})";
  CHECK_EQ(golf_str(two, s), 1);
  CHECK_EQ(s.event_id, std::string("2"));
  CHECK_EQ(golf_str(std::string(R"({"sports":[{"leagues":[{"events":[{"id":"5"},{"id":"6"}]}]}]})"), s), 1);
  CHECK_EQ(s.event_id, std::string("5"));
  // Rows by ESPN's order however they arrive, at most kGolfRows of them, and
  // the scores that aren't a number
  std::string many = R"({"sports":[{"leagues":[{"events":[{"id":"9","fullStatus":{"type":{"name":"STATUS_IN_PROGRESS","state":"in"}},"competitors":[)";
  for (int i = 12; i >= 1; i--) {
    many += "{\"order\":" + std::to_string(i) + ",\"place\":" + std::to_string(i) + ",\"lastName\":\"P" +
            std::to_string(i) + "\",\"score\":\"" + (i == 3 ? "WD" : i == 4 ? "E" : i == 5 ? "+3" : "-" + std::to_string(20 - i)) + "\"}";
    if (i > 1)
      many += ",";
  }
  many += "]}]}]}]}";
  CHECK_EQ(golf_str(many, s), 1);
  CHECK_EQ(s.golf.rows.size(), kGolfRows);
  for (size_t i = 0; i < s.golf.rows.size(); i++)
    CHECK_EQ(s.golf.rows[i].name, "P" + std::to_string(i + 1));
  CHECK(!s.golf.rows[2].par_known);
  CHECK_EQ(golf_score_text(s.golf.rows[2]), std::string("WD"));
  CHECK_EQ(golf_score_color(s.golf.rows[2]), (uint32_t) 0xA0A0A0);
  CHECK_EQ(s.golf.rows[3].to_par, 0);
  CHECK_EQ(golf_score_text(s.golf.rows[3]), std::string("E"));
  CHECK_EQ(golf_score_color(s.golf.rows[3]), (uint32_t) 0xFFFFFF);
  CHECK_EQ(s.golf.rows[4].to_par, 3);
  CHECK_EQ(golf_score_text(s.golf.rows[4]), std::string("+3"));
  CHECK_EQ(golf_score_color(s.golf.rows[4]), (uint32_t) 0x40A0FF);
  CHECK_EQ(golf_pos_text(s.golf.rows[9]), std::string("10"));
  // Places past the board's two characters, and none at all
  GolfRow r;
  r.place = 12;
  r.tie = true;
  CHECK_EQ(golf_pos_text(r), std::string("12"));
  CHECK_EQ(golf_place_text(r), std::string("T12"));
  r.place = 0;
  CHECK_EQ(golf_pos_text(r), std::string("-"));
  r.place = 150;
  CHECK_EQ(golf_pos_text(r), std::string("-"));
  r.par_known = true;
  r.to_par = -150;
  CHECK_EQ(golf_score_text(r), std::string("-99"));
  // A score as a JSON number, and one too long for the board
  CHECK_EQ(golf_str(std::string(R"({"sports":[{"leagues":[{"events":[{"id":"9","competitors":[{"order":1,"score":-4},{"order":2,"score":"-123456"}]}]}]}]})"), s), 1);
  CHECK_EQ(s.golf.rows[0].to_par, -4);
  CHECK_EQ(s.golf.rows[0].score, std::string("-4"));
  CHECK_EQ(s.golf.rows[1].score, std::string("-123"));
  CHECK_EQ(golf_score_text(s.golf.rows[1]), std::string("-99"));
}

static void test_golf_calendar() {
  GolfNext n;
  const std::string pga = slurp("fixtures/golf_calendar_pga_2026.json");
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-11-09T12:00Z"), "401850979", n), 1);
  CHECK(n.valid);
  CHECK_EQ(n.event_id, std::string("401850980"));
  CHECK_EQ(n.name, std::string("Austin Championship pres. by Compliance Solutions"));
  CHECK_EQ(n.start_epoch, parse_iso8601_z("2026-11-12T08:00Z"));
  CHECK_EQ(golf_dates_of(n.start_epoch, n.end_epoch), std::string("Nov 12-15"));
  // During the World Wide Technology week, the event itself is passed over
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-11-04T12:00Z"), "401850979", n), 1);
  CHECK_EQ(n.event_id, std::string("401850980"));
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-11-04T12:00Z"), "", n), 1);
  CHECK_EQ(n.event_id, std::string("401850979"));
  // The first morning of the Austin Championship, past the calendar's 08:00Z
  // start, with the header not listing it yet: still that event, not the RSM
  for (const char *at : {"2026-11-12T07:59Z", "2026-11-12T08:00:30Z", "2026-11-12T20:00Z", "2026-11-13T07:59Z"}) {
    CHECK_EQ(golf_cal_str(pga, parse_iso8601_z(at), "", n), 1);
    CHECK_EQ(n.event_id, std::string("401850980"));
  }
  GameSnapshot card = golf_next_snapshot(n);
  CHECK_EQ(golf_poll_ms(card, parse_iso8601_z("2026-11-12T08:00:30Z")), 5u * 60 * 1000);
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-11-13T08:00Z"), "", n), 1);
  CHECK_EQ(n.event_id, std::string("401850981"));  // a day on, the next one
  // After the Truist Championship's final, ONEflight Myrtle Beach Classic
  // (the same week's opposite-field event) is behind it too
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-05-10T23:00Z"), "401811945", n), 1);
  CHECK_EQ(n.name, std::string("PGA Championship"));
  // The season is over: nothing ahead in this year's list
  GolfNext none;
  CHECK_EQ(golf_cal_str(pga, parse_iso8601_z("2026-12-14T12:00Z"), "", none), 0);
  CHECK(!none.valid);
  CHECK_EQ(golf_cal_str(slurp("fixtures/golf_calendar_pga_2027.json"), parse_iso8601_z("2026-12-14T12:00Z"), "", n),
           1);
  CHECK_EQ(n.name, std::string("The American Express"));
  CHECK_EQ(golf_dates_of(n.start_epoch, n.end_epoch), std::string("Jan 21-24"));
  CHECK_EQ(golf_cal_str(slurp("fixtures/golf_calendar_lpga_2027.json"), parse_iso8601_z("2026-11-24T12:00Z"), "",
                               n),
           1);
  CHECK_EQ(n.name, std::string("Hilton Grand Vacations Tournament of Champions"));
  CHECK_EQ(golf_dates_of(n.start_epoch, n.end_epoch), std::string("Feb 4-7"));
  CHECK_EQ(golf_cal_str(std::string("{}"), 0, "", n), -1);
  CHECK_EQ(golf_cal_str(std::string("nope"), 0, "", n), -1);
  CHECK_EQ(golf_cal_str(std::string(R"({"leagues":[]})"), 0, "", n), 0);
  // The calendar's next event as the board: a PRE card
  GameSnapshot g = golf_next_snapshot(n);
  CHECK(g.valid);
  CHECK(g.state == GameState::PRE);
  CHECK(g.sport == Sport::GOLF);
  CHECK(g.golf.phase == GolfPhase::PRE);
  CHECK_EQ(g.event_id, n.event_id);
  CHECK_EQ(g.kickoff_epoch, n.start_epoch);
  CHECK_EQ(g.golf.end_epoch, n.end_epoch);
  CHECK(!is_team_placeholder(g));
  CHECK_EQ(golf_card_text(g, false, "Feb 4-7"), std::string("Hilton Grand Vacations Tournament\nFeb 4-7"));
  // No word longer than the card's line, 12 characters on one panel
  g.game_name = "Hanwha LIFEPLUS International Crown";
  CHECK_EQ(golf_card_text(g, false, "Sep 9-12"), std::string("Hanwha LIFEPLUS Int'l Crown\nSep 9-12"));
  CHECK_EQ(golf_card_text(g, true, "Sep 9-12"), std::string("Hanwha LIFEPLUS International Crown\nSep 9-12"));
  g.game_name = "Supercalifragilistic Pro-Amateurs";
  CHECK_EQ(golf_card_text(g, false, ""), std::string("Supercalifra Pro-Amateurs"));
}

// No line over its column at either width (fixtures, the placeholder, a card)
static void test_golf_budgets() {
  const char *files[] = {"fixtures/golf_header_pga_suspended.json", "fixtures/golf_header_lpga_live.json",
                         "fixtures/golf_header_eur_after_round.json", "fixtures/golf_header_kft_final.json",
                         "fixtures/golf_header_champions_pre.json", "fixtures/golf_header_liv_canceled.json"};
  std::vector<GameSnapshot> boards;
  for (const char *f : files) {
    GameSnapshot s = golf_doc(f);
    for (auto &r : s.golf.rows)
      r.set_tee(r.state == 'p' ? "12:05p" : "");  // the widest tee time
    boards.push_back(s);
  }
  Team pga{League::PGA, 1106, "PGA", "PGA Tour", 0};
  boards.push_back(team_placeholder(pga));
  GolfNext n;
  n.valid = true;
  n.event_id = "1";
  n.name = "Hilton Grand Vacations Tournament of Champions presented by Somebody Long";
  boards.push_back(golf_next_snapshot(n));
  for (const GameSnapshot &s : boards) {
    for (bool wide : {false, true}) {
      BoardLines l = golf_lines(s, wide);
      CHECK(l.status.size() <= 12);
      if (wide)
        CHECK(l.title.size() + 1 + l.status.size() <= 25);
      else
        CHECK(l.title.empty());
      CHECK(l.pos.size() <= kGolfBoardRows);
      CHECK(l.name.size() == l.pos.size() && l.value.size() == l.pos.size() && l.detail.size() == l.pos.size());
      CHECK(l.value_color.size() == l.pos.size() && l.name_color.size() == l.pos.size());
      for (size_t i = 0; i < l.pos.size(); i++) {
        CHECK(l.pos[i].size() <= 2);
        CHECK(l.name[i].size() <= (wide ? 11u : 7u));
        CHECK(l.value[i].size() <= 3);
        CHECK(l.detail[i].size() <= (wide ? 6u : 0u));
      }
      std::string card = golf_card_text(s, wide, "Oct 29-Nov 1");
      size_t nl = card.find('\n');
      CHECK(card.substr(0, nl).size() <= (wide ? 42u : 36u));
      if (nl != std::string::npos)
        CHECK(card.size() - nl - 1 <= (wide ? 21u : 12u));
      size_t word = 0;
      for (char c : card) {
        word = c == ' ' || c == '\n' || c == '-' ? 0 : word + 1;
        CHECK(word <= (wide ? 21u : 12u));
      }
    }
  }
}

static void test_golf_event_name() {
  struct {
    const char *name;
    size_t max;
    const char *want;
  } cases[] = {
      {"World Wide Technology Championship", 17, "World Wide"},
      {"World Wide Technology Championship", 36, "World Wide Technology Championship"},
      {"Austin Championship pres. by Compliance Solutions", 17, "Austin"},
      {"Austin Championship pres. by Compliance Solutions", 36, "Austin Championship"},
      {"TOTO Japan Classic", 17, "TOTO Japan"},
      {"TOTO Japan Classic", 36, "TOTO Japan Classic"},
      {"Hilton Grand Vacations Tournament of Champions", 36, "Hilton Grand Vacations Tournament"},
      {"The RSM Classic", 17, "The RSM Classic"},
      {"Abu Dhabi Aldar Championship", 17, "Abu Dhabi Aldar"},
      {"Korn Ferry Tour Championship", 17, "Korn Ferry Tour"},
      {"PGA TOUR Q-School presented by Korn Ferry", 17, "PGA TOUR Q-School"},
      {"Mitsubishi Electric Championship at Hualalai", 36, "Mitsubishi Electric Championship"},
      // Real names from ESPN's calendars
      {"The ANNIKA driven by Gainbridge at Pelican", 17, "The ANNIKA"},
      {"The ANNIKA driven by Gainbridge at Pelican", 36, "The ANNIKA"},
      {"Kroger Queen City Championship pres by P&G", 36, "Kroger Queen City Championship"},
      {"Sony Open in Hawaii", 12, "Sony Open"},
      {"Sony Open in Hawaii", 17, "Sony Open"},
      {"Sony Open in Hawaii", 19, "Sony Open in Hawaii"},
      {"Cognizant Classic in The Palm Beaches", 36, "Cognizant Classic"},
      {"Riviera Maya Open at Mayakoba", 19, "Riviera Maya Open"},
      {"TOUR Championship", 16, "TOUR Champ"},
      {"PGA Championship", 12, "PGA Champ"},
      {"PGA Championship", 16, "PGA Championship"},
      {"BMW Championship", 15, "BMW Champ"},
      {"The Championship", 12, "Championship"},
      {"TBD (Additional Event)", 17, "TBD"},
      {"Meijer LPGA Classic for Simply Give", 30, "Meijer LPGA Classic"},
      {"Championship", 5, "Champ"},
      {"Supercalifragilistic Open", 10, "Supercalif"},
      {"", 10, ""},
  };
  for (const auto &c : cases)
    CHECK_EQ(golf_event_name(c.name, c.max), std::string(c.want));
  for (size_t max = 0; max < 40; max++)
    CHECK(golf_event_name("Hilton Grand Vacations Tournament of Champions", max).size() <= max);
}

static void test_golf_poll_ms() {
  const int64_t now = parse_iso8601_z("2026-11-07T03:18Z");
  const int64_t hour = 3600, day = 24 * hour;
  const uint32_t minute = 60 * 1000;
  GameSnapshot g = golf_doc("fixtures/golf_header_lpga_live.json");
  CHECK_EQ(golf_poll_ms(g, now), minute);
  g.golf.phase = GolfPhase::BREAK;
  CHECK_EQ(golf_poll_ms(g, now), 5 * minute);
  g.golf.phase = GolfPhase::PRE;
  g.kickoff_epoch = now + 6 * hour;
  CHECK_EQ(golf_poll_ms(g, now), 5 * minute);
  g.kickoff_epoch = now - 2 * hour;  // the first day, before ESPN says it started
  CHECK_EQ(golf_poll_ms(g, now), 5 * minute);
  g.kickoff_epoch = now + 13 * hour;
  CHECK_EQ(golf_poll_ms(g, now), 30 * minute);
  g.kickoff_epoch = now + 9 * day;
  CHECK_EQ(golf_poll_ms(g, now), 30 * minute);
  g.kickoff_epoch = now + 11 * day;
  CHECK_EQ(golf_poll_ms(g, now), 360 * minute);
  for (GolfPhase p : {GolfPhase::FINAL, GolfPhase::OFF}) {
    g.golf.phase = p;
    g.kickoff_epoch = now - 4 * day;
    g.golf.end_epoch = now - 1 * day;
    CHECK_EQ(golf_poll_ms(g, now), 30 * minute);
    g.golf.end_epoch = now - 11 * day;
    g.kickoff_epoch = now - 14 * day;
    CHECK_EQ(golf_poll_ms(g, now), 360 * minute);
    g.golf.end_epoch = 0;  // a one-day event with no end date: its start
    g.kickoff_epoch = now - 2 * day;
    CHECK_EQ(golf_poll_ms(g, now), 30 * minute);
  }
}

static void test_golf_older() {
  GameSnapshot shown = golf_doc("fixtures/golf_header_pga_suspended.json");  // round 2, break
  GameSnapshot read = shown;
  CHECK(!golf_read_is_older(shown, read));
  read.period = 1;
  CHECK(golf_read_is_older(shown, read));  // a round back
  read.period = 3;
  read.golf.phase = GolfPhase::LIVE;
  CHECK(!golf_read_is_older(shown, read));  // the next round
  read.period = 2;
  CHECK(!golf_read_is_older(shown, read));  // resumed
  read.golf.phase = GolfPhase::PRE;
  CHECK(golf_read_is_older(shown, read));  // before the event, after a round of it
  shown.golf.phase = GolfPhase::FINAL;
  read.golf.phase = GolfPhase::BREAK;
  CHECK(golf_read_is_older(shown, read));  // the final, taken back
  // Another event: older when it started earlier
  read = shown;
  read.event_id = "401850978";
  read.kickoff_epoch = shown.kickoff_epoch - 7 * 86400;
  CHECK(golf_read_is_older(shown, read));
  read.event_id = "401850980";
  read.kickoff_epoch = shown.kickoff_epoch + 7 * 86400;
  read.period = 0;
  read.golf.phase = GolfPhase::PRE;
  CHECK(!golf_read_is_older(shown, read));
  // The calendar's card (midnight Pacific) and the header's event the same
  // days (midnight Eastern), another event id: not an older copy
  GameSnapshot card = golf_next_snapshot(
      GolfNext{true, "401811946", "ONEflight Myrtle Beach Classic", parse_iso8601_z("2026-11-12T08:00Z"),
               parse_iso8601_z("2026-11-15T08:00Z")});
  read = shown;
  read.event_id = "401811945";
  read.kickoff_epoch = parse_iso8601_z("2026-11-12T05:00Z");
  CHECK(!golf_read_is_older(card, read));
  read.kickoff_epoch = parse_iso8601_z("2026-11-05T05:00Z");  // last week's
  CHECK(golf_read_is_older(card, read));
  // Nothing to compare with: a boot, the placeholder, another sport
  CHECK(!golf_read_is_older(GameSnapshot{}, read));
  Team pga{League::PGA, 1106, "PGA", "PGA Tour", 0};
  CHECK(!golf_read_is_older(team_placeholder(pga), read));
  GameSnapshot nfl = shown;
  nfl.sport = Sport::FOOTBALL;
  CHECK(!golf_read_is_older(nfl, read));
}

static void test_golf_off_season() {
  const int64_t now = parse_iso8601_z("2026-12-20T15:00Z");
  const int64_t day = 86400;
  GameSnapshot g = golf_next_snapshot(GolfNext{true, "401916371", "The American Express", now + 32 * day, now + 35 * day});
  CHECK(golf_off_season(g, now));  // the next season, a month out
  g.kickoff_epoch = now + 9 * day;
  CHECK(!golf_off_season(g, now));
  g = golf_doc("fixtures/golf_header_kft_final.json");  // final on 10/11
  CHECK(golf_off_season(g, now));
  CHECK(!golf_show_next(g, now));  // nothing known after it
  g.golf.next_unknown = true;      // the calendar read failed: the final stays up
  CHECK(!golf_off_season(g, now));
  CHECK(!golf_show_next(g, now));
  g.golf.next_unknown = false;
  g.golf.next = GolfNext{true, "1", "Next One", now + 20 * day, now + 23 * day};
  CHECK(golf_off_season(g, now));
  CHECK(golf_show_next(g, now));  // the board shows the next event, which reaches the clock itself
  CHECK(golf_off_season(golf_next_snapshot(g.golf.next), now));
  g.golf.next.start_epoch = now + 5 * day;
  CHECK(!golf_off_season(g, now));
  CHECK(!golf_off_season(golf_next_snapshot(g.golf.next), now));
  // A final two days ago keeps its leaderboard
  const int64_t sunday = parse_iso8601_z("2026-10-11T04:00:00Z");
  CHECK(!golf_off_season(g, sunday + 2 * day));
  CHECK(!golf_show_next(g, sunday + 2 * day));
  CHECK(golf_off_season(golf_doc("fixtures/golf_header_liv_canceled.json"), now));
  CHECK(!golf_off_season(golf_doc("fixtures/golf_header_lpga_live.json"), now));
  Team pga{League::PGA, 1106, "PGA", "PGA Tour", 0};
  CHECK(!golf_off_season(team_placeholder(pga), now));
  CHECK(!golf_off_season(GameSnapshot{}, now));
}

// ---- racing ----------------------------------------------------------------------
// A series followed like a golf tour, from its scoreboard header, one entry per
// session (fixtures recorded 2026-11-07 05:20Z, see fixtures/README.md).

static int racing_str(std::string json, GameSnapshot &s) { return parse_racing(json, s); }
static int racing_cal_str(std::string json, int64_t now, const std::string &after, GolfNext &n,
                          int64_t after_start = 0) {
  return parse_racing_calendar(json, now, after, after_start, n);
}

static GameSnapshot racing_doc(const char *file) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK_EQ(racing_str(json, s), 1);
  return s;
}

// The panel's racing_when in UTC
static std::string racing_when_utc(int64_t at, int64_t now) { return racing_when(utc_tm(at), utc_tm(now)); }

static void test_racing_urls() {
  CHECK(is_racing(League::F1) && is_racing(League::NASCAR) && !is_racing(League::PGA) && !is_racing(League::NFL));
  CHECK(is_leaderboard(League::F1) && is_leaderboard(League::NASCAR) && is_leaderboard(League::LPGA));
  CHECK(!is_leaderboard(League::NFL) && !is_leaderboard(League::MCBB));
  CHECK(is_board_sport(Sport::RACING) && is_board_sport(Sport::GOLF) && !is_board_sport(Sport::SOCCER));
  CHECK(league_sport(League::NASCAR) == Sport::RACING);
  CHECK_EQ(std::string(league_key(League::F1)), std::string("f1"));
  CHECK(league_by_key("f1") != nullptr && league_by_key("f1")->league == League::F1);
  CHECK(league_by_key("nascar") != nullptr && league_by_key("nascar")->league == League::NASCAR);
  CHECK_EQ(header_url(League::F1),
           std::string("https://site.api.espn.com/apis/v2/scoreboard/header?sport=racing&league=f1"));
  CHECK_EQ(header_url(League::NASCAR),
           std::string("https://site.api.espn.com/apis/v2/scoreboard/header?sport=racing&league=nascar-premier"));
  // The coming Tuesday's scoreboard (6 KB, the calendar and no events), or next year's
  CHECK_EQ(golf_calendar_url(League::F1, parse_iso8601_z("2026-11-08T23:00Z"), false),
           std::string("https://site.api.espn.com/apis/site/v2/sports/racing/f1/scoreboard?dates=20261110"));
  CHECK_EQ(golf_calendar_url(League::NASCAR, parse_iso8601_z("2026-11-09T12:00Z"), true),
           std::string("https://site.api.espn.com/apis/site/v2/sports/racing/nascar-premier/scoreboard?dates=2027"));
  set_site_root("https://bench.invalid/sports/");
  CHECK_EQ(header_url(League::F1),
           std::string("https://bench.invalid/apis/v2/scoreboard/header?sport=racing&league=f1"));
  set_site_root("https://site.api.espn.com/apis/site/v2/sports/");
  CHECK_EQ(team_logo_url(League::F1, 2030, "F1"), std::string(""));
  CHECK_EQ(team_logo_url(League::NASCAR, 2021, "NASCAR"), std::string(""));
  // The series are rows in the team table, after golf's tours: Home
  // Assistant's options and /gameday/set
  size_t lpga = kTeamCount, f1 = kTeamCount, nascar = kTeamCount;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::LPGA)
      lpga = i;
    if (kTeams[i].league == League::F1 && kTeams[i].espn_id == 2030) {
      f1 = i;
      CHECK_EQ(team_option(kTeams[i]), std::string("Racing: Formula 1"));
      CHECK_EQ(std::string(kTeams[i].abbr), std::string("F1"));
    }
    if (kTeams[i].league == League::NASCAR && kTeams[i].espn_id == 2021) {
      nascar = i;
      CHECK_EQ(team_option(kTeams[i]), std::string("Racing: NASCAR Cup Series"));
    }
  }
  CHECK(lpga < kTeamCount && f1 == lpga + 1 && nascar == lpga + 2);
  // Appended: every earlier option keeps its place. Only leagues added later
  // (minor league baseball) come after.
  for (size_t i = nascar + 1; i < kTeamCount; i++)
    CHECK((int) kTeams[i].league > (int) League::NASCAR);
}

// Sao Paulo, Saturday morning: FP1 and FP2 run, FP3 at 14:30Z
static void test_racing_break() {
  GameSnapshot s = racing_doc("fixtures/racing_header_f1_practice.json");
  CHECK(s.valid);
  CHECK(s.sport == Sport::RACING);
  CHECK(s.race.phase == RacePhase::BREAK);
  CHECK(s.state == GameState::IN);
  CHECK(!s.completed);
  CHECK(s.race.kind == RaceKind::PRACTICE);
  CHECK_EQ((int) s.race.number, 2);
  CHECK(s.race.next_kind == RaceKind::PRACTICE);
  CHECK_EQ((int) s.race.next_number, 3);
  CHECK_EQ(s.race.next_start, parse_iso8601_z("2026-11-07T14:30Z"));
  CHECK_EQ(s.race.race_epoch, parse_iso8601_z("2026-11-08T17:00Z"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-06T15:30Z"));
  CHECK_EQ(s.race.session_start, parse_iso8601_z("2026-11-06T19:00Z"));
  CHECK_EQ(s.event_id, std::string("600057448"));
  CHECK_EQ(s.game_name, std::string("MSC Cruises Sao Paulo Grand Prix"));
  CHECK_EQ(s.venue, std::string("Autodromo Jose Carlos Pace"));
  CHECK_EQ(s.tv, std::string("Apple TV"));
  CHECK_EQ(s.race.rows.size(), kRaceRows);
  const char *names[] = {"Antonelli", "Norris", "Verstappen", "Russell", "Piastri", "Hamilton"};
  const char *gaps[] = {"1:10.763", "+0.253", "+0.259", "+0.273", "+0.321", "+0.450"};
  const char *nums[] = {"12", "1", "3", "63", "81", "44"};
  const char *detail[] = {"1:10.7", "+0.253", "+0.259", "+0.273", "+0.321", "+0.450"};
  for (size_t i = 0; i < 6; i++) {
    const RaceRow &r = s.race.rows[i];
    CHECK_EQ((int) r.place, (int) i + 1);
    CHECK_EQ(r.name, std::string(names[i]));
    CHECK_EQ(r.gap, std::string(gaps[i]));
    CHECK_EQ(r.num, std::string(nums[i]));
    CHECK(!r.winner);
    CHECK_EQ((int) r.out, 0);
    CHECK_EQ((int) r.laps_down, 0);
  }
  CHECK_EQ(racing_state_text(s), std::string("FP2 final"));
  CHECK_EQ(racing_state_long(s), std::string("Practice 2 final"));
  CHECK_EQ(racing_state_color(s), (uint32_t) 0xFFFFFF);
  BoardLines l = racing_lines(s, false);
  CHECK_EQ(l.status, std::string("FP2 final"));
  CHECK(l.title.empty());
  CHECK(!l.value_narrow);  // no car numbers on one panel: the names get the room
  CHECK_EQ(l.pos.size(), kGolfBoardRows);
  for (size_t i = 0; i < kGolfBoardRows; i++) {
    CHECK_EQ(l.name[i], std::string(names[i]));
    CHECK_EQ(l.pos[i], std::to_string(i + 1));
    CHECK(l.value[i].empty() && l.detail[i].empty());
    CHECK_EQ(l.name_color[i], 0xFFFFFF);
  }
  BoardLines w = racing_lines(s, true);
  CHECK_EQ(w.title, std::string("Sao Paulo GP"));
  for (size_t i = 0; i < kGolfBoardRows; i++) {
    CHECK_EQ(w.value[i], std::string(nums[i]));
    CHECK_EQ(w.detail[i], std::string(detail[i]));
    CHECK_EQ(w.value_color[i], 0xA0A0A0);
  }
  const int64_t now = parse_iso8601_z("2026-11-07T05:22Z");
  const std::string when = racing_when_utc(s.race.next_start, now);
  CHECK_EQ(when, std::string("Today 2:30p"));
  CHECK_EQ(status_text(s, TickerOptions{}, when),
           std::string("MSC Cruises Sao Paulo Grand Prix | Practice 2 final | 1 Antonelli 1:10.763, 2 Norris +0.253, "
                       "3 Verstappen +0.259, 4 Russell +0.273, 5 Piastri +0.321, 6 Hamilton +0.450, 7 Leclerc +0.720, "
                       "8 Bortoleto +1.017, 9 Hadjar +1.076, 10 Bearman +1.228 | Next: Practice 3, Today 2:30p | "
                       "Apple TV"));
  TickerOptions o;
  o.odds = false;
  CHECK(!contains(status_text(s, o, when), "Apple TV"));
  CHECK_EQ(ha_status_text(s, when), std::string("MSC Cruises Sao Paulo Grand Prix | Practice 2 final | Fastest: Antonelli"));
  CHECK_EQ(clock_text(s), std::string(""));
  // Closer to FP3, closer reads
  CHECK_EQ(racing_poll_ms(s, now), 30u * 60 * 1000);
  CHECK_EQ(racing_poll_ms(s, parse_iso8601_z("2026-11-07T13:00Z")), 5u * 60 * 1000);
  CHECK_EQ(racing_poll_ms(s, parse_iso8601_z("2026-11-07T14:25Z")), 60u * 1000);
  CHECK_EQ(board_poll_ms(s, now), 30u * 60 * 1000);
}

// The US Grand Prix, final: retirements, gaps, the winner
static void test_racing_final() {
  GameSnapshot s = racing_doc("fixtures/racing_header_f1_final.json");
  CHECK(s.race.phase == RacePhase::FINAL);
  CHECK(s.state == GameState::POST);
  CHECK(s.completed);
  CHECK(s.race.kind == RaceKind::RACE);
  CHECK_EQ(s.event_id, std::string("600057446"));
  CHECK_EQ(s.period, 56);
  CHECK_EQ((int) s.race.laps, 56);
  CHECK_EQ(s.race.next_start, (int64_t) 0);
  CHECK_EQ(s.race.race_epoch, parse_iso8601_z("2026-10-25T20:00Z"));
  CHECK(s.race.rows[0].winner);
  CHECK_EQ(s.race.rows[0].name, std::string("Antonelli"));
  CHECK(!s.race.rows[1].winner);
  CHECK_EQ(s.race.rows[0].gap, std::string(""));  // the leader has no gap
  CHECK_EQ(s.race.rows[1].gap, std::string("+13.078"));
  BoardLines l = racing_lines(s, false), w = racing_lines(s, true);
  CHECK_EQ(l.status, std::string("Final"));
  CHECK_EQ(l.status_color, (uint32_t) 0xFFD000);
  CHECK_EQ(l.name_color[0], 0xFFD000);
  CHECK_EQ(l.name_color[1], 0xFFFFFF);
  CHECK_EQ(w.title, std::string("US GP"));
  const char *detail[] = {"", "+13.07", "+17.48", "+18.51", "+30.52", "+34.37"};
  for (size_t i = 0; i < kGolfBoardRows; i++)
    CHECK_EQ(w.detail[i], std::string(detail[i]));
  CHECK_EQ(w.value[0], std::string("12"));
  std::string t = status_text(s, TickerOptions{}, "");
  CHECK_EQ(t.rfind("MSC Cruises United States Grand Prix | Final | Antonelli wins | 2 Leclerc +13.078, "
                   "3 Verstappen +17.485",
                   0),
           (size_t) 0);
  CHECK(t.size() > 18 && t.compare(t.size() - 18, 18, "10 Gasly +1:27.516") == 0);
  CHECK(!contains(t, "Apple TV"));  // not after the race
  CHECK_EQ(ha_status_text(s, ""), std::string("MSC Cruises United States Grand Prix | Final | Antonelli wins"));
  GameSnapshot n = s;
  n.race.next = GolfNext{true, "600057447", "Mexico City Grand Prix", parse_iso8601_z("2026-10-30T18:30Z"),
                         parse_iso8601_z("2026-11-01T20:00Z")};
  CHECK(contains(status_text(n, TickerOptions{}, "Oct 30-Nov 1"), "| Next: Mexico City Grand Prix, Oct 30-Nov 1"));
}

// The US Grand Prix on lap 34 (edited from the final, see fixtures/README.md)
static void test_racing_live() {
  GameSnapshot s = racing_doc("fixtures/racing_header_f1_live.json");
  CHECK(s.race.phase == RacePhase::LIVE);
  CHECK(s.state == GameState::IN);
  CHECK(!s.completed);
  CHECK(s.race.kind == RaceKind::RACE);
  CHECK_EQ(s.period, 34);
  CHECK_EQ((int) s.race.laps, 56);
  CHECK_EQ(s.race.session_start, parse_iso8601_z("2026-10-25T20:00Z"));
  CHECK(!s.race.rows[0].winner);
  CHECK_EQ(racing_state_text(s), std::string("Lap 34/56"));
  CHECK_EQ(racing_state_color(s), (uint32_t) 0x40FF40);
  CHECK_EQ(racing_state_long(s), std::string("Race lap 34/56"));
  CHECK_EQ(racing_poll_ms(s, parse_iso8601_z("2026-10-25T21:00Z")), 30u * 1000);
  CHECK_EQ(ha_status_text(s, ""), std::string("MSC Cruises United States Grand Prix | Race | Leader: Antonelli"));
  CHECK(contains(status_text(s, TickerOptions{}, ""),
                 "MSC Cruises United States Grand Prix | Race lap 34/56 | 1 Antonelli, 2 Leclerc +13.078"));
  CHECK(contains(status_text(s, TickerOptions{}, ""), "| Apple TV"));
  // The total unknown, or no lap yet
  GameSnapshot u = s;
  u.race.laps = 0;
  CHECK_EQ(racing_state_text(u), std::string("Lap 34"));
  u.period = 0;
  CHECK_EQ(racing_state_text(u), std::string("Race live"));
  u.race.kind = RaceKind::SPRINT;
  CHECK_EQ(racing_state_text(u), std::string("Sprint live"));
  u.period = 5;
  u.race.laps = 19;
  CHECK_EQ(racing_state_text(u), std::string("Sprint 5/19"));
  CHECK_EQ(racing_state_long(u), std::string("Sprint lap 5/19"));
  u.period = 999;
  u.race.laps = 999;
  CHECK_EQ(racing_state_text(u), std::string("Sprint 999"));  // no room for the total
  u.race.kind = RaceKind::QUALIFYING;
  CHECK_EQ(racing_state_text(u), std::string("Qual live"));
  CHECK_EQ(racing_state_long(u), std::string("Qualifying live"));
  CHECK_EQ(racing_poll_ms(u, 0), 60u * 1000);
  u.race.kind = RaceKind::SPRINT_QUALIFYING;
  CHECK_EQ(racing_state_text(u), std::string("SQ live"));  // "Sprint Q live" is too long
  CHECK_EQ(racing_state_long(u), std::string("Sprint qualifying live"));
  u.race.kind = RaceKind::PRACTICE;
  u.race.number = 0;
  CHECK_EQ(racing_state_text(u), std::string("Prac live"));
  u.race.number = 3;
  CHECK_EQ(racing_state_text(u), std::string("FP3 live"));
  CHECK_EQ(racing_state_long(u), std::string("Practice 3 live"));
  CHECK_EQ(ha_status_text(u, ""), std::string("MSC Cruises United States Grand Prix | Practice 3 | Leader: Antonelli"));
  // A red flag, a yellow, a stoppage
  std::string doc = slurp("fixtures/racing_header_f1_live.json");
  const std::string lap = "\"fullStatus\":{\"period\":34,";
  size_t at = doc.find(lap);
  CHECK(at != std::string::npos);
  std::string red = doc;
  red.insert(at + lap.size(), "\"flag\":\"RED\",");
  GameSnapshot r;
  CHECK_EQ(racing_str(red, r), 1);
  CHECK_EQ(r.race.flag, 'R');
  CHECK_EQ(racing_state_text(r), std::string("Red flag"));
  CHECK_EQ(racing_state_color(r), (uint32_t) 0xFF3030);
  CHECK_EQ(racing_state_long(r), std::string("Race lap 34/56, red flag"));
  std::string yellow = doc;
  yellow.insert(at + lap.size(), "\"flag\":\"YELLOW\",");
  CHECK_EQ(racing_str(yellow, r), 1);
  CHECK_EQ(r.race.flag, 'Y');
  CHECK_EQ(racing_state_text(r), std::string("Lap 34/56"));
  CHECK_EQ(racing_state_color(r), (uint32_t) 0xFFA000);
  std::string held = doc;
  held.replace(held.find("STATUS_IN_PROGRESS"), 18, "STATUS_SUSPENDED");
  CHECK_EQ(racing_str(held, r), 1);
  CHECK(r.race.phase == RacePhase::LIVE);  // a stopped race is still on
  CHECK_EQ(racing_state_text(r), std::string("Suspended"));
  CHECK_EQ(racing_state_color(r), (uint32_t) 0xFFA000);
  CHECK_EQ(racing_state_long(r), std::string("Race suspended"));
  // Read every 2 minutes for the restart, not every 30 seconds; every 15
  // once it has been stopped six hours past its start
  CHECK_EQ(racing_poll_ms(r, parse_iso8601_z("2026-10-25T21:00Z")), 2u * 60 * 1000);
  CHECK_EQ(racing_poll_ms(r, parse_iso8601_z("2026-10-26T02:01Z")), 15u * 60 * 1000);
  // and it holds back an automatic update only until then
  CHECK(racing_on_track(r, parse_iso8601_z("2026-10-25T21:00Z")));
  CHECK(!racing_on_track(r, parse_iso8601_z("2026-10-26T02:01Z")));
  CHECK(racing_on_track(s, parse_iso8601_z("2026-10-26T02:01Z")));  // on track, however long
  GameSnapshot between = s;
  between.race.phase = RacePhase::BREAK;
  CHECK(!racing_on_track(between, 0));
  CHECK(!racing_on_track(GameSnapshot{}, 0));
  r.race.status = GolfStatus::DELAYED;
  CHECK_EQ(racing_state_text(r), std::string("Delayed"));
  CHECK_EQ(racing_poll_ms(r, parse_iso8601_z("2026-10-25T20:10Z")), 2u * 60 * 1000);
  // The winner's splash: once, on the read that goes final, the race only
  GameSnapshot fin = racing_doc("fixtures/racing_header_f1_final.json");
  Splash sp = racing_splash(s, fin, false);
  CHECK_EQ(sp.text, std::string("ANTONELLI WINS!"));
  CHECK_EQ(sp.color, (uint32_t) 0xFFD000);
  CHECK_EQ(racing_splash(s, fin, true).text, std::string("ANTONELLI WINS!"));
  CHECK_EQ(decide_splash(s, fin, true, false).text, std::string("ANTONELLI WINS!"));
  CHECK_EQ(board_splash(s, fin, false).text, std::string("ANTONELLI WINS!"));
  GameSnapshot brk = s;
  brk.race.phase = RacePhase::BREAK;  // qualifying's order up when the race ends
  CHECK_EQ(racing_splash(brk, fin, false).text, std::string("ANTONELLI WINS!"));
  CHECK(racing_splash(fin, fin, false).text.empty());            // final to final
  CHECK(racing_splash(GameSnapshot{}, fin, false).text.empty());  // a boot or a team change
  GameSnapshot pre = s;
  pre.race.phase = RacePhase::PRE;
  CHECK(racing_splash(pre, fin, false).text.empty());
  GameSnapshot other = s;
  other.event_id = "1";
  CHECK(racing_splash(other, fin, false).text.empty());
  GameSnapshot sprint = fin;
  sprint.race.kind = RaceKind::SPRINT;
  CHECK(racing_splash(s, sprint, false).text.empty());
  // Names: a suffix dropped, each word on a line of the splash
  GameSnapshot longer = fin;
  GolfRow::put(longer.race.rows[0].name, sizeof(longer.race.rows[0].name), "van Gisbergen");
  CHECK_EQ(racing_splash(s, longer, false).text, std::string("VAN GISBERGEN WINS!"));
  GolfRow::put(longer.race.rows[0].name, sizeof(longer.race.rows[0].name), "Stenhouse Jr.");
  CHECK_EQ(racing_splash(s, longer, false).text, std::string("STENHOUSE WINS!"));
  GolfRow::put(longer.race.rows[0].name, sizeof(longer.race.rows[0].name), "Allmendinger");
  CHECK_EQ(racing_splash(s, longer, false).text, std::string("ALLMENDING WINS!"));
  CHECK_EQ(racing_splash(s, longer, true).text, std::string("ALLMENDINGER WINS!"));
}

static void test_racing_sprint() {
  // Singapore's Sprint final on Saturday: no gold, no splash, race wins only
  GameSnapshot s = racing_doc("fixtures/racing_header_f1_sprint.json");
  CHECK(s.race.phase == RacePhase::BREAK);
  CHECK(s.race.kind == RaceKind::SPRINT);
  CHECK(!s.completed);
  CHECK_EQ(racing_state_text(s), std::string("Sprint final"));
  CHECK_EQ(racing_state_long(s), std::string("Sprint final"));
  for (const auto &r : s.race.rows)
    CHECK(!r.winner);
  for (int c : racing_lines(s, true).name_color)
    CHECK_EQ(c, 0xFFFFFF);
  CHECK(contains(ha_status_text(s, ""), "| Sprint final | Winner: "));
  GameSnapshot before = s;
  before.race.phase = RacePhase::LIVE;
  CHECK(racing_splash(before, s, false).text.empty());
  // The Sprint Shootout: ESPN's behindTime is each driver's own lap time, so
  // the gaps come from the times
  GameSnapshot q = racing_doc("fixtures/racing_header_f1_sprint_shootout.json");
  CHECK(q.race.phase == RacePhase::BREAK);
  CHECK(q.race.kind == RaceKind::SPRINT_QUALIFYING);
  CHECK_EQ(racing_session_label(q.race.kind, q.race.number), std::string("Sprint Q"));
  CHECK_EQ(racing_state_text(q), std::string("SQ final"));  // "Sprint Q final" is too long
  CHECK_EQ(racing_lines(q, false).status, std::string("SQ final"));
  GameSnapshot prac = q;
  prac.race.kind = RaceKind::PRACTICE;
  CHECK_EQ(racing_state_text(prac), std::string("Prac final"));
  CHECK_EQ(racing_state_long(q), std::string("Sprint qualifying final"));
  CHECK_EQ(q.race.rows[0].name, std::string("Verstappen"));
  CHECK_EQ(q.race.rows[0].gap, std::string("1:31.156"));
  CHECK_EQ(q.race.rows[1].name, std::string("Russell"));
  CHECK_EQ(q.race.rows[1].gap, std::string("+0.120"));
  CHECK_EQ(q.race.rows[2].name, std::string("Leclerc"));
  CHECK_EQ(q.race.rows[2].gap, std::string("+0.243"));
  CHECK_EQ(racing_lines(q, true).detail[0], std::string("1:31.1"));
  for (const auto &r : q.race.rows)
    CHECK_EQ((int) r.out, 0);  // STATUS_UNKNOWN is a normal row
}

static void test_racing_nascar() {
  // Martinsville, final: no per-driver status, ".000" gaps, laps down negative
  GameSnapshot f = racing_doc("fixtures/racing_header_nascar_final.json");
  CHECK(f.race.phase == RacePhase::FINAL);
  CHECK(f.race.kind == RaceKind::RACE);
  CHECK_EQ(f.event_id, std::string("202611010020"));
  CHECK(f.race.rows[0].winner);
  CHECK_EQ(f.race.rows[0].name, std::string("Byron"));
  CHECK_EQ(f.race.rows[4].name, std::string("van Gisbergen"));
  BoardLines l = racing_lines(f, false), w = racing_lines(f, true);
  CHECK_EQ(l.name[4], std::string("Gisbergen"));
  CHECK_EQ(w.name[4], std::string("Gisbergen"));
  CHECK_EQ(w.value[0], std::string("24"));
  CHECK_EQ(w.value[1], std::string("9"));
  CHECK_EQ(w.detail[0], std::string(""));
  CHECK_EQ(w.detail[1], std::string(""));  // on the lead lap: no gap from NASCAR
  CHECK_EQ(w.title, std::string("Martinsville"));
  CHECK_EQ((int) f.race.rows[7].laps_down, 1);
  CHECK_EQ(racing_detail_text(f.race.rows[7]), std::string("+1L"));
  CHECK_EQ(status_text(f, TickerOptions{}, ""),
           std::string("NASCAR Cup Series at Martinsville | Final | Byron wins | 2 Elliott, 3 Gibbs, 4 Berry, "
                       "5 van Gisbergen, 6 Hamlin, 7 Dillon, 8 Wallace +1 lap, 9 Larson +1 lap, 10 Bell +1 lap"));
  // Miami, the season finale, before it starts: only the race is listed
  GameSnapshot p = racing_doc("fixtures/racing_header_nascar_pre.json");
  CHECK(p.race.phase == RacePhase::PRE);
  CHECK(p.state == GameState::PRE);
  CHECK(p.race.rows.empty());
  CHECK(p.race.next_kind == RaceKind::RACE);
  CHECK_EQ(p.race.next_start, parse_iso8601_z("2026-11-08T20:00Z"));
  CHECK_EQ((int) p.race.laps, 267);
  CHECK_EQ(p.race.session_start, (int64_t) 0);
  CHECK(racing_lines(p, false).name.empty());
  CHECK_EQ(racing_lines(p, true).status, std::string("Upcoming"));
  CHECK_EQ(racing_card_text(p, false, "Sun 3:00p"), std::string("Miami\nRace Sun 3:00p"));
  CHECK_EQ(racing_card_text(p, true, "Sun 3:00p"), std::string("Miami\nRace Sun 3:00p"));
  CHECK_EQ(board_card_text(p, false, "Sun 3:00p"), std::string("Miami\nRace Sun 3:00p"));
  CHECK_EQ(status_text(p, TickerOptions{}, "Sun 3:00p"),
           std::string("NASCAR Cup Series at Miami | Race Sun 3:00p | Homestead-Miami Speedway | NBC/HBO Max"));
  CHECK_EQ(ha_status_text(p, "Sun 3:00p"), std::string("NASCAR Cup Series at Miami | Race Sun 3:00p"));
  CHECK_EQ(racing_poll_ms(p, parse_iso8601_z("2026-11-07T05:22Z")), 30u * 60 * 1000);
  // Daytona's Duels, two races (two events) on one day: the second's final
  GameSnapshot d = racing_doc("fixtures/racing_header_nascar_duels.json");
  CHECK_EQ(d.event_id, std::string("202602120045"));
  CHECK_EQ(d.game_name, std::string("Duel #2"));
  CHECK(d.race.phase == RacePhase::FINAL);
  CHECK_EQ(d.race.rows[0].name, std::string("Elliott"));
  CHECK(d.race.rows[0].winner);
  // The first Duel's final stays up until the second starts, so its splash fires
  std::string duels = slurp("fixtures/racing_header_nascar_duels.json");
  size_t second = duels.find("\"id\":\"202602120045\"");
  CHECK(second != std::string::npos);
  std::string early = duels;
  size_t st = early.find("STATUS_FINAL", second);
  early.replace(st, 12, "STATUS_SCHEDULED");
  size_t state = early.find("\"state\":\"post\"", second);
  early.replace(state, 14, "\"state\":\"pre\"");
  CHECK_EQ(racing_str(early, d), 1);
  CHECK_EQ(d.game_name, std::string("Duel #1"));
  CHECK(d.race.phase == RacePhase::FINAL);
  // Bahrain, canceled
  GameSnapshot c = racing_doc("fixtures/racing_header_f1_canceled.json");
  CHECK(c.race.phase == RacePhase::OFF);
  CHECK(c.state == GameState::POST);
  CHECK(!c.completed);
  CHECK(racing_lines(c, false).name.empty());
  CHECK_EQ(racing_state_text(c), std::string("Canceled"));
  CHECK_EQ(racing_state_color(c), (uint32_t) 0xFF3030);
  CHECK_EQ(racing_card_text(c, false, ""), std::string("Bahrain GP\nCanceled"));
  CHECK_EQ(status_text(c, TickerOptions{}, ""), std::string("Gulf Air Bahrain Grand Prix | Canceled"));
  CHECK_EQ(ha_status_text(c, ""), std::string("Gulf Air Bahrain Grand Prix | Canceled"));
  GameSnapshot pp = c;
  pp.race.status = GolfStatus::POSTPONED;
  CHECK_EQ(racing_state_text(pp), std::string("Postponed"));
  // Nothing listed, and documents that aren't a header
  GameSnapshot none;
  CHECK_EQ(racing_str(slurp("fixtures/racing_header_none.json"), none), 0);
  CHECK(!none.valid);
  CHECK_EQ(racing_str(std::string("{}"), none), -1);
  CHECK_EQ(racing_str(std::string("garbage"), none), -1);
  CHECK_EQ(racing_str(std::string(R"({"sports":[{"leagues":[{"events":[]}]}]})"), none), 0);
  CHECK_EQ(racing_str(std::string(R"({"sports":[{"leagues":[{"events":[{"name":"No id"}]}]}]})"), none), -1);
  CHECK_EQ(racing_str(std::string(R"({"sports":[{"leagues":[{"events":[7]}]}]})"), none), -1);
  CHECK_EQ(status_text(GameSnapshot{}, TickerOptions{}, ""), std::string("No upcoming game"));
  GameSnapshot nf;
  nf.valid = true;
  nf.sport = Sport::RACING;
  CHECK_EQ(status_text(nf, TickerOptions{}, ""), std::string("No upcoming race"));
  // A session whose time isn't one, the rows in ESPN's order however they come
  std::string many = R"({"sports":[{"leagues":[{"events":[{"id":"9","competitionType":{"id":"2"},)"
                     R"("fullStatus":{"type":{"name":"STATUS_FINAL","state":"post"}},"competitors":[)";
  for (int i = 12; i >= 1; i--) {
    many += "{\"order\":" + std::to_string(i) + ",\"place\":" + std::to_string(i) + ",\"lastName\":\"P" +
            std::to_string(i) + "\",\"time\":\"" + (i == 3 ? "-1:27.798" : "1:2" + std::to_string(i % 10) + ".5") +
            "\"" + (i == 3 ? ",\"status\":{\"type\":{\"name\":\"STATUS_DISQUALIFIED\"}}" : "") + "}";
    if (i > 1)
      many += ",";
  }
  many += "]}]}]}]}";
  GameSnapshot m;
  CHECK_EQ(racing_str(many, m), 1);
  CHECK(m.race.phase == RacePhase::BREAK);  // qualifying, and no race listed
  CHECK_EQ(m.race.rows.size(), kRaceRows);
  for (size_t i = 0; i < m.race.rows.size(); i++)
    CHECK_EQ(m.race.rows[i].name, "P" + std::to_string(i + 1));
  CHECK_EQ(m.race.rows[0].gap, std::string("1:21.5"));
  CHECK_EQ(m.race.rows[1].gap, std::string("+1.000"));
  CHECK_EQ(m.race.rows[2].gap, std::string(""));  // a disqualified row's negative time
  CHECK_EQ(racing_detail_text(m.race.rows[2]), std::string("DSQ"));
  CHECK_EQ(m.race.rows[9].gap, std::string(""));  // 1:20.5, ahead of the leader
  // A list in order with no places: the order is the place
  GameSnapshot o;
  CHECK_EQ(racing_str(std::string(R"({"sports":[{"leagues":[{"events":[{"id":"9","competitionType":{"id":"1",)"
                                  R"("abbreviation":"FP1"},"fullStatus":{"type":{"name":"STATUS_IN_PROGRESS",)"
                                  R"("state":"in"}},"competitors":[{"order":2,"lastName":"Second"},)"
                                  R"({"order":1,"lastName":"First"},{"lastName":"None"}]}]}]}]})"),
                      o),
           1);
  CHECK(o.race.phase == RacePhase::LIVE);
  CHECK_EQ(o.race.rows.size(), (size_t) 3);
  CHECK_EQ(o.race.rows[0].name, std::string("First"));
  CHECK_EQ(racing_pos_text(o.race.rows[0]), std::string("1"));
  CHECK_EQ(racing_pos_text(o.race.rows[1]), std::string("2"));
  CHECK_EQ(racing_pos_text(o.race.rows[2]), std::string("-"));
}

static void test_racing_calendar() {
  GolfNext n;
  const std::string f1 = slurp("fixtures/racing_calendar_f1_2026.json");
  CHECK_EQ(racing_cal_str(f1, parse_iso8601_z("2026-11-09T12:00Z"), "600057448", n), 1);
  CHECK(n.valid);
  CHECK_EQ(n.event_id, std::string("600057449"));
  CHECK_EQ(n.name, std::string("Heineken Las Vegas Grand Prix"));
  // Three hours earlier than the calendar says, as the header has it
  CHECK_EQ(n.start_epoch, parse_iso8601_z("2026-11-20T00:30Z"));
  CHECK_EQ(n.end_epoch, parse_iso8601_z("2026-11-22T04:00Z"));
  CHECK_EQ(golf_dates(utc_tm(n.start_epoch), utc_tm(n.end_epoch)), std::string("Nov 20-22"));
  // During a weekend the header doesn't list yet, that weekend
  CHECK_EQ(racing_cal_str(f1, parse_iso8601_z("2026-11-07T05:22Z"), "", n), 1);
  CHECK_EQ(n.event_id, std::string("600057448"));
  CHECK_EQ(n.name, std::string("MSC Cruises Sao Paulo Grand Prix"));
  // The season's last race, until six hours after its start
  CHECK_EQ(racing_cal_str(f1, parse_iso8601_z("2026-12-06T10:00Z"), "", n), 1);
  CHECK_EQ(n.event_id, std::string("600057451"));
  CHECK_EQ(n.name, std::string("Etihad Airways Abu Dhabi Grand Prix"));
  GolfNext none;
  CHECK_EQ(racing_cal_str(f1, parse_iso8601_z("2026-12-07T12:00Z"), "", none), 0);
  CHECK(!none.valid);
  const std::string nascar = slurp("fixtures/racing_calendar_nascar_2026.json");
  CHECK_EQ(racing_cal_str(nascar, parse_iso8601_z("2026-11-02T12:00Z"), "202611010020", n), 1);
  CHECK_EQ(n.event_id, std::string("202611080042"));
  CHECK_EQ(n.name, std::string("NASCAR Cup Series at Miami"));
  CHECK_EQ(n.start_epoch, parse_iso8601_z("2026-11-08T20:00Z"));
  CHECK_EQ(racing_cal_str(nascar, parse_iso8601_z("2026-11-09T12:00Z"), "", none), 0);
  // Daytona's Duels: after the second's final, not the first (it started
  // before the one just over), but the Daytona 500; after the first's, the
  // second
  const int64_t duel1 = parse_iso8601_z("2026-02-13T00:00Z"), duel2 = parse_iso8601_z("2026-02-13T01:45Z");
  CHECK_EQ(racing_cal_str(nascar, parse_iso8601_z("2026-02-13T03:20Z"), "202602120045", n, duel2), 1);
  CHECK_EQ(n.event_id, std::string("202602150001"));
  CHECK_EQ(n.name, std::string("Daytona 500"));
  CHECK_EQ(racing_cal_str(nascar, parse_iso8601_z("2026-02-13T01:20Z"), "202602120044", n, duel1), 1);
  CHECK_EQ(n.event_id, std::string("202602120045"));
  CHECK_EQ(n.name, std::string("Duel #2"));
  // The id is in each entry's $ref; an entry without one is skipped
  CHECK_EQ(racing_cal_str(std::string(R"({"leagues":[{"calendar":[{"label":"No ref","startDate":"2027-03-01T00:00Z",)"
                                      R"("endDate":"2027-03-03T00:00Z","event":{"$ref":"http://x/y"}},{"label":"Ref",)"
                                      R"("startDate":"2027-03-08T00:00Z","event":{"$ref":"http://x/events/123?a=b"}}]}]})"),
                          parse_iso8601_z("2027-01-01T00:00Z"), "", n),
           1);
  CHECK_EQ(n.event_id, std::string("123"));
  CHECK_EQ(n.name, std::string("Ref"));
  CHECK_EQ(n.end_epoch, n.start_epoch);  // no end date: the start
  CHECK_EQ(racing_cal_str(std::string("{}"), 0, "", n), -1);
  CHECK_EQ(racing_cal_str(std::string("nope"), 0, "", n), -1);
  CHECK_EQ(racing_cal_str(std::string(R"({"leagues":[]})"), 0, "", n), 0);
  // The calendar's next race weekend as the board: a PRE card with its dates
  CHECK_EQ(racing_cal_str(f1, parse_iso8601_z("2026-11-09T12:00Z"), "600057448", n), 1);
  GameSnapshot g = racing_next_snapshot(n);
  CHECK(g.valid);
  CHECK(g.state == GameState::PRE);
  CHECK(g.sport == Sport::RACING);
  CHECK(g.race.phase == RacePhase::PRE);
  CHECK_EQ(g.event_id, n.event_id);
  CHECK_EQ(g.kickoff_epoch, n.start_epoch);
  CHECK_EQ(g.race.race_epoch, n.end_epoch);
  CHECK_EQ(g.race.next_start, (int64_t) 0);
  CHECK(!is_team_placeholder(g));
  CHECK(board_next_snapshot(Sport::RACING, n).sport == Sport::RACING);
  CHECK(board_next_snapshot(Sport::GOLF, n).sport == Sport::GOLF);
  CHECK_EQ(racing_card_text(g, false, "Nov 20-22"), std::string("Las Vegas GP\nNov 20-22"));
  CHECK_EQ(status_text(g, TickerOptions{}, "Nov 20-22"), std::string("Heineken Las Vegas Grand Prix | Nov 20-22"));
  CHECK_EQ(racing_poll_ms(g, parse_iso8601_z("2026-11-09T12:00Z")), 30u * 60 * 1000);
}

static void test_racing_names() {
  struct {
    const char *name;
    size_t max;
    const char *want;
  } cases[] = {
      {"MSC Cruises Sao Paulo Grand Prix", 15, "Sao Paulo GP"},
      {"MSC Cruises United States Grand Prix", 15, "US GP"},
      {"Mexico City Grand Prix", 15, "Mexico City GP"},
      {"Mexico City Grand Prix", 12, "Mexico City"},
      {"STC Saudi Arabian Grand Prix", 15, "Saudi Arabian"},
      {"STC Saudi Arabian Grand Prix", 24, "Saudi Arabian GP"},
      {"Qatar Airways Australian Grand Prix", 12, "Australian"},
      {"MSC Cruises Barcelona-Catalunya Grand Prix", 15, "Barcelona"},
      {"MSC Cruises Barcelona-Catalunya Grand Prix", 24, "Barcelona-Catalunya GP"},
      {"Moet & Chandon Belgian Grand Prix", 15, "Belgian GP"},
      {"Moet & Chandon Belgian Grand Prix", 40, "Belgian GP"},
      {"Gulf Air Bahrain Grand Prix in Malaysia", 24, "Bahrain GP"},
      {"Heineken Las Vegas Grand Prix", 24, "Las Vegas GP"},
      {"Etihad Airways Abu Dhabi Grand Prix", 24, "Abu Dhabi GP"},
      {"Crypto.com Miami Grand Prix", 24, "Miami GP"},
      {"Monaco Grand Prix", 24, "Monaco GP"},
      {"NASCAR Cup Series at Miami", 24, "Miami"},
      {"NASCAR Cup Series All Star Race", 24, "All Star Race"},
      {"NASCAR Cup Series at World Wide Technology Raceway", 15, "World Wide"},
      {"NASCAR Cup Series at Circuit of the Americas", 21, "COTA"},
      {"NASCAR Cup Series at Circuit of the Americas", 12, "COTA"},
      {"NASCAR Cup Series at Circuit of the Americas", 24, "Circuit of the Americas"},
      {"NASCAR Cup Series at North Wilkesboro", 15, "Wilkesboro"},
      {"NASCAR Cup Series at North Wilkesboro", 13, "Wilkesboro"},
      {"NASCAR Cup Series at North Wilkesboro", 16, "North Wilkesboro"},
      {"Clash at Bowman Gray", 21, "Clash at Bowman Gray"},
      {"Duel #1", 12, "Duel #1"},
      {"Daytona 500", 12, "Daytona 500"},
      {"Formula 1", 24, "Formula 1"},
      {"", 10, ""},
  };
  for (const auto &c : cases)
    CHECK_EQ(racing_event_name(c.name, c.max), std::string(c.want));
  for (size_t max = 0; max < 30; max++) {
    CHECK(racing_event_name("MSC Cruises Barcelona-Catalunya Grand Prix", max).size() <= max);
    CHECK(racing_event_name("NASCAR Cup Series at World Wide Technology Raceway", max).size() <= max);
  }
  CHECK_EQ(board_event_name(Sport::RACING, "Mexico City Grand Prix", 15), std::string("Mexico City GP"));
  CHECK_EQ(board_event_name(Sport::GOLF, "TOTO Japan Classic", 17), std::string("TOTO Japan"));
  // Names in a column
  CHECK_EQ(racing_name_fit("van Gisbergen", 10), std::string("Gisbergen"));
  CHECK_EQ(racing_name_fit("van Gisbergen", 13), std::string("van Gisbergen"));
  CHECK_EQ(racing_name_fit("Stenhouse Jr.", 11), std::string("Stenhouse"));
  CHECK_EQ(racing_name_fit("Allmendinger", 10), std::string("Allmending"));
  CHECK_EQ(racing_name_fit("Allmendinger", 11), std::string("Allmendinge"));
  CHECK_EQ(racing_name_fit("Hulkenberg", 10), std::string("Hulkenberg"));
  CHECK_EQ(racing_name_fit("van der Garde", 6), std::string("Garde"));
  // The detail column and the ticker's
  RaceRow r;
  CHECK_EQ(racing_detail_text(r), std::string(""));
  CHECK_EQ(racing_pos_text(r), std::string("-"));
  r.place = 7;
  CHECK_EQ(racing_pos_text(r), std::string("7"));
  r.place = 100;
  CHECK_EQ(racing_pos_text(r), std::string("-"));
  struct {
    const char *gap;
    const char *board;
  } gaps[] = {{"+0.084", "+0.084"}, {"+13.078", "+13.07"}, {"+1:20.588", "+1:20"}, {"1:10.763", "1:10.7"}};
  for (const auto &g : gaps) {
    GolfRow::put(r.gap, sizeof(r.gap), g.gap);
    CHECK_EQ(racing_detail_text(r), std::string(g.board));
    CHECK_EQ(racing_detail_long(r), std::string(g.gap));
  }
  r.laps_down = 1;
  CHECK_EQ(racing_detail_text(r), std::string("+1L"));
  CHECK_EQ(racing_detail_long(r), std::string("+1 lap"));
  r.laps_down = 13;
  CHECK_EQ(racing_detail_text(r), std::string("+13L"));
  CHECK_EQ(racing_detail_long(r), std::string("+13 laps"));
  r.laps_down = 999;
  CHECK_EQ(racing_detail_text(r), std::string("+999L"));
  r.out = 'R';
  CHECK_EQ(racing_detail_text(r), std::string("DNF"));
  CHECK_EQ(racing_detail_long(r), std::string("DNF"));
  r.out = 'D';
  CHECK_EQ(racing_detail_text(r), std::string("DSQ"));
  CHECK_EQ(racing_detail_long(r), std::string("DSQ"));
  // Gaps from lap times, in whole milliseconds
  CHECK_EQ(racing_gap("1:10.763", "1:10.510"), std::string("+0.253"));
  CHECK_EQ(racing_gap("1:31.276", "1:31.156"), std::string("+0.120"));
  CHECK_EQ(racing_gap("2:12.004", "1:10.000"), std::string("+1:02.004"));
  CHECK_EQ(racing_gap("58.1", "57.95"), std::string("+0.150"));
  CHECK_EQ(racing_gap("1:35:16.650", "1:35:16.650"), std::string("+0.000"));
  CHECK_EQ(racing_gap("1:09.000", "1:10.000"), std::string(""));  // ahead of the leader
  CHECK_EQ(racing_gap("-1:27.798", "1:10.000"), std::string(""));
  CHECK_EQ(racing_gap("DNF", "1:10.000"), std::string(""));
  CHECK_EQ(racing_gap("1:10.000", ""), std::string(""));
  CHECK_EQ(racing_gap("1::10.0", "1:10.0"), std::string(""));
  CHECK_EQ(racing_gap("1:10.", "1:10.0"), std::string(""));
  // Session names
  CHECK_EQ(racing_session_label(RaceKind::PRACTICE, 1), std::string("FP1"));
  CHECK_EQ(racing_session_label(RaceKind::PRACTICE, 0), std::string("Practice"));
  CHECK_EQ(racing_session_label(RaceKind::QUALIFYING, 0), std::string("Qual"));
  CHECK_EQ(racing_session_label(RaceKind::SPRINT, 0), std::string("Sprint"));
  CHECK_EQ(racing_session_label(RaceKind::RACE, 0), std::string("Race"));
  CHECK_EQ(racing_session_long(RaceKind::PRACTICE, 3), std::string("Practice 3"));
  CHECK_EQ(racing_session_long(RaceKind::SPRINT_QUALIFYING, 0), std::string("Sprint qualifying"));
  CHECK_EQ(racing_session_long(RaceKind::RACE, 0), std::string("Race"));
  // A session's day and time
  const int64_t now = parse_iso8601_z("2026-11-07T05:22Z");  // a Saturday
  CHECK_EQ(racing_when_utc(parse_iso8601_z("2026-11-07T09:30Z"), now), std::string("Today 9:30a"));
  CHECK_EQ(racing_when_utc(parse_iso8601_z("2026-11-08T12:00Z"), now), std::string("Tomorrow 12:00p"));
  CHECK_EQ(racing_when_utc(parse_iso8601_z("2026-11-13T09:30Z"), now), std::string("Fri 9:30a"));
  CHECK_EQ(racing_when_utc(parse_iso8601_z("2026-11-14T09:30Z"), now), std::string("Nov 14 9:30a"));
  CHECK_EQ(racing_when_utc(parse_iso8601_z("2026-11-20T19:30Z"), now), std::string("Nov 20 7:30p"));
  CHECK_EQ(racing_when(mk(2026, 0, -1, 0, 0, -1, 0), utc_tm(now)), std::string(""));
  CHECK_EQ(day_label(utc_tm(parse_iso8601_z("2026-11-08T12:00Z")), utc_tm(now)), std::string("Tomorrow"));
}

// No line over its column at either width (every fixture, the placeholder, a card)
static void test_racing_budgets() {
  const char *files[] = {"fixtures/racing_header_f1_practice.json", "fixtures/racing_header_f1_final.json",
                         "fixtures/racing_header_f1_sprint.json",   "fixtures/racing_header_f1_sprint_shootout.json",
                         "fixtures/racing_header_f1_live.json",     "fixtures/racing_header_f1_canceled.json",
                         "fixtures/racing_header_nascar_pre.json",  "fixtures/racing_header_nascar_final.json",
                         "fixtures/racing_header_nascar_duels.json"};
  std::vector<GameSnapshot> boards;
  for (const char *f : files)
    boards.push_back(racing_doc(f));
  Team f1{League::F1, 2030, "F1", "Formula 1", 0};
  boards.push_back(team_placeholder(f1));
  GolfNext n{true, "1", "NASCAR Cup Series at World Wide Technology Raceway Presented by Somebody", 0, 0};
  boards.push_back(racing_next_snapshot(n));
  GameSnapshot longest = boards[1];
  for (auto &r : longest.race.rows) {
    GolfRow::put(r.name, sizeof(r.name), "Abcdefghijklmnopqrstuvwxyz");
    GolfRow::put(r.num, sizeof(r.num), "1234");
    GolfRow::put(r.gap, sizeof(r.gap), "+1:23:45.678");
  }
  longest.game_name = "Supercalifragilisticexpialidocious Grand Prix";
  boards.push_back(longest);
  for (const GameSnapshot &s : boards) {
    for (bool wide : {false, true}) {
      BoardLines l = racing_lines(s, wide);
      CHECK(l.status.size() <= 12);
      if (wide)
        CHECK(l.title.size() + 1 + l.status.size() <= 25);
      else
        CHECK(l.title.empty());
      CHECK(!l.value_narrow);
      CHECK(l.pos.size() <= kGolfBoardRows);
      CHECK(l.name.size() == l.pos.size() && l.value.size() == l.pos.size() && l.detail.size() == l.pos.size());
      CHECK(l.value_color.size() == l.pos.size() && l.name_color.size() == l.pos.size());
      for (size_t i = 0; i < l.pos.size(); i++) {
        CHECK(l.pos[i].size() <= 2);
        CHECK(l.name[i].size() <= (wide ? 11u : 10u));
        CHECK(l.value[i].size() <= (wide ? 3u : 0u));
        CHECK(l.detail[i].size() <= (wide ? 6u : 0u));
      }
      for (const char *when : {"", "Tomorrow 12:30p", "Nov 20 7:30p", "Oct 30-Nov 1"}) {
        std::string card = racing_card_text(s, wide, when);
        size_t nl = card.find('\n');
        CHECK(card.substr(0, nl).size() <= (wide ? 21u : 24u));
        size_t word = 0;
        for (char c : card) {
          word = c == ' ' || c == '\n' || c == '-' ? 0 : word + 1;
          CHECK(word <= (wide ? 21u : 12u));
        }
      }
    }
  }
}

static void test_racing_timing() {
  const int64_t now = parse_iso8601_z("2026-11-07T05:22Z");
  const int64_t hour = 3600, day = 24 * hour;
  const uint32_t minute = 60 * 1000;
  GameSnapshot g = racing_doc("fixtures/racing_header_f1_practice.json");
  // Live: a race or sprint every 30 seconds, practice and qualifying every minute
  g.race.phase = RacePhase::LIVE;
  for (RaceKind k : {RaceKind::RACE, RaceKind::SPRINT}) {
    g.race.kind = k;
    CHECK_EQ(racing_poll_ms(g, now), minute / 2);
  }
  for (RaceKind k : {RaceKind::PRACTICE, RaceKind::QUALIFYING, RaceKind::SPRINT_QUALIFYING}) {
    g.race.kind = k;
    CHECK_EQ(racing_poll_ms(g, now), minute);
  }
  // Before and between sessions, by the next session's start
  for (RacePhase p : {RacePhase::PRE, RacePhase::BREAK}) {
    g.race.phase = p;
    g.race.next_start = 0;
    g.kickoff_epoch = 0;
    CHECK_EQ(racing_poll_ms(g, now), 30 * minute);  // nothing known
    g.kickoff_epoch = now + 3 * hour;  // with no next session, the weekend's start
    CHECK_EQ(racing_poll_ms(g, now), 30 * minute);
    struct {
      int64_t until;
      uint32_t ms;
    } cases[] = {{10 * 60, minute},         {5 * 60, minute},          {-hour, minute},
                 {-6 * hour + 60, minute},  {-6 * hour, 15 * minute},  {-2 * day, 15 * minute},
                 {10 * 60 + 1, 5 * minute}, {2 * hour, 5 * minute},    {2 * hour + 1, 30 * minute},
                 {44 * day, 30 * minute}};
    for (const auto &c : cases) {
      g.race.next_start = now + c.until;
      CHECK_EQ(racing_poll_ms(g, now), c.ms);
    }
    g.race.next_start = now + 46 * day;
    CHECK_EQ(racing_poll_ms(g, now), p == RacePhase::PRE ? 360 * minute : 30 * minute);
  }
  // After the race (or a canceled one): until the header moves on
  for (RacePhase p : {RacePhase::FINAL, RacePhase::OFF}) {
    g.race.phase = p;
    g.race.race_epoch = now - day;
    CHECK_EQ(racing_poll_ms(g, now), 30 * minute);
    g.race.race_epoch = now - 46 * day;
    CHECK_EQ(racing_poll_ms(g, now), 360 * minute);
    // The off-season clock up (nothing ahead, a day after the race): every 6
    // hours, not every 30 minutes for 45 days
    g.race.race_epoch = now - 2 * day;
    CHECK(racing_off_season(g, now));
    CHECK_EQ(racing_poll_ms(g, now), 360 * minute);
    g.race.next_unknown = true;  // the calendar read failed: the final stays up
    CHECK_EQ(racing_poll_ms(g, now), 30 * minute);
    g.race.next_unknown = false;
    g.race.next = GolfNext{true, "1", "Next GP", now + 12 * day, now + 14 * day};  // the next race's card
    CHECK_EQ(racing_poll_ms(g, now), 30 * minute);
    g.race.next = GolfNext{};
  }
  // The off-season clock: the next race 45 days or more away
  GameSnapshot card = racing_next_snapshot(GolfNext{true, "1", "Next GP", now + 44 * day, now + 46 * day});
  CHECK(!racing_off_season(card, now));
  card.kickoff_epoch = now + 46 * day;
  CHECK(racing_off_season(card, now));
  CHECK(board_off_season(card, now));
  GameSnapshot fin = racing_doc("fixtures/racing_header_f1_final.json");
  const int64_t race = fin.race.race_epoch;
  CHECK(!racing_off_season(fin, race + 23 * hour));  // the final stays up a day
  CHECK(!racing_show_next(fin, race + 25 * hour));   // nothing known after it
  CHECK(racing_off_season(fin, race + 25 * hour));   // and nothing ahead: the clock
  fin.race.next_unknown = true;                      // the calendar read failed: the final stays up
  CHECK(!racing_off_season(fin, race + 25 * hour));
  fin.race.next_unknown = false;
  fin.race.next = GolfNext{true, "600057447", "Mexico City Grand Prix", race + 12 * day, race + 14 * day};
  CHECK(!racing_off_season(fin, race + 25 * hour));
  CHECK(!racing_show_next(fin, race + 23 * hour));
  CHECK(racing_show_next(fin, race + 25 * hour));  // the next race's card
  CHECK(board_show_next(fin, race + 25 * hour));
  CHECK(!racing_off_season(racing_next_snapshot(fin.race.next), race + 25 * hour));
  fin.race.next.start_epoch = race + 89 * day;
  CHECK(racing_off_season(fin, race + 25 * hour));
  CHECK(racing_off_season(racing_next_snapshot(fin.race.next), race + 25 * hour));
  CHECK(!racing_off_season(racing_doc("fixtures/racing_header_f1_live.json"), race));
  Team f1{League::F1, 2030, "F1", "Formula 1", 0};
  CHECK(!racing_off_season(team_placeholder(f1), now));
  CHECK(!racing_off_season(GameSnapshot{}, now));
  // Older CDN copies of the same weekend: an earlier session, phase or lap
  GameSnapshot live = racing_doc("fixtures/racing_header_f1_live.json");  // the race, lap 34
  GameSnapshot qual = live;  // qualifying's order up, the race to come
  qual.race.phase = RacePhase::BREAK;
  qual.race.kind = RaceKind::QUALIFYING;
  qual.race.session_start = parse_iso8601_z("2026-10-24T21:00Z");
  GameSnapshot qual_live = qual;
  qual_live.race.phase = RacePhase::LIVE;
  CHECK(racing_read_is_older(qual, qual_live));  // a stale "Qual live" under "Qual final"
  CHECK(!racing_read_is_older(qual_live, qual));
  CHECK(!racing_read_is_older(qual, live));  // the race started
  CHECK(racing_read_is_older(live, qual));
  GameSnapshot lap33 = live;
  lap33.period = 33;
  CHECK(racing_read_is_older(live, lap33));
  CHECK(!racing_read_is_older(lap33, live));
  CHECK(!racing_read_is_older(live, live));
  GameSnapshot done = racing_doc("fixtures/racing_header_f1_final.json");
  CHECK(!racing_read_is_older(live, done));
  CHECK(racing_read_is_older(done, live));  // the final, taken back
  CHECK(board_read_is_older(done, live));
  // Another weekend: older when it started two days or more before
  GameSnapshot last = done;
  last.event_id = "600057445";
  last.kickoff_epoch = done.kickoff_epoch - 14 * day;
  CHECK(racing_read_is_older(done, last));
  CHECK(!racing_read_is_older(last, done));
  // The calendar's card and the header's weekend, the same event
  GameSnapshot cal = racing_next_snapshot(GolfNext{true, "600057448", "MSC Cruises Sao Paulo Grand Prix",
                                                   parse_iso8601_z("2026-11-06T15:30Z"),
                                                   parse_iso8601_z("2026-11-08T17:00Z")});
  GameSnapshot sp = racing_doc("fixtures/racing_header_f1_practice.json");
  CHECK(!racing_read_is_older(cal, sp));
  CHECK(racing_read_is_older(sp, cal));
  // Nothing to compare with
  CHECK(!racing_read_is_older(GameSnapshot{}, live));
  CHECK(!racing_read_is_older(team_placeholder(f1), live));
  GameSnapshot golf = live;
  golf.sport = Sport::GOLF;
  CHECK(!racing_read_is_older(golf, lap33));
}

// ---- The idle display (idle.h) ----------------------------------------------

// The night hours: start included, end not, across midnight when the end is
// earlier, and none at all when they are the same.
static void test_idle_night_window() {
  const int ten_pm = 22 * 60, seven_am = 7 * 60;
  CHECK(in_night(ten_pm, ten_pm, seven_am));
  CHECK(in_night(23 * 60 + 59, ten_pm, seven_am));
  CHECK(in_night(0, ten_pm, seven_am));  // midnight
  CHECK(in_night(seven_am - 1, ten_pm, seven_am));
  CHECK(!in_night(seven_am, ten_pm, seven_am));
  CHECK(!in_night(12 * 60, ten_pm, seven_am));
  CHECK(!in_night(ten_pm - 1, ten_pm, seven_am));
  // Inside one day: 1 AM to 6 AM
  CHECK(in_night(60, 60, 360));
  CHECK(in_night(359, 60, 360));
  CHECK(!in_night(360, 60, 360));
  CHECK(!in_night(59, 60, 360));
  CHECK(!in_night(23 * 60, 60, 360));
  // The same start and end is no night hours, not all day.
  for (int m = 0; m < kDayMinutes; m += 37)
    CHECK(!in_night(m, 600, 600));
  // 10 PM to 7 AM is nine hours, minute by minute across midnight.
  int night = 0;
  for (int m = 0; m < kDayMinutes; m++)
    night += in_night(m, ten_pm, seven_am) ? 1 : 0;
  CHECK_EQ(night, 9 * 60);
  // From the last minute of the day to the first.
  CHECK(in_night(23 * 60 + 59, 23 * 60 + 59, 0));
  CHECK(!in_night(0, 23 * 60 + 59, 0));
}

// What shows: the mode's board while a followed game is on, the owner's
// choice when nothing is, and at night the quiet version of it.
static void test_idle_state() {
  IdleInputs in;
  in.clock_valid = true;
  in.board_ready = true;
  in.connected = true;
  in.minute = 14 * 60;  // 2 PM
  in.night_on = true;
  in.night_start = 22 * 60;
  in.night_end = 7 * 60;
  auto view = [&](IdleShow show) {
    in.show = (uint8_t) show;
    return idle_state(in);
  };
  // A panel that never chose: the mode's own board, day and night alike
  // until the night hours (then the clock).
  CHECK(view(IdleShow::NEXT).view == IdleView::BOARD);
  CHECK(!view(IdleShow::NEXT).night);
  CHECK(view(IdleShow::CLOCK).view == IdleView::CLOCK);
  CHECK(view(IdleShow::SCORES).view == IdleView::SCORES);
  CHECK(view(IdleShow::TODAY).view == IdleView::TODAY);
  IdleState off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::CLOCK && off.dim && !off.night);
  // A followed game on (or its final's wait, or Follow this game): the board,
  // whatever the choice.
  in.busy = true;
  for (int s = 0; s < kIdleCount; s++) {
    IdleState st = view((IdleShow) s);
    CHECK(st.view == IdleView::BOARD && !st.dim && !st.night);
  }
  // At night: the scores and the mode's board give way to the clock; My team
  // today and Off or dim stay. A followed game still shows, in the night
  // hours (night brightness).
  in.minute = 23 * 60;
  CHECK(view(IdleShow::SCORES).view == IdleView::BOARD);  // busy
  CHECK(view(IdleShow::SCORES).night);
  in.busy = false;
  CHECK(view(IdleShow::NEXT).view == IdleView::CLOCK);
  CHECK(view(IdleShow::SCORES).view == IdleView::CLOCK);
  CHECK(view(IdleShow::CLOCK).view == IdleView::CLOCK);
  CHECK(view(IdleShow::TODAY).view == IdleView::TODAY);
  off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::CLOCK && off.dim && off.night);
  // The night hours off: night never comes.
  in.night_on = false;
  CHECK(!view(IdleShow::SCORES).night);
  CHECK(view(IdleShow::SCORES).view == IdleView::SCORES);
  CHECK(view(IdleShow::NEXT).view == IdleView::BOARD);
  // No clock yet: nothing can be decided, so the mode's board at the owner's
  // brightness, night hours or not.
  in.night_on = true;
  in.clock_valid = false;
  for (int s = 0; s < kIdleCount; s++) {
    IdleState st = view((IdleShow) s);
    CHECK(st.view == IdleView::BOARD && !st.dim && !st.night);
  }
  // A byte from a newer firmware: the mode's own board.
  in.clock_valid = true;
  in.minute = 12 * 60;
  in.show = 9;
  CHECK(idle_state(in).view == IdleView::BOARD);
  CHECK(std::string(idle_view_key(IdleView::BOARD)).empty());
  CHECK_EQ(std::string(idle_view_key(IdleView::TODAY)), std::string("today"));

  // Before the first board after a restart or a new team or mode: the boot
  // screen and that board stay up, but the night hours and Off or dim go by
  // the clock alone. An overnight update's restart at 3 AM stays dark or at
  // the night brightness.
  in.board_ready = false;
  in.minute = 3 * 60;
  off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::BOARD && off.dim && off.night);
  IdleState next = view(IdleShow::NEXT);
  CHECK(next.view == IdleView::BOARD && next.night && !next.dim);
  CHECK(view(IdleShow::CLOCK).view == IdleView::BOARD);
  in.minute = 14 * 60;
  off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::BOARD && off.dim && !off.night);
  // Off Wi-Fi the panel's screens about getting connected stay lit (at the
  // night brightness in the night hours), and the view is as it would be.
  in.board_ready = true;
  in.connected = false;
  off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::CLOCK && !off.dim);
  in.minute = 23 * 60;
  off = view(IdleShow::OFF);
  CHECK(!off.dim && off.night);
  in.connected = true;
  CHECK(view(IdleShow::OFF).dim);
  // A followed game still wins at night, before the first board too.
  in.busy = true;
  in.board_ready = false;
  off = view(IdleShow::OFF);
  CHECK(off.view == IdleView::BOARD && !off.dim && off.night);
}

// The owner's Brightness change in the night hours holds until they end, not
// through a moment without the time or the first board after a change.
static void test_idle_night_override() {
  NightOverride o;
  o.update(true, false);
  o.brightness_changed(false);  // in the day: nothing to hold
  CHECK(!o.on);
  o.update(true, true);  // 10 PM
  CHECK(!o.on);
  o.brightness_changed(true);
  CHECK(o.on);
  o.update(true, true);
  CHECK(o.on);
  // The time unknown for a while (the night reads false then): still theirs
  o.update(false, false);
  CHECK(o.on);
  o.update(true, true);
  CHECK(o.on);
  // 7 AM: the hours end, and the next night starts at the night brightness
  o.update(true, false);
  CHECK(!o.on);
  o.update(true, true);
  CHECK(!o.on);
  // The hours ending while the time was unknown end the change too.
  o.brightness_changed(true);
  o.update(false, false);
  o.update(true, false);
  CHECK(!o.on);
  // After a restart in the night hours the change is gone, as before.
  NightOverride boot;
  boot.update(true, true);
  CHECK(!boot.on);
}

// Whether the mode's own game keeps the panel, from plain values: the live
// against idle decision behind every idle display.
static void test_idle_busy() {
  const int64_t now = parse_iso8601_z("2026-11-08T23:00Z");
  BusyInputs my;  // My team
  my.valid = true;
  my.now = now;
  my.kickoff = now - 3600;
  my.linger_ms = 30 * 60 * 1000;
  // Before the game, and with no game at all: idle.
  my.state = GameState::PRE;
  CHECK(!idle_busy(my));
  my.state = GameState::NOT_FOUND;
  CHECK(!idle_busy(my));
  BusyInputs none = my;
  none.valid = false;
  none.state = GameState::IN;
  CHECK(!idle_busy(none));
  // In progress: busy.
  my.state = GameState::IN;
  CHECK(idle_busy(my));
  // The final: the half hour after the panel saw it end, once.
  my.state = GameState::POST;
  CHECK(!idle_busy(my));  // no wait started (not read as a final yet)
  my.post_seen = true;
  my.saw_play = true;
  my.post_age_ms = 0;
  CHECK(idle_busy(my));
  my.post_age_ms = 29 * 60 * 1000;
  CHECK(idle_busy(my));
  my.post_age_ms = 30 * 60 * 1000;
  CHECK(!idle_busy(my));
  my.post_age_ms = 0;
  my.lingered = true;  // the team endpoint still names a final that had its wait
  CHECK(!idle_busy(my));
  my.lingered = false;
  // Baseball's ten minutes
  my.linger_ms = 10 * 60 * 1000;
  my.post_age_ms = 11 * 60 * 1000;
  CHECK(!idle_busy(my));
  my.linger_ms = 30 * 60 * 1000;
  my.post_age_ms = 60 * 1000;

  // Right after a restart: a final the panel only read as one. Last night's
  // game, which ESPN still names at the 3 AM restart of an overnight update,
  // gets no new wait, so Off or dim stays dark and the clock comes straight
  // back. One that started under four hours ago may have just ended: its wait.
  my.saw_play = false;
  my.kickoff = parse_iso8601_z("2026-11-08T01:20Z");  // 7:20 PM Central the evening before
  my.now = parse_iso8601_z("2026-11-08T09:00Z");      // 3 AM Central
  CHECK(!idle_busy(my));
  my.kickoff = my.now - 3 * 3600;
  CHECK(idle_busy(my));
  my.kickoff = my.now - kFinalFreshSeconds;
  CHECK(!idle_busy(my));
  my.kickoff = 0;  // no start time known: nothing says it is new
  CHECK(!idle_busy(my));
  // A final the panel saw end keeps its wait however long the game ran.
  my.saw_play = true;
  my.kickoff = my.now - 6 * 3600;
  CHECK(idle_busy(my));
  my.now = now;

  // Live games: its own pick until its next look moves on after the final;
  // the finals rotation follows no game; the team card is like My team.
  BusyInputs live = my;
  live.live_mode = true;
  live.state = GameState::IN;
  CHECK(idle_busy(live));
  live.state = GameState::POST;
  live.post_seen = false;
  CHECK(idle_busy(live));
  live.state = GameState::PRE;
  CHECK(!idle_busy(live));
  // A game the scan picked, before its first read lands: its board was
  // cleared for that read, and the scan saw it in progress, so the clock or
  // Off or dim doesn't come up at each Switch every.
  BusyInputs picked = live;
  picked.valid = false;
  picked.state = GameState::NOT_FOUND;
  CHECK(!idle_busy(picked));
  picked.picked = true;
  CHECK(idle_busy(picked));
  picked.recap = true;  // the finals rotation follows no game
  CHECK(!idle_busy(picked));
  picked.recap = false;
  picked.live_fallback = true;  // the team's card goes by its own game
  CHECK(!idle_busy(picked));
  picked.live_fallback = false;
  picked.live_mode = false;  // only Live games picks
  CHECK(!idle_busy(picked));
  live.recap = true;
  live.state = GameState::IN;
  CHECK(!idle_busy(live));
  live.recap = false;
  live.live_fallback = true;
  CHECK(idle_busy(live));
  live.state = GameState::POST;
  CHECK(!idle_busy(live));  // the card's final, with no wait started
  live.post_seen = true;
  live.saw_play = true;
  CHECK(idle_busy(live));

  // Favorite teams: a favorite that took over (Take over before kickoff, the
  // game, After the final) or one in progress; the playlist alone is idle.
  BusyInputs fav;
  fav.favorites = true;
  fav.now = now;
  CHECK(!idle_busy(fav));
  fav.fav_locked = true;
  CHECK(idle_busy(fav));
  fav.fav_locked = false;
  fav.fav_live = true;
  CHECK(idle_busy(fav));
  fav.fav_live = false;
  fav.valid = true;
  fav.state = GameState::IN;  // the card on the board is the playlist's
  CHECK(!idle_busy(fav));

  // Follow this game, and the demo's scripted game, hold whatever else.
  BusyInputs hold;
  hold.hold = true;
  CHECK(idle_busy(hold));
  hold.live_mode = true;
  hold.recap = true;
  CHECK(idle_busy(hold));

  // A golf tour or a racing series: busy while a round or session is on, not
  // overnight between rounds or between a weekend's sessions, and for the
  // half hour after its final, not the day or two ESPN keeps it.
  BusyInputs golf;
  golf.valid = true;
  golf.leaderboard = true;
  golf.now = now;
  golf.kickoff = now - 3 * 86400;  // the event's first day
  golf.linger_ms = kBoardFinalWaitMs;
  golf.state = GameState::IN;
  golf.on_course = true;
  CHECK(idle_busy(golf));
  golf.on_course = false;  // after round 2, or a suspended round
  CHECK(!idle_busy(golf));
  golf.state = GameState::POST;
  golf.post_seen = true;
  golf.saw_play = true;
  golf.post_age_ms = 10 * 60 * 1000;
  CHECK(idle_busy(golf));
  golf.post_age_ms = kBoardFinalWaitMs;
  CHECK(!idle_busy(golf));
  // A restart the day after: the final shows no wait.
  golf.saw_play = false;
  golf.post_age_ms = 0;
  CHECK(!idle_busy(golf));
  golf.state = GameState::PRE;
  CHECK(!idle_busy(golf));
}

// My team today's final after a restart, and the records with the ranks.
static void test_idle_today_final() {
  const int64_t day = parse_iso8601_z("2026-11-08T06:00Z");  // midnight Central
  // The team read names a final that started today, later than the one kept
  // (none, yesterday's, or a doubleheader's first): read it.
  const int64_t seven_pm = day + 19 * 3600;
  CHECK(want_today_final(day, true, seven_pm, 0));
  CHECK(want_today_final(day, true, day, 0));
  CHECK(want_today_final(day, true, seven_pm, day - 5 * 3600));
  CHECK(want_today_final(day, true, seven_pm, day + 13 * 3600));
  // Already kept, yesterday's, not a final, or My team today off.
  CHECK(!want_today_final(day, true, seven_pm, seven_pm));
  CHECK(!want_today_final(day, true, day - 60, 0));
  CHECK(!want_today_final(day, false, seven_pm, 0));
  CHECK(!want_today_final(0, true, seven_pm, 0));

  // The ranks come with a game, and My team today's records line (the
  // ticker's own, records_line) puts each side's in front of it.
  GameSnapshot fin = hoops_event("fixtures/event_mcbb_final.json", kMichigan);
  CHECK_EQ((int) fin.team_rank, 1);
  CHECK_EQ((int) fin.opp_rank, 2);
  CHECK_EQ(espn::detail::records_line(fin, ""), std::string("#1 MICH 37-3 | #2 CONN 34-6"));
  GameSnapshot iowa;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_tbd.json"), Sport::FOOTBALL, 2294, iowa));
  CHECK_EQ((int) iowa.team_rank, 17);
  CHECK_EQ((int) iowa.opp_rank, 0);
  GameSnapshot jxst;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_live.json"), Sport::FOOTBALL, 55, jxst));
  CHECK_EQ((int) jxst.team_rank, 0);
  CHECK_EQ((int) jxst.opp_rank, 0);
  CHECK(espn::detail::records_line(jxst, "").find('#') == std::string::npos);
}

static void test_idle_brightness() {
  IdleState day;
  CHECK_EQ((int) idle_brightness(128, day, 20, 0, false), 128);
  IdleState night;
  night.night = true;
  CHECK_EQ((int) idle_brightness(128, night, 20, 0, false), 20);
  // The owner changed the brightness tonight: theirs.
  CHECK_EQ((int) idle_brightness(128, night, 20, 0, true), 128);
  CHECK_EQ((int) idle_brightness(20, night, 60, 0, true), 20);  // WizMote NIGHT in the night hours
  // Off: dark, day or night.
  IdleState dark;
  dark.dim = true;
  CHECK_EQ((int) idle_brightness(128, dark, 20, 0, false), 0);
  dark.night = true;
  CHECK_EQ((int) idle_brightness(128, dark, 20, 0, false), 0);
  // Dim: the dim level, never brighter than the panel would be anyway.
  IdleState dim;
  dim.dim = true;
  CHECK_EQ((int) idle_brightness(128, dim, 20, 30, false), 30);
  CHECK_EQ((int) idle_brightness(10, dim, 20, 30, false), 10);
  dim.night = true;
  CHECK_EQ((int) idle_brightness(128, dim, 20, 30, false), 20);
  CHECK_EQ((int) idle_brightness(128, dim, 40, 30, false), 30);
  // The remote's brightness buttons wait while Off or dim holds the panel
  // below what it would have: dark, or a dim level under the brightness.
  CHECK(idle_caps(128, dark, 20, 0, false));
  IdleState day_dim;
  day_dim.dim = true;
  CHECK(idle_caps(128, day_dim, 20, 30, false));
  CHECK(!idle_caps(20, day_dim, 20, 30, false));  // 20 shows as 20: a step shows
  CHECK(!idle_caps(30, day_dim, 20, 30, false));
  CHECK(!idle_caps(128, IdleState{}, 20, 0, false));  // not idle
  CHECK(!idle_caps(128, night, 20, 0, false));        // the night hours alone
  CHECK(!idle_caps(128, dim, 20, 30, false));  // night 20 is under the dim level 30
  CHECK(idle_caps(128, dim, 40, 30, false));
  CHECK(idle_caps(128, dim, 20, 30, true));  // the owner's 128 tonight, held at 30
  // Then the heat and power guard on top, as guard_brightness() does it.
  CHECK_EQ((int) guarded_brightness(idle_brightness(200, night, 100, 0, false), 0, true), (int) kPowerCap);
}

static void test_idle_countdown() {
  CHECK_EQ(countdown_text(2 * 3600 + 15 * 60), std::string("in 2h 15m"));
  CHECK_EQ(countdown_text(2 * 3600), std::string("in 2h"));
  CHECK_EQ(countdown_text(45 * 60), std::string("in 45m"));
  // Rounded up: a game 30 seconds away is a minute away, never "in 0m".
  CHECK_EQ(countdown_text(30), std::string("in 1m"));
  CHECK_EQ(countdown_text(61), std::string("in 2m"));
  CHECK_EQ(countdown_text(2 * 3600 - 1), std::string("in 2h"));
  CHECK_EQ(countdown_text(2 * 3600 + 1), std::string("in 2h 1m"));
  CHECK_EQ(countdown_text(23 * 3600 + 59 * 60), std::string("in 23h 59m"));
  // Due, or late (ESPN keeps a game waiting a while)
  CHECK_EQ(countdown_text(0), std::string("Starting"));
  CHECK_EQ(countdown_text(-600), std::string("Starting"));
  // Fits a single panel's 12 characters in spleen_8.
  CHECK(countdown_text(23 * 3600 + 59 * 60).size() <= 12);
}

static void test_idle_hhmm() {
  CHECK_EQ(parse_hhmm("22:00"), 22 * 60);
  CHECK_EQ(parse_hhmm("07:05"), 7 * 60 + 5);
  CHECK_EQ(parse_hhmm("7:05"), 7 * 60 + 5);
  CHECK_EQ(parse_hhmm("00:00"), 0);
  CHECK_EQ(parse_hhmm("23:59"), 23 * 60 + 59);
  CHECK_EQ(parse_hhmm("24:00"), -1);
  CHECK_EQ(parse_hhmm("12:60"), -1);
  CHECK_EQ(parse_hhmm("1200"), -1);
  CHECK_EQ(parse_hhmm(""), -1);
  CHECK_EQ(parse_hhmm(":30"), -1);
  CHECK_EQ(parse_hhmm("1:5"), -1);
  CHECK_EQ(parse_hhmm("001:00"), -1);
  CHECK_EQ(parse_hhmm("ab:cd"), -1);
  CHECK_EQ(parse_hhmm("-1:00"), -1);
  CHECK_EQ(parse_hhmm("10:00 PM"), -1);
  CHECK_EQ(clock_hhmm(7 * 60 + 5), std::string("07:05"));
  CHECK_EQ(clock_hhmm(22 * 60), std::string("22:00"));
  CHECK_EQ(clock_hhmm(kDayMinutes), std::string("00:00"));
  CHECK_EQ(clock_hhmm(-5), std::string("00:00"));
  for (int m = 0; m < kDayMinutes; m += 7)
    CHECK_EQ(parse_hhmm(clock_hhmm(m)), m);
}

// The zone for these tests' local_day(): TZ for the host's mktime, a
// POSIX rule like the ones in timezones_parsed.h. Put back afterwards.
struct ScopedTz {
  std::string old;
  bool had{false};
  explicit ScopedTz(const char *tz) {
    const char *o = getenv("TZ");
    this->had = o != nullptr;
    if (this->had)
      this->old = o;
    setenv("TZ", tz, 1);
    tzset();
  }
  ~ScopedTz() {
    if (this->had)
      setenv("TZ", this->old.c_str(), 1);
    else
      unsetenv("TZ");
    tzset();
  }
};

// Today in the zone tz, the way the panel works it out: its local time now,
// and each midnight from its date. mktime with tm_isdst -1 stands in for the
// time component's conversion, which matches it (ESPTime::recalc_timestamp_local).
static LocalDay idle_day(int64_t now, const char *tz) {
  ScopedTz z(tz);
  time_t t = (time_t) now;
  struct tm local {};
  localtime_r(&t, &local);
  return local_day(now, local, [](int y, int m, int d) -> int64_t {
    struct tm at {};
    at.tm_year = y - 1900;
    at.tm_mon = m - 1;
    at.tm_mday = d;
    at.tm_isdst = -1;
    return (int64_t) mktime(&at);
  });
}

static TodayGame idle_game(GameState state, const char *kick, bool tbd = false) {
  TodayGame g;
  g.valid = true;
  g.state = state;
  g.kickoff = parse_iso8601_z(kick);
  g.tbd = tbd;
  return g;
}

// My team today: the game today counts down, its final stays until local
// midnight, and a day without one is the clock, all in the panel's zone.
static void test_idle_today() {
  // November: standard time in both
  const char *central = "CST6CDT,M3.2.0,M11.1.0", *utc = "UTC0", *berlin = "CET-1CEST,M3.5.0,M10.5.0/3";
  const int64_t noon_central = parse_iso8601_z("2026-11-08T18:00Z");  // Sunday noon, US Central
  // A 7 PM Central kickoff is Monday 01:00 UTC.
  std::vector<TodayGame> sunday_night = {idle_game(GameState::PRE, "2026-11-09T01:00Z")};
  CHECK_EQ(today_pick(sunday_night, idle_day(noon_central, central)), 0);
  CHECK_EQ(today_pick(sunday_night, idle_day(noon_central, utc)), -1);     // tomorrow in UTC
  CHECK_EQ(today_pick(sunday_night, idle_day(noon_central, berlin)), -1);  // tomorrow in Berlin too
  // The day runs midnight to midnight local.
  LocalDay d = idle_day(noon_central, central);
  CHECK_EQ(d.start, parse_iso8601_z("2026-11-08T06:00Z"));
  CHECK_EQ(d.end, parse_iso8601_z("2026-11-09T06:00Z"));

  // The final, until local midnight: at 11:59 PM yes, at 12:01 AM the clock.
  std::vector<TodayGame> final_only = {idle_game(GameState::POST, "2026-11-09T01:00Z")};
  CHECK_EQ(today_pick(final_only, idle_day(parse_iso8601_z("2026-11-09T05:59Z"), central)), 0);
  CHECK_EQ(today_pick(final_only, idle_day(parse_iso8601_z("2026-11-09T06:01Z"), central)), -1);
  // A late game that ends past midnight is the day it started: the clock
  // the next day.
  CHECK_EQ(today_pick(final_only, idle_day(parse_iso8601_z("2026-11-09T07:30Z"), central)), -1);

  // A doubleheader: the second game counts down once the first is over, then
  // its own final is the one that stays.
  std::vector<TodayGame> two = {idle_game(GameState::POST, "2026-11-08T19:00Z"),
                                idle_game(GameState::PRE, "2026-11-09T00:10Z")};
  CHECK_EQ(today_pick(two, d), 1);
  two[1].state = GameState::POST;
  CHECK_EQ(today_pick(two, d), 1);
  two[0].kickoff = parse_iso8601_z("2026-11-09T02:00Z");  // the later one wins
  CHECK_EQ(today_pick(two, d), 0);

  // Favorites: the first game today that hasn't ended, whoever plays it.
  std::vector<TodayGame> favs = {idle_game(GameState::PRE, "2026-11-09T03:00Z"),
                                 idle_game(GameState::PRE, "2026-11-10T01:00Z"),  // tomorrow
                                 idle_game(GameState::PRE, "2026-11-08T21:00Z"),
                                 idle_game(GameState::POST, "2026-11-08T17:00Z")};
  CHECK_EQ(today_pick(favs, d), 2);
  favs[2].state = GameState::POST;
  CHECK_EQ(today_pick(favs, d), 0);
  favs[0].state = GameState::POST;
  CHECK_EQ(today_pick(favs, d), 0);  // the latest final today

  // No game today, a game with nothing to show, and one ESPN dropped.
  std::vector<TodayGame> none = {idle_game(GameState::PRE, "2026-11-12T01:00Z"), TodayGame{},
                                 idle_game(GameState::NOT_FOUND, "2026-11-08T20:00Z")};
  CHECK_EQ(today_pick(none, d), -1);
  CHECK_EQ(today_pick({}, d), -1);

  // A game with no time yet sits at midnight Eastern of its day: Sunday's is
  // today in Central, though its placeholder is Saturday evening there.
  std::vector<TodayGame> tbd = {idle_game(GameState::PRE, "2026-11-08T05:00Z", true)};
  CHECK_EQ(today_pick(tbd, d), 0);
  tbd[0].tbd = false;
  CHECK_EQ(today_pick(tbd, d), -1);  // with a real time, 11 PM Saturday Central
}

// The days the clocks change are 23 and 25 hours long: a Saturday 11 PM final
// is yesterday's on the spring Sunday, and midnight to 1 AM is today on the
// fall one. US Central's 2026 changes are March 8 and November 1.
static void test_idle_dst_day() {
  const char *chicago = "CST6CDT,M3.2.0,M11.1.0";
  LocalDay spring = idle_day(parse_iso8601_z("2026-03-08T17:00Z"), chicago);  // noon CDT
  CHECK_EQ(spring.start, parse_iso8601_z("2026-03-08T06:00Z"));               // midnight CST
  CHECK_EQ(spring.end, parse_iso8601_z("2026-03-09T05:00Z"));                 // midnight CDT
  std::vector<TodayGame> sat = {idle_game(GameState::POST, "2026-03-08T05:15Z")};  // Sat 11:15 PM CST
  CHECK_EQ(today_pick(sat, spring), -1);
  sat[0].kickoff = parse_iso8601_z("2026-03-08T06:30Z");  // Sun 12:30 AM CST
  CHECK_EQ(today_pick(sat, spring), 0);
  // Either side of 2 AM, the same day
  CHECK_EQ(idle_day(parse_iso8601_z("2026-03-08T07:59Z"), chicago).start, spring.start);
  CHECK_EQ(idle_day(parse_iso8601_z("2026-03-08T08:00Z"), chicago).start, spring.start);
  // 11:59 PM CDT is still Sunday, 12:00 AM Monday is not
  CHECK_EQ(idle_day(parse_iso8601_z("2026-03-09T04:59Z"), chicago).start, spring.start);
  CHECK_EQ(idle_day(parse_iso8601_z("2026-03-09T05:00Z"), chicago).start, spring.end);
  LocalDay sat_day = idle_day(parse_iso8601_z("2026-03-07T18:00Z"), chicago);
  CHECK_EQ(sat_day.end - sat_day.start, (int64_t) 86400);
  CHECK_EQ(sat_day.end, spring.start);

  LocalDay fall = idle_day(parse_iso8601_z("2026-11-01T18:00Z"), chicago);  // noon CST
  CHECK_EQ(fall.start, parse_iso8601_z("2026-11-01T05:00Z"));               // midnight CDT
  CHECK_EQ(fall.end, parse_iso8601_z("2026-11-02T06:00Z"));                 // midnight CST
  std::vector<TodayGame> early = {idle_game(GameState::POST, "2026-11-01T05:30Z")};  // 12:30 AM CDT
  CHECK_EQ(today_pick(early, fall), 0);
  early[0].kickoff = parse_iso8601_z("2026-11-01T04:30Z");  // Sat 11:30 PM CDT
  CHECK_EQ(today_pick(early, fall), -1);
  // 1:30 AM comes twice; both are Sunday
  CHECK_EQ(idle_day(parse_iso8601_z("2026-11-01T06:30Z"), chicago).start, fall.start);
  CHECK_EQ(idle_day(parse_iso8601_z("2026-11-01T07:30Z"), chicago).start, fall.start);
  LocalDay mon = idle_day(parse_iso8601_z("2026-11-02T18:00Z"), chicago);
  CHECK_EQ(mon.start, fall.end);
  CHECK_EQ(mon.end - mon.start, (int64_t) 86400);

  // The next midnight across a month's and a year's end, and a leap day.
  LocalDay nye = idle_day(parse_iso8601_z("2026-12-31T18:00Z"), chicago);
  CHECK_EQ(nye.start, parse_iso8601_z("2026-12-31T06:00Z"));
  CHECK_EQ(nye.end, parse_iso8601_z("2027-01-01T06:00Z"));
  LocalDay feb = idle_day(parse_iso8601_z("2028-02-28T18:00Z"), chicago);
  CHECK_EQ(feb.end, parse_iso8601_z("2028-02-29T06:00Z"));
  CHECK_EQ(idle_day(parse_iso8601_z("2027-02-28T18:00Z"), chicago).end, parse_iso8601_z("2027-03-01T06:00Z"));
  CHECK_EQ(idle_day(parse_iso8601_z("2026-04-30T18:00Z"), chicago).end, parse_iso8601_z("2026-05-01T05:00Z"));
  // Midnight conversions that make no sense: 24 hours from the time of day.
  struct tm odd {};
  odd.tm_year = 126;
  odd.tm_mon = 10;
  odd.tm_mday = 8;
  odd.tm_hour = 14;
  const int64_t two_pm = parse_iso8601_z("2026-11-08T14:00Z");
  LocalDay fallback = local_day(two_pm, odd, [](int, int, int) -> int64_t { return -1; });
  CHECK_EQ(fallback.start, parse_iso8601_z("2026-11-08T00:00Z"));
  CHECK_EQ(fallback.end, parse_iso8601_z("2026-11-09T00:00Z"));
  // A zone whose clocks jump at midnight itself (Chile, Saturday 24:00): the
  // day still holds the time, whatever the C library makes of the missing
  // midnight.
  const char *santiago = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";
  const int64_t sun = parse_iso8601_z("2026-09-06T16:00Z");
  LocalDay jump = idle_day(sun, santiago);
  CHECK(jump.start <= sun && sun < jump.end);
  CHECK(jump.end - jump.start >= 22 * 3600 && jump.end - jump.start <= 26 * 3600);
}

// The leagues the idle display's scores read in My team and Favorite teams.
static void test_idle_scan_leagues() {
  std::vector<League> live = {League::MLB, League::NHL};
  CHECK(scan_leagues((uint8_t) Mode::MY_TEAM, live).empty());
  CHECK(scan_leagues((uint8_t) Mode::FAVORITES, live).empty());
  CHECK(scan_leagues((uint8_t) Mode::MY_TEAM, live, true) == live);
  CHECK(scan_leagues((uint8_t) Mode::FAVORITES, live, true) == live);
  CHECK(scan_leagues((uint8_t) Mode::LIVE, live, true) == live);
  CHECK(scan_leagues((uint8_t) Mode::LIVE_NFL, live, true) == std::vector<League>{League::NFL});
  // A golf tour with no favorites has no league to read: no NFL stand-in.
  CHECK(live_leagues(0, (uint8_t) League::PGA, {}, false).empty());
  CHECK(live_leagues(0, (uint8_t) League::PGA, {}) == std::vector<League>{League::NFL});
  CHECK(live_leagues(0, 0xFF, {(uint8_t) League::NBA}, false) == std::vector<League>{League::NBA});
  CHECK(live_leagues(0, (uint8_t) League::NHL, {(uint8_t) League::MLB}, false) ==
        (std::vector<League>{League::MLB, League::NHL}));
}

int main() {
  test_urls();
  test_iso();
  test_team_parse();
  test_scoreboard_live();
  test_scoreboard_post();
  test_event_live();
  test_over_under_values();
  test_odd_documents();
  test_event_matches_scoreboard();
  test_scoreboard_pre_nfl();
  test_splash();
  test_win_splash_every_sport();
  test_splash_extreme_scores();
  test_status_text();
  test_no_game_text();
  test_captive();
  test_captive_redirect_host();
  test_ticker_speed();
  test_setup_qr();
  test_join_ticker();
  test_ticker_host();
  test_hotspot_route();
  test_ha_status_text();
  test_short_game_name();
  test_kickoff_label();
  test_live_games();
  test_upcoming();
  test_clock_text();
  test_halftime_fixture();
  test_carry_football();
  test_halftime_resumes();
  test_end_of_half_in_progress();
  test_break_timelines();
  test_break_edges();
  test_red_zone();
  test_color();
  test_favorites();
  test_favorites_shrunk();
  test_followed_favorite();
  test_follow();
  test_schedule_league();
  test_schedule_due();
  test_team_read_waits();
  test_no_live_rescan();
  test_team_cache();
  test_team_cache_edges();
  test_boot_hold();
  test_rotate_minutes();
  test_rotate_seconds();
  test_rotate_turn_resume();
  test_mlb_urls();
  test_team_placeholder();
  test_mlb_team();
  test_mlb_live();
  test_mlb_pre_post();
  test_mlb_splash();
  test_mlb_home_run();
  test_favorites_filters();
  test_soccer_urls();
  test_soccer_team();
  test_soccer_pre();
  test_soccer_live();
  test_soccer_finals();
  test_soccer_splash();
  test_soccer_text_helpers();
  test_soccer_cups();
  test_lookahead();
  test_tbd_kickoff();
  test_nhl_urls();
  test_nhl_team();
  test_nhl_live();
  test_nhl_pre_post();
  test_nhl_splash();
  test_nba_urls();
  test_nba_team();
  test_nba_live();
  test_wnba_live();
  test_nba_pre_post();
  test_nba_splash();
  test_mcbb_urls();
  test_mcbb();
  test_golf_urls();
  test_golf_live();
  test_golf_suspended();
  test_golf_after_round();
  test_golf_final();
  test_golf_pre();
  test_golf_canceled();
  test_golf_documents();
  test_golf_calendar();
  test_golf_budgets();
  test_golf_event_name();
  test_golf_poll_ms();
  test_golf_older();
  test_golf_off_season();
  test_racing_urls();
  test_racing_break();
  test_racing_final();
  test_racing_live();
  test_racing_sprint();
  test_racing_nascar();
  test_racing_calendar();
  test_racing_names();
  test_racing_budgets();
  test_racing_timing();
  checks++;
  failures += run_football_golden();
  test_logo_lru();
  test_logo_lru_edges();
  test_logo_backoff();
  test_json_allocator();
  test_timezone_index();
  test_postseason();
  test_ranks();
  test_rank_width();
  test_standings();
  test_record_row();
  test_board_layout();
  test_badge();
  test_badge_thresholds();
  test_demo();
  test_recap();
  test_recap_back_days();
  test_recap_share();
  test_later_today();
  test_backoff();
  test_miss_count();
  test_backoff_windows();
  test_cup_backoff();
  test_auto_update();
  test_auto_update_edges();
  test_update_mirror();
  test_update_mark();
  test_boot_log();
  test_restart_reasons();
  test_health_json();
  test_crash_capture();
  test_crash_save();
  test_heat_guard();
  test_heat_guard_cool_at_zero();
  test_power_log();
  test_guarded_brightness();
  test_guard_steps();
  test_guard_json_finite();
  test_offseason();
  test_offseason_edges();
  test_panel_text();
  test_wifi_history();
  test_wifi_causes();
  test_idle_night_window();
  test_idle_state();
  test_idle_brightness();
  test_idle_countdown();
  test_idle_hhmm();
  test_idle_today();
  test_idle_dst_day();
  test_idle_night_override();
  test_idle_busy();
  test_idle_today_final();
  test_idle_scan_leagues();
  failures += run_set_tests(checks);
  failures += run_alert_tests(checks);
  failures += run_bet_tests(checks);
  failures += run_watch_tests(checks);
  failures += run_player_tests(checks);
  failures += run_mlbstats_tests(checks);
  failures += run_hockeytech_tests(checks);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
