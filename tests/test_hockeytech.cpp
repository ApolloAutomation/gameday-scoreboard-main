// Minor league hockey, the AHL and the ECHL, from HockeyTech's feed
// (components/gameday/source_hockeytech.h): the URLs, the times, every parse
// against the recorded documents in fixtures/ht_*.json, and how the results
// go through the shared hockey text, splashes, Live games and the finals
// rotation. No game was on while the documents were recorded, so the live
// states are the recorded ones with the feed's live fields set (live_clock).
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "alerts.h"
#include "bets.h"
#include "follow.h"
#include "health.h"
#include "idle.h"
#include "poll_timing.h"
#include "recap.h"
#include "set_request.h"
#include "source_hockeytech.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define HCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

#define HCHECK_EQ(a, b)                                                         \
  do {                                                                          \
    checks++;                                                                   \
    auto va = (a);                                                              \
    auto vb = (b);                                                              \
    if (!(va == vb)) {                                                          \
      failures++;                                                               \
      std::ostringstream os;                                                    \
      os << va << " != " << vb;                                                 \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str()); \
    }                                                                           \
  } while (0)

std::string slurp(const std::string &name) {
  std::ifstream f("fixtures/" + name, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int64_t at(const char *iso) { return parse_iso8601_z(iso); }

// The parsers take their input by reference (the firmware's is a streaming
// reader): these hand them a copy of a document.
int team_read(std::string json, uint32_t our_id, int64_t now, Schedule &out, std::vector<Upcoming> *up = nullptr,
              size_t max = 0) {
  return parse_ht_team(json, our_id, now, out, up, max);
}
bool game_read(std::string json, League league, uint32_t our_id, GameSnapshot &out) {
  return parse_ht_game(json, league, our_id, out);
}
bool day_read(std::string json, std::vector<LiveGame> &out, int64_t *next = nullptr,
              std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  return parse_ht_day(json, out, next, finals, pending);
}

// The keys gameday-common.yaml sets (its substitutions).
const char *const kAhlKey = "50c2cd9b5e18e390";
const char *const kEchlKey = "2c2b89ea7345cae8";

// Recorded documents (fixtures/README.md). Teams by HockeyTech id.
const uint32_t kSyracuse = 324, kHartford = 307, kLaval = 415, kCleveland = 373, kToronto = 335, kUtica = 390;
const uint32_t kRochester = 323, kHamilton = 457;
const uint32_t kRapidCity = 70, kIdaho = 11, kTahoe = 106, kIndy = 65, kCincinnati = 5, kFortWayne = 60;
const uint32_t kBloomington = 107, kGreenville = 52, kOrlando = 61;

GameSnapshot game(const std::string &file, League league, uint32_t our_id) {
  GameSnapshot g;
  bool ok = game_read(slurp(file), league, our_id, g);
  HCHECK(ok);
  return g;
}

// A recorded game centre clock with its live fields set: what the feed sends
// during play (status 2, the period, the clock left in it, the words).
std::string live_clock(const std::string &file, const char *period, const char *clock, const char *progress = "",
                       const char *period_name = nullptr, const char *season = nullptr) {
  JsonDocument doc;
  deserializeJson(doc, slurp(file));
  JsonObject c = doc["GC"]["Clock"];
  c["status"] = "2";
  c["started"] = "1";
  c["final"] = "0";
  c["period"] = period;
  c["game_clock"] = clock;
  c["progress"] = progress;
  if (period_name != nullptr)
    c["period_name"] = period_name;
  if (season != nullptr)
    c["season_name"] = season;
  c["scoring"]["home"].remove("SO");
  std::string out;
  serializeJson(doc, out);
  return out;
}

// The same clock with the score set.
std::string with_score(const std::string &json, int away, int home) {
  JsonDocument doc;
  deserializeJson(doc, json);
  doc["GC"]["Clock"]["visiting_goal_count"] = std::to_string(away);
  doc["GC"]["Clock"]["home_goal_count"] = std::to_string(home);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// A recorded scorebar with one game's fields set.
std::string edited_bar(const std::string &file, const char *id, const char *key, const char *value) {
  JsonDocument doc;
  deserializeJson(doc, slurp(file));
  for (JsonObject g : doc["SiteKit"]["Scorebar"].as<JsonArray>())
    if (std::string(g["ID"] | "") == id)
      g[key] = value;
  std::string out;
  serializeJson(doc, out);
  return out;
}

void test_urls() {
  // Until the YAML sets them, a league's reads carry no key (and fail).
  HCHECK(ht_team_url(League::AHL, kSyracuse).find("&key=&client_code=&") != std::string::npos);
  HCHECK_EQ(ht_logo_url(League::AHL, kSyracuse), std::string());
  ht_set_feed(League::AHL, kAhlKey, "ahl");
  ht_set_feed(League::ECHL, kEchlKey, "echl");
  ht_set_feed(League::NHL, "x", "nhl");  // not a HockeyTech league: ignored
  HCHECK_EQ(std::string(ht_client(League::AHL)), std::string("ahl"));
  HCHECK_EQ(std::string(ht_client(League::NHL)), std::string(""));
  // The same reads as the recorded team, game and day documents.
  HCHECK_EQ(ht_team_url(League::AHL, kSyracuse),
            std::string("https://lscluster.hockeytech.com/feed/index.php?feed=modulekit&view=scorebar"
                        "&key=50c2cd9b5e18e390&client_code=ahl&team_id=324&numberofdaysback=1&numberofdaysahead=5"
                        "&fmt=json&lang=en"));
  HCHECK_EQ(ht_game_url(League::ECHL, "25591"),
            std::string("https://lscluster.hockeytech.com/feed/index.php?feed=gc&key=2c2b89ea7345cae8"
                        "&client_code=echl&game_id=25591&lang_code=en&fmt=json&tab=clock"));
  HCHECK_EQ(ht_day_url(League::AHL, at("2026-11-07T23:00:00Z")),
            std::string("https://lscluster.hockeytech.com/feed/index.php?feed=modulekit&view=gamesbydate"
                        "&key=50c2cd9b5e18e390&client_code=ahl&fetch_date=2026-11-07&fmt=json&lang=en"));
  // The day turns over at 4 AM Eastern (9 AM UTC), like the ESPN scans: a
  // West Coast game after midnight Eastern is still the day it started.
  HCHECK(ht_day_url(League::ECHL, at("2026-11-08T08:59:00Z")).find("fetch_date=2026-11-07&") != std::string::npos);
  HCHECK(ht_day_url(League::ECHL, at("2026-11-08T09:00:00Z")).find("fetch_date=2026-11-08&") != std::string::npos);
  HCHECK(ht_day_url(League::ECHL, at("2027-01-01T03:00:00Z")).find("fetch_date=2026-12-31&") != std::string::npos);
  // Only HockeyTech's reads are counted on its side.
  HCHECK(is_ht_url(ht_team_url(League::AHL, kSyracuse)));
  HCHECK(is_ht_url(ht_day_url(League::ECHL, at("2026-11-07T23:00:00Z"))));
  HCHECK(!is_ht_url("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/teams/6"));
  HCHECK(!is_ht_url("https://statsapi.mlb.com/api/v1/schedule"));
  HCHECK(!is_ht_url("https://assets.leaguestat.com/ahl/logos/50x50/324.png"));

  // Logos: the 50 px copy, named for its season when the team's file is
  HCHECK_EQ(ht_logo_url(League::AHL, kSyracuse), std::string("https://assets.leaguestat.com/ahl/logos/50x50/324.png"));
  HCHECK_EQ(ht_logo_url(League::AHL, kCleveland),
            std::string("https://assets.leaguestat.com/ahl/logos/50x50/373_94.png"));
  HCHECK_EQ(ht_logo_url(League::ECHL, kRapidCity),
            std::string("https://assets.leaguestat.com/echl/logos/50x50/70.png"));
  // A team with no copy small enough for the logo cache gets none: the panel
  // draws its badge (Hamilton's 50 px file is 500 px and 137 KB; Greenville
  // has none).
  HCHECK_EQ(ht_logo_url(League::AHL, kHamilton), std::string());
  HCHECK_EQ(ht_logo_url(League::ECHL, kGreenville), std::string());
  // A team not in teams.h still gets the plain file
  HCHECK_EQ(ht_logo_url(League::ECHL, 9999), std::string("https://assets.leaguestat.com/echl/logos/50x50/9999.png"));
  HCHECK_EQ(ht_logo_url(League::ECHL, 0), std::string());
  // The team table and a game read give the same URL, so the cache keeps one.
  HCHECK_EQ(team_logo_url(League::AHL, kCleveland, "CLE"), ht_logo_url(League::AHL, kCleveland));
  HCHECK_EQ(team_logo_url(League::ECHL, kGreenville, "GVL"), std::string());
}

void test_league() {
  for (League lg : {League::AHL, League::ECHL}) {
    const LeagueInfo *l = league_info(lg);
    HCHECK(l != nullptr);
    if (l == nullptr)
      continue;
    HCHECK(l->sport == Sport::HOCKEY);
    HCHECK(l->source == Source::HOCKEYTECH);
    HCHECK(l->per_event && l->season_list && l->logo_by_id);
    HCHECK_EQ((int) l->linger_min, 30);
    HCHECK(is_hockeytech(lg));
    HCHECK(!is_mlb_stats(lg));
    HCHECK(!is_leaderboard(lg));
    // The day a scan reads turns over at 4 AM Eastern, as the NHL's
    HCHECK_EQ(scan_day_shift(lg), scan_day_shift(League::NHL));
  }
  HCHECK(!is_hockeytech(League::NHL));
  HCHECK(league_by_key("ahl") != nullptr && league_by_key("ahl")->league == League::AHL);
  HCHECK(league_by_key("echl") != nullptr && league_by_key("echl")->league == League::ECHL);
  HCHECK_EQ(std::string(recap_league_word((uint8_t) League::AHL)), std::string("AHL"));
  HCHECK_EQ(std::string(recap_league_word((uint8_t) League::ECHL)), std::string("ECHL"));
  // The last two values the 16-bit league masks have room for.
  HCHECK_EQ((int) League::AHL, 14);
  HCHECK_EQ((int) League::ECHL, 15);
  HCHECK_EQ(kLeagueCount, (size_t) 16);
  HCHECK_EQ(recap_bit((uint8_t) League::ECHL), (uint16_t) 0x8000);
  HCHECK_EQ(recap_bit(16), (uint16_t) 0);
  HCHECK_EQ(live_mask_from_keys("ahl,echl,nhl"), (uint16_t) ((1u << 14) | (1u << 15) | (1u << 3)));
  std::vector<League> live = live_leagues((1u << 15) | (1u << 14), 0, {});
  HCHECK(live.size() == 2 && live[0] == League::AHL && live[1] == League::ECHL);
  // Auto: the saved team's league and each favorite's
  live = live_leagues(0, (uint8_t) League::ECHL, {(uint8_t) League::AHL});
  HCHECK(live.size() == 2 && live[0] == League::AHL && live[1] == League::ECHL);
  // A team like any other: picked, a favorite
  SetOp op;
  HCHECK(parse_set("team", "ahl:324", op, 16));
  HCHECK(op.team.league == (uint8_t) League::AHL && op.team.id == kSyracuse);
  HCHECK(parse_set("favs", "echl:70,ahl:324,nhl:6", op, 16));
  HCHECK_EQ(op.teams.size(), (size_t) 3);
  HCHECK(!parse_set("team", "echl:324", op, 16));  // Syracuse is in the AHL
  // Every team: a code, and a group that is a logo season (or the badge bit)
  int ahl = 0, echl = 0;
  for (const auto &t : kTeams) {
    if (t.league != League::AHL && t.league != League::ECHL)
      continue;
    (t.league == League::AHL ? ahl : echl)++;
    HCHECK(t.abbr[0] != '\0');
    HCHECK((t.group & ~(kHtNoSmallLogo | 0xFFFFu)) == 0);
  }
  HCHECK_EQ(ahl, 32);
  HCHECK_EQ(echl, 30);
  HCHECK_EQ(no_game_text(true, false, false, 1, false, Source::HOCKEYTECH), std::string("Can't reach HockeyTech"));
  HCHECK_EQ(no_game_text(true, false, true, 1, false, Source::HOCKEYTECH), std::string(""));
}

void test_times() {
  // The feed's start times carry their own offset
  HCHECK_EQ(ht_iso_epoch("2026-11-07T15:00:00-05:00"), at("2026-11-07T20:00:00Z"));
  HCHECK_EQ(ht_iso_epoch("2026-11-07T19:10:00-07:00"), at("2026-11-08T02:10:00Z"));
  HCHECK_EQ(ht_iso_epoch("2026-07-01T12:00:00+02:00"), at("2026-07-01T10:00:00Z"));
  HCHECK_EQ(ht_iso_epoch("2026-11-07T15:00:00Z"), at("2026-11-07T15:00:00Z"));
  HCHECK_EQ(ht_iso_epoch("2026-11-07T15:00:00"), at("2026-11-07T15:00:00Z"));
  HCHECK_EQ(ht_iso_epoch(""), (int64_t) 0);
  HCHECK_EQ(ht_iso_epoch("2026-11-07T15:00:00-xx:00"), at("2026-11-07T15:00:00Z"));
  // A day read's local times, across the zones the two leagues play in
  HCHECK_EQ(ht_local_epoch("2026-11-07", "15:00:00", "America/Toronto"), at("2026-11-07T20:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-07", "18:00:00", "America/Winnipeg"), at("2026-11-07T23:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-07", "18:05:00", "America/Denver"), at("2026-11-08T01:05:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-07", "17:00:00", "America/Vancouver"), at("2026-11-08T00:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-07", "19:00:00", "America/Phoenix"), at("2026-11-08T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-07", "18:00:00", "America/Los_Angeles"), at("2026-11-08T02:00:00Z"));
  // British Columbia, Alberta and Manitoba keep summer time all year from
  // 2 AM on 1 November 2026 (UTC-7, -6 and -5, the feed's own offsets); their
  // days before that keep the old rule.
  HCHECK_EQ(ht_local_epoch("2027-02-15", "14:00:00", "America/Winnipeg"), at("2027-02-15T19:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-03-04", "10:30:00", "America/Winnipeg"), at("2027-03-04T15:30:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-01-16", "19:00:00", "America/Edmonton"), at("2027-01-17T01:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-01-16", "19:00:00", "America/Vancouver"), at("2027-01-17T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-07-01", "19:00:00", "America/Vancouver"), at("2027-07-02T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", "America/Vancouver"), at("2026-01-11T03:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", "America/Edmonton"), at("2026-01-11T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", "America/Winnipeg"), at("2026-01-11T01:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-10-31", "19:00:00", "America/Vancouver"), at("2026-11-01T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-01", "01:30:00", "America/Vancouver"), at("2026-11-01T08:30:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-01", "03:00:00", "America/Vancouver"), at("2026-11-01T10:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-01", "03:00:00", "America/Los_Angeles"), at("2026-11-01T11:00:00Z"));
  // Daylight saving time: on from 2 AM on March's second Sunday (8 March
  // 2026), off from 2 AM on November's first (1 November 2026)
  HCHECK_EQ(ht_local_epoch("2026-03-07", "19:00:00", "America/New_York"), at("2026-03-08T00:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-03-08", "01:59:00", "America/New_York"), at("2026-03-08T06:59:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-03-08", "15:00:00", "America/New_York"), at("2026-03-08T19:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-10-31", "19:00:00", "America/Chicago"), at("2026-11-01T00:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-11-01", "15:00:00", "America/Chicago"), at("2026-11-01T21:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-03-13", "19:00:00", "America/Denver"), at("2027-03-14T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-03-14", "19:00:00", "America/Denver"), at("2027-03-15T01:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-03-13", "19:00:00", "America/Edmonton"), at("2027-03-14T01:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2027-03-14", "19:00:00", "America/Edmonton"), at("2027-03-15T01:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-07-01", "19:00:00", "America/Los_Angeles"), at("2026-07-02T02:00:00Z"));
  // Arizona and Saskatchewan keep standard time all year
  HCHECK_EQ(ht_local_epoch("2026-04-10", "19:00:00", "America/Phoenix"), at("2026-04-11T02:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-04-10", "19:00:00", "America/Regina"), at("2026-04-11T01:00:00Z"));
  // Newfoundland is half an hour off; an unknown zone is Eastern
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", "America/St_Johns"), at("2026-01-10T22:30:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", "America/Indiana/Indianapolis"), at("2026-01-11T00:00:00Z"));
  HCHECK_EQ(ht_local_epoch("2026-01-10", "19:00:00", ""), at("2026-01-11T00:00:00Z"));
  HCHECK_EQ(ht_local_epoch("", "19:00:00", "America/Toronto"), (int64_t) 0);
  // Each recorded scorebar game's local date, time and zone agree with its
  // start and offset: Abbotsford's home games in Vancouver's zone, Calgary's
  // in Edmonton's and Manitoba's in Winnipeg's among them.
  for (const char *file : {"ht_team_ahl_syr.json", "ht_team_echl_rc.json", "ht_team_ahl_abb.json",
                           "ht_team_ahl_cgy.json", "ht_team_ahl_mb.json"}) {
    JsonDocument doc;
    deserializeJson(doc, slurp(file));
    int n = 0;
    for (JsonObject g : doc["SiteKit"]["Scorebar"].as<JsonArray>()) {
      n++;
      HCHECK_EQ(ht_local_epoch(g["Date"] | "", g["ScheduledTime"] | "", g["Timezone"] | ""),
                ht_iso_epoch(g["GameDateISO8601"] | ""));
    }
    HCHECK_EQ(n, 6);
  }
}

void test_states() {
  HCHECK(ht_state("1", "7:00 PM EST") == HtState::PRE);
  HCHECK(ht_state("2", "") == HtState::LIVE);
  HCHECK(ht_state("3", "Unofficial Final") == HtState::FINAL);
  HCHECK(ht_state("4", "Final OT") == HtState::FINAL);
  HCHECK(ht_state("8", "Postponed") == HtState::OFF);
  HCHECK(ht_state("9", "Suspended") == HtState::OFF);
  // Any other code goes by the words, then by whether the game started
  HCHECK(ht_state("", "Final SO") == HtState::FINAL);
  HCHECK(ht_state("7", "Cancelled") == HtState::OFF);
  HCHECK(ht_state("6", "Game Suspended") == HtState::OFF);
  HCHECK(ht_state("5", "") == HtState::PRE);
  HCHECK(ht_state("5", "", true) == HtState::LIVE);
  HCHECK(ht_state("1", "", true) == HtState::PRE);  // the code wins
}

void test_team() {
  const int64_t now = at("2026-11-09T01:30:00Z");  // Sunday evening, as recorded
  // Syracuse: the 11/7 overtime loss to Hartford, then five games to come.
  Schedule s;
  std::vector<Upcoming> up;
  HCHECK_EQ(team_read(slurp("ht_team_ahl_syr.json"), kSyracuse, now, s, &up, 4), 1);
  HCHECK(s.valid);
  HCHECK_EQ(s.event_id, std::string("1029294"));
  HCHECK_EQ(s.kickoff_epoch, at("2026-11-12T00:00:00Z"));
  HCHECK(!s.time_tbd);
  HCHECK_EQ(s.opp_id, kLaval);
  HCHECK_EQ(s.opp_abbr, std::string("LAV"));
  HCHECK_EQ(s.team_record, std::string("5-5-2"));
  HCHECK_EQ(s.opp_record, std::string("10-3-1"));
  HCHECK_EQ(up.size(), (size_t) 4);
  if (up.size() == 4) {
    HCHECK_EQ(up[0].event_id, std::string("1029294"));
    HCHECK(up[0].home);
    HCHECK_EQ(up[0].opp_name, std::string("Rocket"));
    HCHECK_EQ(up[1].opp_id, kToronto);
    HCHECK_EQ(up[1].opp_name, std::string("Marlies"));
    HCHECK_EQ(up[2].opp_id, kUtica);
    HCHECK(!up[2].home);  // at Utica
    HCHECK_EQ(up[2].kickoff_epoch, at("2026-11-14T23:00:00Z"));
    HCHECK_EQ(up[3].opp_id, kRochester);
    HCHECK(up[3].tv.empty());
  }
  // Without the list, the same game
  Schedule s2;
  HCHECK_EQ(team_read(slurp("ht_team_ahl_syr.json"), kSyracuse, now, s2), 1);
  HCHECK_EQ(s2.event_id, std::string("1029294"));
  // So a read in the half hour the 11/7 final lingers (the six-hour read or
  // Refresh Now) would put LAV @ SYR, four days away, up in its place: in My
  // team the read waits for the linger to end and the final is polled.
  const uint32_t linger = 30u * 60000u, final_ms = 5u * 3600000u;
  HCHECK(team_read_waits(Source::HOCKEYTECH, final_ms, final_ms + 10u * 60000u, linger));
  HCHECK(!team_read_waits(Source::HOCKEYTECH, final_ms, final_ms + linger, linger));
  // A game in progress is the one
  HCHECK_EQ(team_read(edited_bar("ht_team_ahl_syr.json", "1029277", "GameStatus", "2"), kSyracuse, now, s, &up, 4), 1);
  HCHECK_EQ(s.event_id, std::string("1029277"));
  HCHECK_EQ(s.opp_id, kHartford);
  HCHECK_EQ(s.team_record, std::string("5-5-2"));
  HCHECK_EQ(up.size(), (size_t) 4);  // not started: the next four
  // A game left over as not started a day after its start is passed over
  HCHECK_EQ(team_read(edited_bar("ht_team_ahl_syr.json", "1029277", "GameStatus", "1"), kSyracuse, now, s), 1);
  HCHECK_EQ(s.event_id, std::string("1029294"));
  // The morning of the 11/7 game, it is the one
  HCHECK_EQ(team_read(edited_bar("ht_team_ahl_syr.json", "1029277", "GameStatus", "1"), kSyracuse,
                      at("2026-11-07T14:00:00Z"), s),
            1);
  HCHECK_EQ(s.event_id, std::string("1029277"));
  // A game with no time yet sits at midnight Eastern of its day, like ESPN's
  HCHECK_EQ(team_read(edited_bar("ht_team_ahl_syr.json", "1029294", "TimeTbd", "1"), kSyracuse, now, s, &up, 4), 1);
  HCHECK(s.time_tbd);
  HCHECK_EQ(s.kickoff_epoch, at("2026-11-11T05:00:00Z"));
  HCHECK(!up.empty() && up[0].time_tbd);

  // Rapid City: three games at Idaho postponed (the last listed), then five
  // to come, the first at home to Tahoe.
  HCHECK_EQ(team_read(slurp("ht_team_echl_rc.json"), kRapidCity, now, s, &up, 4), 1);
  HCHECK_EQ(s.event_id, std::string("25626"));
  HCHECK_EQ(s.kickoff_epoch, at("2026-11-12T02:05:00Z"));
  HCHECK_EQ(s.opp_id, kTahoe);
  HCHECK_EQ(s.team_record, std::string("5-2-0"));
  HCHECK_EQ(s.opp_record, std::string("4-5-1"));
  HCHECK_EQ(up.size(), (size_t) 4);
  if (!up.empty())
    HCHECK_EQ(up[0].opp_name, std::string("Knight Monsters"));
  // From Idaho's side the postponed game is passed over too
  HCHECK_EQ(team_read(slurp("ht_team_echl_rc.json"), kIdaho, now, s), 1);
  HCHECK_EQ(s.event_id, std::string("25626"));

  // Between seasons the scorebar is empty
  const std::string none = "{\"SiteKit\":{\"Parameters\":{\"view\":\"scorebar\"},\"Scorebar\":[]}}";
  HCHECK_EQ(team_read(none, kSyracuse, now, s, &up, 4), 0);
  HCHECK(s.valid && s.event_id.empty());
  HCHECK(up.empty());
  // A bad key gets text, not a document
  HCHECK_EQ(team_read("Invalid key.", kSyracuse, now, s), -1);
  HCHECK_EQ(team_read("{\"SiteKit\":{}}", kSyracuse, now, s), -1);
}

void test_game_final() {
  // CLE @ LAV, Final 4-2, from Laval's side and Cleveland's
  GameSnapshot g = game("ht_game_ahl_final.json", League::AHL, kLaval);
  HCHECK(g.valid);
  HCHECK(g.sport == Sport::HOCKEY);
  HCHECK(g.state == GameState::POST);
  HCHECK(g.completed);
  HCHECK_EQ(g.event_id, std::string("1029271"));
  HCHECK_EQ(g.short_detail, std::string("Final"));
  HCHECK_EQ(g.status_name, std::string("STATUS_FINAL"));
  HCHECK(g.team_home);
  HCHECK_EQ(g.team_abbr, std::string("LAV"));
  HCHECK_EQ(g.opp_abbr, std::string("CLE"));
  HCHECK_EQ(g.team_score, 4);
  HCHECK_EQ(g.opp_score, 2);
  HCHECK(g.team_winner);
  HCHECK_EQ(g.kickoff_epoch, at("2026-11-07T20:00:00Z"));
  HCHECK_EQ(g.venue, std::string("Place Bell"));
  HCHECK_EQ(g.team_logo, std::string("https://assets.leaguestat.com/ahl/logos/50x50/415.png"));
  HCHECK_EQ(g.opp_logo, std::string("https://assets.leaguestat.com/ahl/logos/50x50/373_94.png"));
  HCHECK(!g.nhl.playoffs);
  TickerOptions o;
  HCHECK_EQ(status_text(g, o, ""), std::string("Final"));
  GameSnapshot c = game("ht_game_ahl_final.json", League::AHL, kCleveland);
  HCHECK(!c.team_home);
  HCHECK_EQ(c.team_abbr, std::string("CLE"));
  HCHECK_EQ(c.team_score, 2);
  HCHECK(!c.team_winner);
  // Not our game, and not a clock
  GameSnapshot x;
  HCHECK(!game_read(slurp("ht_game_ahl_final.json"), League::AHL, kSyracuse, x));
  HCHECK(!game_read(slurp("ht_game_ahl_final.json"), League::AHL, 0, x));
  HCHECK(!game_read("Invalid key.", League::AHL, kLaval, x));
  HCHECK(!game_read("{\"GC\":{\"Parameters\":{}}}", League::AHL, kLaval, x));

  // HFD @ SYR, 3-2 in overtime; the ECHL's CIN @ IND, the same in its words
  g = game("ht_game_ahl_final_ot.json", League::AHL, kSyracuse);
  HCHECK_EQ(g.short_detail, std::string("Final/OT"));
  HCHECK_EQ(g.period, 4);
  HCHECK(!g.team_winner);
  g = game("ht_game_echl_final_ot.json", League::ECHL, kIndy);
  HCHECK_EQ(g.short_detail, std::string("Final/OT"));
  HCHECK_EQ(g.opp_id, kCincinnati);
  HCHECK_EQ(g.venue, std::string("Fishers Event Center"));
  // BLM @ FW, 5-4 in a shootout: the game centre leaves the period at OT
  g = game("ht_game_echl_final_so.json", League::ECHL, kBloomington);
  HCHECK_EQ(g.short_detail, std::string("Final/SO"));
  HCHECK_EQ(g.period, 5);
  HCHECK(g.team_winner);
  HCHECK_EQ(g.team_score, 5);
  HCHECK_EQ(g.opp_id, kFortWayne);
  // With the records the team read gave, the ticker carries them
  Schedule s;
  s.event_id = "25591";
  s.team_record = "7-2-1";
  s.opp_id = kFortWayne;
  s.opp_record = "6-2-0";
  ht_fill_from_schedule(g, s);
  HCHECK_EQ(status_text(g, o, ""), std::string("Final/SO | BLM 7-2-1 | FW 6-2-0"));
}

void test_game_postponed() {
  // RC @ IDH, postponed: ESPN's post state with no result and no splash
  GameSnapshot g = game("ht_game_echl_postponed.json", League::ECHL, kRapidCity);
  HCHECK(g.state == GameState::POST);
  HCHECK(!g.completed);
  HCHECK_EQ(g.short_detail, std::string("Postponed"));
  HCHECK_EQ(g.kickoff_epoch, at("2026-11-08T02:10:00Z"));
  TickerOptions o;
  HCHECK_EQ(status_text(g, o, ""), std::string("Postponed"));
  GameSnapshot pre = g;
  pre.state = GameState::PRE;
  pre.short_detail = "Scheduled";
  HCHECK(decide_splash(pre, g, true, false).text.empty());
  // Suspended (status 9) and canceled (by the words)
  std::string json = slurp("ht_game_echl_postponed.json");
  std::string sus = json;
  sus.replace(sus.find("\"status\":\"8\""), 12, "\"status\":\"9\"");
  HCHECK(game_read(sus, League::ECHL, kRapidCity, g));
  HCHECK_EQ(g.short_detail, std::string("Suspended"));
  std::string can = json;
  can.replace(can.find("\"status\":\"8\""), 12, "\"status\":\"7\"");
  can.replace(can.find("\"progress\":\"Postponed\""), 22, "\"progress\":\"Cancelled\"");
  HCHECK(game_read(can, League::ECHL, kRapidCity, g));
  HCHECK(g.state == GameState::POST && !g.completed);
  HCHECK_EQ(g.short_detail, std::string("Canceled"));
}

void test_game_pre() {
  // LAV @ SYR on 11/11, the next game
  GameSnapshot g = game("ht_game_ahl_pre.json", League::AHL, kSyracuse);
  HCHECK(g.state == GameState::PRE);
  HCHECK_EQ(g.short_detail, std::string("Scheduled"));
  HCHECK_EQ(g.kickoff_epoch, at("2026-11-12T00:00:00Z"));
  HCHECK(g.team_home);
  HCHECK_EQ(g.opp_abbr, std::string("LAV"));
  HCHECK_EQ(g.team_score, 0);
  TickerOptions o;
  HCHECK_EQ(status_text(g, o, "Wed 7:00 PM"), std::string("Wed 7:00 PM | Upstate Medical University Arena"));
  // The team read's records and a game with no time yet
  Schedule s;
  s.event_id = "1029294";
  s.kickoff_epoch = at("2026-11-11T05:00:00Z");
  s.time_tbd = true;
  s.team_record = "5-5-2";
  s.opp_id = kLaval;
  s.opp_record = "10-3-1";
  ht_fill_from_schedule(g, s);
  HCHECK(g.time_tbd);
  HCHECK_EQ(g.kickoff_epoch, at("2026-11-11T05:00:00Z"));
  HCHECK_EQ(g.team_record, std::string("5-5-2"));
  HCHECK_EQ(g.opp_record, std::string("10-3-1"));
  // Another opponent's record isn't this one's; the game id fills in only
  // when the clock gave none
  GameSnapshot h = game("ht_game_ahl_pre.json", League::AHL, kSyracuse);
  s.opp_id = kToronto;
  s.event_id = "1";
  ht_fill_from_schedule(h, s);
  HCHECK(h.opp_record.empty());
  HCHECK(!h.time_tbd);
  HCHECK_EQ(h.event_id, std::string("1029294"));
  // A team drawn as a badge has no logo to fetch
  std::string ham = slurp("ht_game_ahl_pre.json");
  ham.replace(ham.find("\"team_id\":\"415\""), 15, "\"team_id\":\"457\"");
  HCHECK(game_read(ham, League::AHL, kSyracuse, h));
  HCHECK_EQ(h.opp_id, kHamilton);
  HCHECK(h.opp_logo.empty());
  HCHECK(!h.team_logo.empty());
}

void test_game_live() {
  TickerOptions o;
  // HFD @ SYR in the 2nd, 12:34 left
  GameSnapshot g;
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "2", "00:12:34", "In Progress"), League::AHL, kSyracuse, g));
  HCHECK(g.state == GameState::IN);
  HCHECK(!g.completed);
  HCHECK_EQ(g.period, 2);
  HCHECK_EQ(g.display_clock, std::string("12:34"));
  HCHECK_EQ(g.short_detail, std::string("12:34 - 2nd"));
  HCHECK(clock_running(g));
  HCHECK_EQ(clock_text(g), std::string("12:34 2nd"));
  HCHECK_EQ(g.team_score, 2);  // the recorded final's score
  // Under a minute, and the scorebar's MM:SS
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "3", "00:00:45"), League::AHL, kSyracuse, g));
  HCHECK_EQ(clock_text(g), std::string("0:45 3rd"));
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "1", "19:59"), League::AHL, kSyracuse, g));
  HCHECK_EQ(clock_text(g), std::string("19:59 1st"));
  // A clock it can't read: the period alone
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "1", "soon"), League::AHL, kSyracuse, g));
  HCHECK_EQ(g.short_detail, std::string("In Progress"));
  // The intermission: the clock at zero between periods
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "1", "00:00:00"), League::AHL, kSyracuse, g));
  HCHECK(g.state == GameState::IN);
  HCHECK_EQ(g.short_detail, std::string("End of 1st"));
  HCHECK_EQ(g.status_name, std::string("STATUS_END_PERIOD"));
  HCHECK(!clock_running(g));
  HCHECK_EQ(clock_text(g), std::string("End of 1st"));
  // The ticker shows the records then, once the team read gave them
  Schedule s;
  s.team_record = "5-5-2";
  s.opp_id = kHartford;
  s.opp_record = "7-8-1";
  ht_fill_from_schedule(g, s);
  HCHECK_EQ(status_text(g, o, ""), std::string("SYR 5-5-2 | HFD 7-8-1"));
  // ...or by the words, with the intermission's own clock running
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "2", "00:17:00", "2nd Intermission"), League::AHL,
                   kSyracuse, g));
  HCHECK_EQ(clock_text(g), std::string("End of 2nd"));
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "3", "00:00:00", "End of 3rd"), League::AHL, kSyracuse, g));
  HCHECK_EQ(clock_text(g), std::string("End of 3rd"));
  // The puck dropped and the clock isn't set yet (the pre-game clock is
  // zero, and no shot or goal in the period): the period alone, not an
  // intermission. The same as a later period starts.
  HCHECK(game_read(live_clock("ht_game_ahl_pre.json", "1", "00:00:00", "In Progress"), League::AHL, kSyracuse, g));
  HCHECK(g.state == GameState::IN);
  HCHECK_EQ(g.short_detail, std::string("1st"));
  HCHECK(g.display_clock.empty());
  HCHECK_EQ(clock_text(g), std::string("1st"));
  HCHECK_EQ(g.status_name, std::string("STATUS_IN_PROGRESS"));
  HCHECK(game_read(live_clock("ht_game_ahl_pre.json", "2", "00:00:00"), League::AHL, kSyracuse, g));
  HCHECK_EQ(clock_text(g), std::string("2nd"));
  // A goal in the period is enough to call it played
  {
    JsonDocument doc;
    deserializeJson(doc, live_clock("ht_game_ahl_pre.json", "2", "00:00:00"));
    doc["GC"]["Clock"]["scoring"]["visiting"]["2"] = "1";
    std::string j;
    serializeJson(doc, j);
    HCHECK(game_read(j, League::AHL, kSyracuse, g));
    HCHECK_EQ(clock_text(g), std::string("End of 2nd"));
  }
  // Overtime
  HCHECK(game_read(live_clock("ht_game_echl_final_ot.json", "4", "00:03:10", "", "1st OT"), League::ECHL, kIndy, g));
  HCHECK_EQ(clock_text(g), std::string("3:10 OT"));
  HCHECK_EQ(g.short_detail, std::string("3:10 - OT"));
  // The shootout, by the period's name or a fifth period
  HCHECK(game_read(live_clock("ht_game_echl_final_so.json", "5", "00:00:00", "", "SO"), League::ECHL, kFortWayne, g));
  HCHECK(g.state == GameState::IN);
  HCHECK_EQ(g.period, 5);
  HCHECK_EQ(clock_text(g), std::string("SO"));
  HCHECK(game_read(live_clock("ht_game_echl_final_so.json", "5", "00:00:00", "", "Shootout"), League::ECHL,
                   kFortWayne, g));
  HCHECK_EQ(clock_text(g), std::string("SO"));
  HCHECK(game_read(live_clock("ht_game_echl_final_so.json", "4", "00:00:00", "", "SO"), League::ECHL, kFortWayne, g));
  HCHECK_EQ(clock_text(g), std::string("SO"));
  // The playoffs have no shootout: a fifth period is the second overtime
  const char *playoffs = "2027 Calder Cup Playoffs";
  HCHECK(game_read(live_clock("ht_game_ahl_final_ot.json", "5", "00:10:00", "", "2nd OT", playoffs), League::AHL,
                   kSyracuse, g));
  HCHECK(g.nhl.playoffs);
  HCHECK_EQ(clock_text(g), std::string("10:00 2OT"));
  std::string pf = slurp("ht_game_ahl_final_ot.json");
  pf.replace(pf.find("\"period\":\"4\""), 12, "\"period\":\"5\"");
  pf.replace(pf.find("2026-27 Regular Season"), 22, "2027 Calder Cup Playoffs");
  HCHECK(game_read(pf, League::AHL, kSyracuse, g));
  HCHECK_EQ(g.short_detail, std::string("Final/2OT"));
  // A code the feed hasn't used, with the game started: in progress
  std::string odd = live_clock("ht_game_ahl_final_ot.json", "1", "00:05:00");
  odd.replace(odd.find("\"status\":\"2\""), 12, "\"status\":\"5\"");
  HCHECK(game_read(odd, League::AHL, kSyracuse, g));
  HCHECK(g.state == GameState::IN);

  // Splashes between polls: ours, theirs, and the win
  GameSnapshot a, b, fin;
  const std::string base = live_clock("ht_game_ahl_final_ot.json", "3", "00:08:00");
  HCHECK(game_read(with_score(base, 1, 1), League::AHL, kSyracuse, a));
  HCHECK(game_read(with_score(base, 1, 2), League::AHL, kSyracuse, b));
  HCHECK_EQ(decide_splash(a, b, true, false).text, std::string("GOAL!"));
  HCHECK(game_read(with_score(base, 2, 1), League::AHL, kSyracuse, b));
  HCHECK_EQ(decide_splash(a, b, true, false).text, std::string("HFD GOAL"));
  HCHECK(decide_splash(a, b, false, false).text.empty());
  HCHECK(game_read(slurp("ht_game_ahl_final.json"), League::AHL, kLaval, fin));
  GameSnapshot lav;
  HCHECK(game_read(live_clock("ht_game_ahl_final.json", "3", "00:01:00"), League::AHL, kLaval, lav));
  HCHECK_EQ(decide_splash(lav, fin, true, false).text, std::string("LAV WINS!"));
}

void test_day() {
  // The AHL's Saturday: sixteen finals, four in overtime
  std::vector<LiveGame> live;
  std::vector<Final> finals;
  std::vector<Pending> pending;
  int64_t next = -1;
  HCHECK(day_read(slurp("ht_day_ahl_20261107.json"), live, &next, &finals, &pending));
  HCHECK(live.empty());
  HCHECK(pending.empty());
  HCHECK_EQ(next, (int64_t) 0);
  HCHECK_EQ(finals.size(), (size_t) 16);
  int ot = 0;
  for (const auto &f : finals) {
    HCHECK(f.detail == "Final" || f.detail == "Final/OT");
    ot += f.detail == "Final/OT";
    HCHECK(!f.away_abbr.empty() && !f.home_abbr.empty());
    HCHECK(!f.away_record.empty() && !f.home_record.empty());
    HCHECK_EQ((int) f.rank, 99);
  }
  HCHECK_EQ(ot, 4);
  if (finals.size() == 16) {
    // CLE @ LAV, the first listed
    HCHECK_EQ(finals[0].away_abbr, std::string("CLE"));
    HCHECK_EQ(finals[0].home_id, kLaval);
    HCHECK_EQ(finals[0].away_score, 2);
    HCHECK_EQ(finals[0].home_score, 4);
    HCHECK_EQ(finals[0].home_record, std::string("10-3-1"));
    HCHECK_EQ(finals[0].away_record, std::string("6-7-0"));
    HCHECK_EQ(finals[0].kickoff_epoch, at("2026-11-07T20:00:00Z"));
    HCHECK_EQ(finals[1].detail, std::string("Final/OT"));
    // TOR @ MB in Winnipeg at 6 PM (UTC-5 all year now); ONT @ ABB in
    // Abbotsford at 5 PM (UTC-7, as the team read has it: 1029265 at
    // 2026-11-07T17:00:00-07:00); CV @ TUC in Tucson at 7 PM
    HCHECK_EQ(finals[7].away_abbr, std::string("TOR"));
    HCHECK_EQ(finals[7].kickoff_epoch, at("2026-11-07T23:00:00Z"));
    HCHECK_EQ(finals[9].away_abbr, std::string("ONT"));
    HCHECK_EQ(finals[9].kickoff_epoch, at("2026-11-08T00:00:00Z"));
    HCHECK_EQ(finals[15].away_abbr, std::string("CV"));
    HCHECK_EQ(finals[15].kickoff_epoch, at("2026-11-08T02:00:00Z"));
    HCHECK_EQ(finals[15].detail, std::string("Final/OT"));
    HCHECK_EQ(finals[15].home_record, std::string("9-2-2"));
    HCHECK_EQ(finals[15].away_record, std::string("5-5-4"));  // 5-5-3-1: overtime and shootout losses together
    // The finals rotation's card: HockeyTech's logos, a hockey board
    Final f = finals[0];
    f.league = (uint8_t) League::AHL;
    GameSnapshot card = final_snapshot(f);
    HCHECK_EQ(card.team_logo, ht_logo_url(League::AHL, kCleveland));
    HCHECK_EQ(card.opp_logo, ht_logo_url(League::AHL, kLaval));
    HCHECK(card.sport == Sport::HOCKEY);
    // LV @ HAM: Hamilton is drawn as a badge
    for (const auto &h : finals)
      if (h.home_id == kHamilton) {
        Final hf = h;
        hf.league = (uint8_t) League::AHL;
        HCHECK(final_snapshot(hf).opp_logo.empty());
      }
    // The rotation keeps the whole day
    std::vector<RecapDay> days;
    std::vector<Final> stamped = finals;
    for (auto &x : stamped)
      x.league = (uint8_t) League::AHL;
    recap_absorb(days, stamped, {}, at("2026-11-08T05:00:00Z"));
    HCHECK_EQ(days.size(), (size_t) 1);
    if (!days.empty())
      HCHECK_EQ(days[0].finals.size(), (size_t) 16);
  }

  // The ECHL's: thirteen finals, three in overtime, Rapid City at Idaho
  // postponed
  finals.clear();
  live.clear();
  HCHECK(day_read(slurp("ht_day_echl_20261107.json"), live, &next, &finals, &pending));
  HCHECK_EQ(finals.size(), (size_t) 13);
  ot = 0;
  for (const auto &f : finals) {
    ot += f.detail == "Final/OT";
    HCHECK(f.away_id != kRapidCity);
  }
  HCHECK_EQ(ot, 3);
  HCHECK(live.empty());
  HCHECK_EQ(next, (int64_t) 0);

  // Tuesday's two games to come, the first at 10:30 AM
  finals.clear();
  HCHECK(day_read(slurp("ht_day_echl_20261110.json"), live, &next, &finals, &pending));
  HCHECK(finals.empty());
  HCHECK_EQ(pending.size(), (size_t) 2);
  HCHECK_EQ(next, at("2026-11-10T15:30:00Z"));
  if (pending.size() == 2) {
    HCHECK_EQ(pending[0].event_id, std::string("25623"));
    HCHECK_EQ(pending[0].away_id, kOrlando);
    HCHECK_EQ(pending[0].home_id, kGreenville);
    HCHECK_EQ(pending[0].home_abbr, std::string("GVL"));
    HCHECK_EQ(pending[0].away_record, std::string("2-5-1"));
    HCHECK_EQ(pending[0].home_record, std::string("7-1-2"));
    HCHECK_EQ(pending[1].kickoff_epoch, at("2026-11-10T23:30:00Z"));
    HCHECK(pending[0].tv.empty());
    Pending p = pending[0];
    p.league = (uint8_t) League::ECHL;
    GameSnapshot card = pending_snapshot(p);
    HCHECK(card.opp_logo.empty());  // Greenville: its badge
    HCHECK_EQ(card.team_logo, ht_logo_url(League::ECHL, kOrlando));
  }
  // The same day with the first game under way: one live, the next start
  // the other
  JsonDocument doc;
  deserializeJson(doc, slurp("ht_day_echl_20261110.json"));
  doc["SiteKit"]["Gamesbydate"][0]["status"] = "2";
  doc["SiteKit"]["Gamesbydate"][0]["started"] = "1";
  std::string on;
  serializeJson(doc, on);
  live.clear();
  pending.clear();
  HCHECK(day_read(on, live, &next, nullptr, &pending));
  HCHECK_EQ(live.size(), (size_t) 1);
  if (!live.empty()) {
    HCHECK_EQ(live[0].event_id, std::string("25623"));
    HCHECK_EQ(live[0].away_abbr, std::string("ORL"));
    HCHECK_EQ(live[0].home_id, kGreenville);
    HCHECK_EQ((int) live[0].league, 0);  // the caller stamps it
  }
  HCHECK_EQ(pending.size(), (size_t) 1);
  HCHECK_EQ(next, at("2026-11-10T23:30:00Z"));
  // A game with no time yet is no kickoff to wait for
  doc["SiteKit"]["Gamesbydate"][1]["time_tbd"] = "1";
  serializeJson(doc, on);
  HCHECK(day_read(on, live, &next));
  HCHECK_EQ(next, (int64_t) 0);
  // A shootout final, from the day's own field
  doc["SiteKit"]["Gamesbydate"][0]["status"] = "4";
  doc["SiteKit"]["Gamesbydate"][0]["shootout"] = "1";
  doc["SiteKit"]["Gamesbydate"][0]["period"] = "4";
  on.clear();
  serializeJson(doc, on);
  finals.clear();
  HCHECK(day_read(on, live, nullptr, &finals));
  HCHECK(finals.size() == 1 && finals[0].detail == "Final/SO");
  // A morning game in Winnipeg (the AHL's only one on 2027-03-04, 10:30 at
  // Manitoba, 15:30Z in the feed): the next start, and the Live games wait
  // after a scan at 14:00Z that found nothing on, end at puck drop
  JsonDocument mb;
  deserializeJson(mb, slurp("ht_day_echl_20261110.json"));
  mb["SiteKit"]["Gamesbydate"][0]["date_played"] = "2027-03-04";
  mb["SiteKit"]["Gamesbydate"][0]["timezone"] = "America/Winnipeg";
  mb["SiteKit"]["Gamesbydate"].remove(1);
  on.clear();
  serializeJson(mb, on);
  pending.clear();
  HCHECK(day_read(on, live, &next, nullptr, &pending));
  HCHECK_EQ(next, at("2027-03-04T15:30:00Z"));
  HCHECK(pending.size() == 1 && pending[0].kickoff_epoch == at("2027-03-04T15:30:00Z"));
  HCHECK_EQ(no_live_rescan_ms(at("2027-03-04T14:00:00Z"), next, 2u * 60000u, 180u * 60000u), 90u * 60000u);
  // Not a day document
  HCHECK(!day_read("Invalid key.", live));
  HCHECK(!day_read("{\"SiteKit\":{\"Scorebar\":[]}}", live));
}

void test_scan_backoff() {
  // HockeyTech's leagues alone in a scan (the AHL and the ECHL): one host,
  // so a failure is the scan's own, which back_off_ counts once against
  // HockeyTech.
  HostWait w;
  HCHECK_EQ(host_scan_result(w, true, true, false, true, 0, 0, 1000), 0u);
  HCHECK_EQ((int) w.run.streak, 0);
  // Beside ESPN's or MLB's leagues it is counted here.
  HCHECK(host_scan_result(w, true, true, true, false, 0, 0, 1000) > 0u);
  HCHECK_EQ((int) w.run.streak, 1);
  HCHECK(w.waiting(1000));
  HCHECK_EQ(host_scan_result(w, true, false, true, false, 0, 0, 1000), 0u);
  HCHECK_EQ((int) w.run.streak, 0);
}

// The idle display's scores, read outside the mode: they leave both of
// HockeyTech's leagues out while its wait runs, and their own failed read
// from it counts in that wait, alone or beside the other hosts' leagues.
void test_side_backoff() {
  std::vector<League> leagues = {League::NHL, League::AHL, League::MILB, League::ECHL};
  HCHECK(drop_source(leagues, Source::HOCKEYTECH));
  HCHECK_EQ(leagues.size(), (size_t) 2);
  HCHECK(leagues[0] == League::NHL && leagues[1] == League::MILB);
  HCHECK(!drop_source(leagues, Source::HOCKEYTECH));
  // MLB's wait leaves HockeyTech's leagues alone, and the other way round
  std::vector<League> both = {League::AHL, League::MILB};
  HCHECK(drop_source(both, Source::MLB_STATS));
  HCHECK(both.size() == 1 && both[0] == League::AHL);
  std::vector<League> alone = {League::ECHL, League::AHL};
  HCHECK(drop_source(alone, Source::HOCKEYTECH));
  HCHECK(alone.empty());  // start_side_job_ then waits for HockeyTech, reading nothing

  HostWait w;
  HCHECK(host_side_result(w, true, true, true, 10 * 60 * 1000, 0, 1000) >= 10u * 60 * 1000);
  HCHECK_EQ((int) w.run.streak, 1);
  HCHECK(w.waiting(2000));
  HCHECK_EQ(host_side_result(w, false, false, false, 0, 0, 3000), 0u);  // not asked
  HCHECK_EQ((int) w.run.streak, 1);
  HCHECK_EQ(host_side_result(w, true, false, false, 0, 0, 3000), 0u);  // answered
  HCHECK_EQ((int) w.run.streak, 0);
  HCHECK(!w.waiting(3000));
}

// The features that came after minor league hockey was built, with an AHL or
// ECHL game: big-play splashes, Follow this game, bets, My team today, ranks
// and the Standing setting.
void test_merged_features() {
  // Big plays: HockeyTech's scores say only that a goal went in, so every
  // goal is GOAL under hockey's one switch, as with the NHL.
  const std::string base = live_clock("ht_game_ahl_final_ot.json", "2", "00:12:00");
  GameSnapshot a, b;
  HCHECK(game_read(with_score(base, 1, 1), League::AHL, kSyracuse, a));
  HCHECK(game_read(with_score(base, 1, 2), League::AHL, kSyracuse, b));
  HCHECK(a.last_play.empty() && b.play_type.empty());
  const uint32_t goal = alert_bit(Alert::HOCKEY_GOAL);
  HCHECK_EQ(decide_splash(a, b, true, false, goal).text, std::string("GOAL!"));
  HCHECK(decide_splash(a, b, true, false, kAlertsAll & ~goal).text.empty());
  // The same read twice, or a read with nothing new: no splash
  HCHECK(decide_splash(b, b, true).text.empty());
  HCHECK(decide_splash(a, a, true).text.empty());
  // Two goals between reads are one splash; Live games names the side
  HCHECK(game_read(with_score(base, 3, 1), League::AHL, kSyracuse, b));
  HCHECK_EQ(decide_splash(a, b, true, true).text, std::string("HFD GOAL"));
  // A shootout: the feed adds the deciding goal to the winner's score, so it
  // splashes like any other goal when a read catches it before the final,
  // and the final after it is the win, once. Read straight from the
  // shootout, the final is the win alone.
  GameSnapshot so, so_won, fin;
  const std::string shootout = live_clock("ht_game_echl_final_so.json", "5", "00:00:00", "", "SO");
  HCHECK(game_read(with_score(shootout, 4, 4), League::ECHL, kBloomington, so));
  HCHECK(game_read(with_score(shootout, 5, 4), League::ECHL, kBloomington, so_won));
  HCHECK(game_read(slurp("ht_game_echl_final_so.json"), League::ECHL, kBloomington, fin));
  HCHECK_EQ(fin.team_score, 5);
  HCHECK_EQ(decide_splash(so, so_won, true).text, std::string("GOAL!"));
  HCHECK_EQ(decide_splash(so_won, fin, true).text, std::string("BLM WINS!"));
  HCHECK_EQ(decide_splash(so, fin, true).text, std::string("BLM WINS!"));

  // Follow this game: an AHL game Live games picked is held while it's on,
  // and let go at its final (Live games keeps no final); in Favorite teams
  // the final stays up for After the final first.
  FollowPin pin;
  const int64_t t0 = a.kickoff_epoch + 3600;
  HCHECK(!a.event_id.empty());
  HCHECK(follow_start(pin, true, true, a, t0));
  HCHECK_EQ(pin.event_id, std::string("1029277"));
  HCHECK(follow_holds(pin, b, t0 + 600, follow_after_final_s(false, 1800), 0));
  GameSnapshot done = game("ht_game_ahl_final_ot.json", League::AHL, kSyracuse);
  HCHECK(done.state == GameState::POST && done.event_id == a.event_id);
  HCHECK(!follow_holds(pin, done, t0 + 1200, follow_after_final_s(false, 1800), 0));
  HCHECK(!pin.on);
  HCHECK(follow_start(pin, true, true, a, t0));
  HCHECK(follow_holds(pin, done, t0 + 1200, follow_after_final_s(true, 1800), 0));
  HCHECK(!follow_holds(pin, done, t0 + 1200 + 1800, follow_after_final_s(true, 1800), 0));
  HCHECK(!follow_start(pin, true, true, done, t0));  // a final can't be followed
  // A game HockeyTech stops answering for lets go after half an hour
  HCHECK(follow_start(pin, true, true, a, t0));
  HCHECK(!follow_holds(pin, a, t0 + kFollowUnreadSeconds, 0, kFollowUnreadSeconds));

  // Bets: an AHL or ECHL team takes a bet like any other (HockeyTech's game
  // id is the game's), graded on HockeyTech's score.
  Bet bet{};
  bet.team_id = kLaval;
  bet.league = (uint8_t) League::AHL;
  bet.type = (uint8_t) BetType::MONEYLINE;
  bet.side = (uint8_t) BetSide::TEAM;
  HCHECK(bet_valid(bet));
  HCHECK_EQ(std::string(bet_team_abbr(bet.league, kLaval)), std::string("LAV"));
  Bet other = bet;
  other.league = (uint8_t) League::ECHL;
  other.team_id = kRapidCity;
  HCHECK(bet_valid(other));
  other.league = (uint8_t) League::AHL;  // Rapid City is no AHL team
  HCHECK(!bet_valid(other));
  GameSnapshot lav = game("ht_game_ahl_final.json", League::AHL, kLaval);  // CLE 2, LAV 4
  BetGame g;
  g.league = (uint8_t) League::AHL;
  g.event = bet_event_id(lav.event_id);
  HCHECK_EQ(g.event, 1029271u);
  g.team_id = lav.team_id;
  g.opp_id = lav.opp_id;
  g.team_score = lav.team_score;
  g.opp_score = lav.opp_score;
  g.kickoff = lav.kickoff_epoch;
  g.phase = BetPhase::FINAL;
  bet.placed = (uint32_t) (lav.kickoff_epoch - 3600);
  HCHECK(bet_observe(bet, g, lav.kickoff_epoch + 4 * 3600, lav.kickoff_epoch + 12 * 3600));
  HCHECK_EQ((int) bet.result, (int) BetResult::WON);
  HCHECK_EQ(bet_ticker(bet, bet_status(bet, &g, lav.kickoff_epoch + 4 * 3600)), std::string("LAV ML WON"));

  // No ranks and no standing: HockeyTech has no poll and no seed, so Seed or
  // Place leaves an AHL or ECHL team its record.
  HCHECK(lav.team_rank == 0 && lav.opp_rank == 0);
  Schedule s;
  const int64_t evening = at("2026-11-08T01:00:00Z");  // 8 PM in Syracuse on 11/7
  HCHECK_EQ(team_read(slurp("ht_team_ahl_syr.json"), kSyracuse, evening, s), 1);
  HCHECK(s.seed == 0 && s.place.empty());
  HCHECK(standing_text(Standing::SEED, s.seed, s.place).empty());
  HCHECK(standing_text(Standing::PLACE, s.seed, s.place).empty());

  // My team today after a restart: the team read names today's final too,
  // with the records the scorebar has now, which the game's clock lacks.
  Schedule last;
  std::string syr = slurp("ht_team_ahl_syr.json");
  HCHECK_EQ(parse_ht_team(syr, kSyracuse, evening, s, nullptr, 0, &last), 1);
  HCHECK_EQ(s.event_id, std::string("1029294"));  // the next game, as before
  HCHECK(last.valid);
  HCHECK_EQ(last.event_id, std::string("1029277"));
  HCHECK_EQ(last.kickoff_epoch, at("2026-11-07T22:00:00Z"));
  HCHECK(!last.time_tbd);
  HCHECK_EQ(last.opp_id, kHartford);
  HCHECK_EQ(last.team_record, std::string("5-5-2"));
  HCHECK(!last.opp_record.empty());
  const int64_t since = at("2026-11-07T05:00:00Z");  // midnight in Syracuse
  HCHECK(want_today_final(since, true, day_order(last.kickoff_epoch, last.time_tbd), 0));
  // kept already: not read again
  HCHECK(!want_today_final(since, true, day_order(last.kickoff_epoch, last.time_tbd),
                           day_order(last.kickoff_epoch, last.time_tbd)));
  // the next day it's yesterday's
  HCHECK(!want_today_final(since + 86400, true, day_order(last.kickoff_epoch, last.time_tbd), 0));
  GameSnapshot today = game("ht_game_ahl_final_ot.json", League::AHL, kSyracuse);
  ht_fill_from_schedule(today, last);
  HCHECK_EQ(today.team_record, std::string("5-5-2"));
  HCHECK_EQ(today.opp_record, last.opp_record);
  HCHECK_EQ(espn::detail::records_line(today, standing_text(Standing::SEED, s.seed, s.place)),
            "SYR 5-5-2 | HFD " + last.opp_record);
  LocalDay day{since, since + 86400};
  std::vector<TodayGame> games = {TodayGame{true, GameState::POST, today.kickoff_epoch, false}};
  HCHECK_EQ(today_pick(games, day), 0);
  // A postponed game is no final to keep: Rapid City has none
  std::string rc = slurp("ht_team_echl_rc.json");
  HCHECK_EQ(parse_ht_team(rc, kRapidCity, evening, s, nullptr, 0, &last), 1);
  HCHECK(!last.valid);
  // With the list of finals not asked for, or the Up next list, the read is as before
  HCHECK_EQ(team_read(syr, kSyracuse, evening, s), 1);
  HCHECK_EQ(s.event_id, std::string("1029294"));
  std::vector<Upcoming> up;
  HCHECK_EQ(parse_ht_team(syr, kSyracuse, evening, s, &up, 4, &last), 1);
  HCHECK(up.size() == 4 && last.event_id == "1029277");
}

void test_health() {
  Health h{};
  h.start = Restart::RESTART;
  h.version = "x";
  h.channel = "stable";
  h.espn_status = 200;
  h.espn_ago = 9;
  char buf[kHealthJsonSize];
  health_json(buf, sizeof(buf), h);
  JsonDocument doc;
  HCHECK(!deserializeJson(doc, buf));
  HCHECK(doc["ht"].isNull());  // no AHL or ECHL read yet
  h.ht_status = 403;
  h.ht_ago = 12;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  HCHECK(!deserializeJson(doc, buf));
  HCHECK_EQ(doc["ht"].as<int>(), 403);
  HCHECK_EQ(doc["ht_ago"].as<int>(), 12);
  HCHECK(doc["mlb"].isNull());
  // All three hosts, with the longest numbers and a fault: it still fits
  h.mlb_status = 200;
  h.mlb_ago = 4000000000LL;
  h.espn_ago = 4000000000LL;
  h.ht_ago = 4000000000LL;
  h.uptime_s = 4000000000LL;
  h.fault = Restart::BROWNOUT;
  h.fault_ago = 4000000000LL;
  h.wifi = true;
  h.rssi = -100;
  h.heap_kb = 999999.9f;
  h.block_kb = 999999.9f;
  h.psram_kb = 999999.9f;
  h.version = "2026.10.2-beta.1";
  h.built = 4000000000LL;
  h.channel = "bar";
  int n = health_json(buf, sizeof(buf), h);
  HCHECK(n > 0 && (size_t) n < sizeof(buf));
  doc.clear();
  HCHECK(!deserializeJson(doc, buf));
  HCHECK_EQ(doc["mlb"].as<int>(), 200);
  HCHECK_EQ(doc["ht"].as<int>(), 403);
}

}  // namespace

int run_hockeytech_tests(int &total_checks) {
  test_urls();
  test_league();
  test_times();
  test_states();
  test_team();
  test_game_final();
  test_game_postponed();
  test_game_pre();
  test_game_live();
  test_day();
  test_scan_backoff();
  test_side_backoff();
  test_merged_features();
  test_health();
  total_checks += checks;
  return failures;
}
