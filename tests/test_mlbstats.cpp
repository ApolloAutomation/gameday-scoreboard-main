// Minor league baseball from the MLB Stats API (components/gameday/
// source_mlbstats.h): the URLs, every parse against the recorded documents in
// fixtures/mlbstats_*.json, and how the results go through the shared
// baseball text, splashes, Live games and the finals rotation.
#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "health.h"
#include "offseason.h"
#include "poll_timing.h"
#include "recap.h"
#include "set_request.h"
#include "source_mlbstats.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define MCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

#define MCHECK_EQ(a, b)                                                                \
  do {                                                                                 \
    checks++;                                                                          \
    auto va = (a);                                                                     \
    auto vb = (b);                                                                     \
    if (!(va == vb)) {                                                                 \
      failures++;                                                                      \
      std::ostringstream os;                                                           \
      os << va << " != " << vb;                                                        \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str());        \
    }                                                                                  \
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
  return parse_mlb_team(json, our_id, now, out, up, max);
}
bool game_read(std::string json, uint32_t our_id, int64_t near, GameSnapshot &out) {
  return parse_mlb_game(json, our_id, near, out);
}
bool day_read(std::string json, std::vector<LiveGame> &out, int64_t *next = nullptr,
              std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  return parse_mlb_day(json, out, next, finals, pending);
}

// Recorded documents (fixtures/README.md). Teams by MLB Stats id.
const uint32_t kDurham = 234, kScranton = 531, kMemphis = 235, kRoundRock = 102, kReno = 2310, kJacksonville = 564;
const uint32_t kLicey = 672, kEscogido = 671, kAguilas = 667, kGigantes = 670, kMargarita = 697, kOriente = 694;
const uint32_t kMochis = 675, kMexicali = 673, kHermosillo = 677;

GameSnapshot game(const std::string &file, uint32_t our_id, int64_t near = 0) {
  std::string json = slurp(file);
  GameSnapshot g;
  bool ok = game_read(json, our_id, near, g);
  MCHECK(ok);
  return g;
}

// A recorded team read with every game from `from` on set back to not
// started: the same document as read on an earlier morning. The API keeps
// each date's entry, so this is what it listed then, scores aside.
std::string reopened(const std::string &file, const char *from) {
  JsonDocument doc;
  deserializeJson(doc, slurp(file));
  int64_t cut = at(from);
  for (JsonObject d : doc["dates"].as<JsonArray>())
    for (JsonObject g : d["games"].as<JsonArray>())
      if (at(g["gameDate"] | "") >= cut)
        g["status"]["codedGameState"] = "S";
  std::string out;
  serializeJson(doc, out);
  return out;
}

void test_urls() {
  const int64_t now = at("2026-11-08T23:00:00Z");
  // The same reads as the recorded team, far and game documents.
  MCHECK_EQ(mlb_team_url(kDurham, now),
            std::string("https://statsapi.mlb.com/api/v1/schedule?sportId=11,12,13,14&teamId=234"
                        "&startDate=2026-11-07&endDate=2026-11-18&hydrate=team&fields=dates,games,gamePk,gameDate,"
                        "status,codedGameState,startTimeTBD,teams,away,home,team,id,abbreviation,teamName,"
                        "leagueRecord,wins,losses,gameNumber"));
  MCHECK_EQ(mlb_far_url(kDurham, kMilbTripleA, now),
            std::string("https://statsapi.mlb.com/api/v1/schedule?sportId=11&teamId=234&startDate=2026-11-19"
                        "&endDate=2027-07-06&fields=dates,games,gamePk,gameDate,status,codedGameState,startTimeTBD"));
  MCHECK_EQ(mlb_game_url("816688"),
            std::string("https://statsapi.mlb.com/api/v1/schedule?gamePk=816688&hydrate=linescore,team,"
                        "probablePitcher&fields=dates,games,gamePk,gameDate,status,codedGameState,statusCode,"
                        "detailedState,reason,startTimeTBD,teams,away,home,team,id,abbreviation,score,leagueRecord,"
                        "wins,losses,isWinner,probablePitcher,fullName,linescore,currentInning,inningState,"
                        "scheduledInnings,balls,strikes,outs,offense,defense,batter,pitcher,first,second,third,runs,"
                        "hits,errors,venue,name"));
  MCHECK_EQ(mlb_day_url(at("2026-08-14T23:00:00Z")),
            std::string("https://statsapi.mlb.com/api/v1/schedule?sportId=11,12,13,14&date=2026-08-14"
                        "&hydrate=linescore,team&fields=dates,games,gamePk,gameDate,status,codedGameState,"
                        "startTimeTBD,teams,away,home,team,id,abbreviation,score,leagueRecord,wins,losses,linescore,"
                        "currentInning,scheduledInnings"));
  // The day turns over at 4 AM Eastern (9 AM UTC), like the ESPN scans: a
  // West Coast game after midnight Eastern is still the day it started.
  MCHECK(mlb_day_url(at("2026-08-15T08:59:00Z")).find("date=2026-08-14&") != std::string::npos);
  MCHECK(mlb_day_url(at("2026-08-15T09:00:00Z")).find("date=2026-08-15&") != std::string::npos);
  // Across a month and a year end
  MCHECK(mlb_team_url(kDurham, at("2026-12-31T23:00:00Z")).find("startDate=2026-12-30&endDate=2027-01-10&") !=
         std::string::npos);
  // A far read asks for one level: the team's, and Triple-A for anything else.
  MCHECK(mlb_far_url(kDurham, kMilbSingleA, now).find("sportId=14&") != std::string::npos);
  MCHECK(mlb_far_url(kDurham, 0, now).find("sportId=11&") != std::string::npos);
  MCHECK(mlb_far_url(kDurham, 99, now).find("sportId=11&") != std::string::npos);
  // Logos: the 64 px PNG spot, the same URL from the team table and the game.
  MCHECK_EQ(mlb_logo_url(kDurham), std::string("https://midfield.mlbstatic.com/v1/team/234/spots/64"));
  MCHECK_EQ(team_logo_url(League::MILB, kDurham, "DUR"), mlb_logo_url(kDurham));
  // Only the MLB Stats API's reads are counted on MLB's side.
  MCHECK(is_mlb_url(mlb_game_url("1")));
  MCHECK(!is_mlb_url("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard"));
  MCHECK(!is_mlb_url("https://midfield.mlbstatic.com/v1/team/234/spots/64"));
  MCHECK(!is_mlb_url(""));
}

void test_league() {
  const LeagueInfo *l = league_by_key("milb");
  MCHECK(l != nullptr);
  MCHECK(l->league == League::MILB);
  MCHECK(l->sport == Sport::BASEBALL);
  MCHECK(l->source == Source::MLB_STATS);
  MCHECK(l->per_event && l->season_list && l->logo_by_id);
  MCHECK_EQ((int) l->linger_min, 10);
  MCHECK(is_mlb_stats(League::MILB));
  MCHECK(!is_mlb_stats(League::MLB));
  MCHECK(league_source(League::NFL) == Source::ESPN);
  MCHECK(!is_leaderboard(League::MILB));
  MCHECK_EQ(std::string(league_info(League::MILB)->prefix), std::string("MiLB"));
  MCHECK_EQ(std::string(recap_league_word((uint8_t) League::MILB)), std::string("MiLB"));
  // The live mask and the finals rotation still have a bit for it.
  MCHECK((int) League::MILB < 16);
  MCHECK_EQ(recap_bit((uint8_t) League::MILB), (uint16_t) (1u << 13));
  MCHECK_EQ(live_mask_from_keys("milb,mlb"), (uint16_t) ((1u << 13) | (1u << 4)));
  std::vector<League> live = live_leagues(1u << 13, 0, {});
  MCHECK(live.size() == 1 && live[0] == League::MILB);
  // Auto: the saved team's league, a minor league one too.
  live = live_leagues(0, (uint8_t) League::MILB, {(uint8_t) League::NHL});
  MCHECK(live.size() == 2 && live[0] == League::NHL && live[1] == League::MILB);
  // A minor league team is a team like any other: picked, a favorite.
  SetOp op;
  MCHECK(parse_set("team", "milb:234", op, 16));
  MCHECK(op.team.league == (uint8_t) League::MILB && op.team.id == kDurham);
  MCHECK(parse_set("favs", "milb:234,nfl:6", op, 16));
  MCHECK_EQ(op.teams.size(), (size_t) 2);
  MCHECK(!parse_set("team", "milb:999999", op, 16));
  // The four levels, 30 teams each, every one with its level as the group.
  int levels[4] = {0, 0, 0, 0};
  for (const auto &t : kTeams) {
    if (t.league != League::MILB)
      continue;
    MCHECK(t.group >= kMilbTripleA && t.group <= kMilbSingleA);
    if (t.group >= kMilbTripleA && t.group <= kMilbSingleA)
      levels[t.group - kMilbTripleA]++;
    MCHECK(t.abbr[0] != '\0');
  }
  for (int n : levels)
    MCHECK_EQ(n, 30);
  MCHECK_EQ(std::string(milb_level_name(kMilbTripleA)), std::string("Triple-A"));
  MCHECK_EQ(std::string(milb_level_name(kMilbDoubleA)), std::string("Double-A"));
  MCHECK_EQ(std::string(milb_level_name(kMilbHighA)), std::string("High-A"));
  MCHECK_EQ(std::string(milb_level_name(kMilbSingleA)), std::string("Single-A"));
  MCHECK_EQ(std::string(milb_level_name(1)), std::string(""));
}

void test_words() {
  MCHECK_EQ(mlb_short_name("Justyn-Henry Malloy"), std::string("J. Malloy"));
  MCHECK_EQ(mlb_short_name("Homer Bush Jr."), std::string("H. Bush Jr."));
  MCHECK_EQ(mlb_short_name("Jasson Dom\xc3\xadnguez"), std::string("J. Dominguez"));  // accents dropped
  MCHECK_EQ(mlb_short_name("Ichiro"), std::string("Ichiro"));
  MCHECK_EQ(mlb_short_name("  Pete   Hansen "), std::string("P. Hansen "));
  MCHECK_EQ(mlb_short_name(""), std::string(""));
  MCHECK_EQ(mlb_short_name("   "), std::string(""));
  MCHECK_EQ(mlb_short_name("Solo "), std::string("Solo"));
  MCHECK_EQ(mlb_ordinal(1), std::string("1st"));
  MCHECK_EQ(mlb_ordinal(2), std::string("2nd"));
  MCHECK_EQ(mlb_ordinal(3), std::string("3rd"));
  MCHECK_EQ(mlb_ordinal(4), std::string("4th"));
  MCHECK_EQ(mlb_ordinal(11), std::string("11th"));
  MCHECK_EQ(mlb_ordinal(12), std::string("12th"));
  MCHECK_EQ(mlb_ordinal(13), std::string("13th"));
  MCHECK_EQ(mlb_ordinal(21), std::string("21st"));
  MCHECK_EQ(mlb_ordinal(22), std::string("22nd"));
  MCHECK(mlb_state("S") == MlbState::PRE);
  MCHECK(mlb_state("P") == MlbState::PRE);
  MCHECK(mlb_state("") == MlbState::PRE);
  MCHECK(mlb_state("X") == MlbState::PRE);
  MCHECK(mlb_state("I") == MlbState::LIVE);
  MCHECK(mlb_state("M") == MlbState::LIVE);
  MCHECK(mlb_state("N") == MlbState::LIVE);
  MCHECK(mlb_state("F") == MlbState::FINAL);
  MCHECK(mlb_state("O") == MlbState::FINAL);
  MCHECK(mlb_state("Q") == MlbState::FINAL);
  MCHECK(mlb_state("D") == MlbState::OFF);
  MCHECK(mlb_state("C") == MlbState::OFF);
  MCHECK(mlb_state("T") == MlbState::OFF);
  MCHECK(mlb_state("U") == MlbState::OFF);
  MCHECK_EQ(no_game_text(true, false, false, 1, false, Source::MLB_STATS), std::string("Can't reach MLB"));
  MCHECK_EQ(no_game_text(true, false, false, 1, false, Source::ESPN), std::string("Can't reach ESPN"));
  MCHECK_EQ(no_game_text(true, false, false, 1, true, Source::MLB_STATS), std::string("Wi-Fi needs a sign-in"));
}

void test_game_final() {
  // MEM @ DUR, 2026-08-14, Final 12-2, from Durham's side.
  GameSnapshot g = game("mlbstats_game_final.json", kDurham, at("2026-08-14T22:45:00Z"));
  MCHECK(g.valid);
  MCHECK(g.sport == Sport::BASEBALL);
  MCHECK_EQ(g.event_id, std::string("816688"));
  MCHECK_EQ(g.kickoff_epoch, at("2026-08-14T22:45:00Z"));
  MCHECK(!g.time_tbd);
  MCHECK(g.state == GameState::POST);
  MCHECK(g.completed);
  MCHECK_EQ(g.short_detail, std::string("Final"));
  MCHECK_EQ(g.team_abbr, std::string("DUR"));
  MCHECK_EQ(g.opp_abbr, std::string("MEM"));
  MCHECK_EQ(g.team_id, kDurham);
  MCHECK_EQ(g.opp_id, kMemphis);
  MCHECK_EQ(g.team_score, 2);
  MCHECK_EQ(g.opp_score, 12);
  MCHECK_EQ(g.team_record, std::string("53-64"));
  MCHECK_EQ(g.opp_record, std::string("66-52"));
  MCHECK(g.team_home);
  MCHECK(!g.team_winner);
  MCHECK_EQ(g.team_logo, mlb_logo_url(kDurham));
  MCHECK_EQ(g.opp_logo, mlb_logo_url(kMemphis));
  MCHECK_EQ(g.venue, std::string("Durham Bulls Athletic Park"));
  MCHECK_EQ(g.period, 9);
  MCHECK_EQ(g.mlb.team_hits, 4);
  MCHECK_EQ(g.mlb.opp_hits, 13);
  MCHECK_EQ(g.mlb.team_errors, 2);
  MCHECK_EQ(g.mlb.opp_errors, 0);
  MCHECK_EQ(g.mlb.team_probable, std::string("L. Workman"));
  MCHECK_EQ(g.mlb.opp_probable, std::string("M. Molina"));
  // A final keeps the last count and runners in the line score: not shown.
  MCHECK_EQ((int) g.mlb.balls, -1);
  MCHECK_EQ((int) g.mlb.outs, -1);
  MCHECK_EQ((int) g.mlb.bases, 0);
  MCHECK(g.mlb.half == Half::NONE);
  MCHECK_EQ(g.possession, 0);
  TickerOptions o;
  MCHECK_EQ(status_text(g, o, ""), std::string("Final | DUR 4 H 2 E, MEM 13 H 0 E | DUR 53-64 | MEM 66-52"));
  // From the other side
  GameSnapshot m = game("mlbstats_game_final.json", kMemphis);
  MCHECK_EQ(m.team_abbr, std::string("MEM"));
  MCHECK(m.team_winner);
  MCHECK(!m.team_home);
  // A team that isn't in the game: no snapshot
  GameSnapshot none;
  MCHECK(!game_read(slurp("mlbstats_game_final.json"), kScranton, 0, none));
  MCHECK(!none.valid);

  // Extra innings: RNO @ RR, Final/11 9-8
  GameSnapshot x = game("mlbstats_game_final_extra.json", kRoundRock);
  MCHECK_EQ(x.short_detail, std::string("Final/11"));
  MCHECK_EQ(x.team_score, 9);
  MCHECK_EQ(x.opp_score, 8);
  MCHECK(x.team_winner);
  MCHECK_EQ(x.opp_id, kReno);
  MCHECK_EQ(status_text(x, o, "").substr(0, 9), std::string("Final/11 "));
  // Called in the 7th of 9 for rain (statusCode FR): Final/7
  GameSnapshot r = game("mlbstats_game_final_short.json", kJacksonville);
  MCHECK_EQ(r.short_detail, std::string("Final/7"));
  MCHECK(r.completed);
  MCHECK_EQ(r.team_score, 10);
  MCHECK_EQ(r.opp_score, 1);
  MCHECK_EQ(r.mlb.team_probable, std::string("Z. Brzykcy"));
  MCHECK_EQ(r.mlb.opp_probable, std::string(""));
}

void test_game_postponed() {
  // DUR @ SWB 815849: postponed 4/7 (cold), set for 4/10 as game 2 of a
  // doubleheader and postponed again, played 4/11 as game 2. The game read
  // lists all three; the one nearest the start the board picked is read.
  const std::string json = slurp("mlbstats_game_postponed.json");
  GameSnapshot g;
  MCHECK(game_read(json, kDurham, at("2026-04-07T22:35:00Z"), g));
  MCHECK(g.state == GameState::POST);
  MCHECK(!g.completed);
  MCHECK_EQ(g.short_detail, std::string("Postponed"));
  MCHECK_EQ(g.kickoff_epoch, at("2026-04-07T22:35:00Z"));
  MCHECK_EQ(g.opp_abbr, std::string("SWB"));
  MCHECK_EQ(g.opp_id, kScranton);
  MCHECK(!g.team_home);
  MCHECK_EQ(g.mlb.team_probable, std::string("J. Scholtens"));
  MCHECK_EQ(g.mlb.opp_probable, std::string("B. Beck"));
  TickerOptions o;
  MCHECK_EQ(status_text(g, o, ""), std::string("Postponed"));
  // No splash for a game that never finished
  GameSnapshot before = g;
  before.state = GameState::IN;
  MCHECK(decide_splash(before, g, true).text.empty());

  // Game 2 on 4/10, no start time (after game 1): midnight Eastern of its day.
  MCHECK(game_read(json, kDurham, at("2026-04-10T05:00:00Z"), g));
  MCHECK_EQ(g.short_detail, std::string("Postponed"));
  MCHECK(g.time_tbd);
  MCHECK_EQ(g.kickoff_epoch, at("2026-04-10T05:00:00Z"));
  MCHECK_EQ(g.mlb.team_probable, std::string("J. Cuevas"));

  // Played 4/11: Final, seven innings of seven.
  MCHECK(game_read(json, kDurham, at("2026-04-11T05:00:00Z"), g));
  MCHECK(g.state == GameState::POST);
  MCHECK(g.completed);
  MCHECK_EQ(g.short_detail, std::string("Final"));
  MCHECK_EQ(g.team_score, 5);
  MCHECK_EQ(g.opp_score, 9);
  MCHECK(!g.team_winner);
  MCHECK(g.time_tbd);
  MCHECK_EQ(g.kickoff_epoch, at("2026-04-11T05:00:00Z"));
}

void test_game_pre() {
  // MXC @ MOC (Mexican Pacific League), Pre-Game: the line score is already
  // there with an empty count, which doesn't show before the first pitch.
  GameSnapshot g = game("mlbstats_game_pregame.json", kMochis, at("2026-11-09T00:00:00Z"));
  MCHECK(g.state == GameState::PRE);
  MCHECK(!g.completed);
  MCHECK_EQ(g.short_detail, std::string("Scheduled"));
  MCHECK_EQ(g.kickoff_epoch, at("2026-11-09T00:00:00Z"));
  MCHECK(g.team_home);
  MCHECK_EQ(g.team_abbr, std::string("MOC"));
  MCHECK_EQ(g.opp_abbr, std::string("MXC"));
  MCHECK_EQ(g.opp_id, kMexicali);
  MCHECK_EQ(g.team_record, std::string("9-12"));
  MCHECK_EQ(g.opp_record, std::string("12-10"));
  MCHECK_EQ((int) g.mlb.balls, -1);
  MCHECK_EQ((int) g.mlb.outs, -1);
  MCHECK_EQ(g.possession, 0);
  MCHECK(g.mlb.batter.empty());
  TickerOptions o;
  MCHECK_EQ(status_text(g, o, "Sun 6:00 PM"), std::string("Sun 6:00 PM | Chevron Park"));
  // Scheduled, no line score yet: no scores, hits or errors
  GameSnapshot s = game("mlbstats_game_scheduled.json", kHermosillo);
  MCHECK(s.state == GameState::PRE);
  MCHECK_EQ(s.team_score, 0);
  MCHECK_EQ(s.opp_score, 0);
  MCHECK_EQ(s.mlb.team_hits, 0);
  MCHECK_EQ(s.team_record, std::string("11-10"));
  MCHECK_EQ(s.opp_record, std::string("17-5"));
  MCHECK_EQ(s.venue, std::string("Kuroda Park"));
  MCHECK_EQ(s.period, 0);
  // The poll before it: every minute in its last hour, 15 minutes before.
  MCHECK(pre_poll_near(3600, false, true));
  MCHECK(!pre_poll_near(3601, false, true));
}

void test_game_live() {
  // LIC @ ESC (Dominican league), 2026-11-08, read 23:09 UTC: Top 6th, 2-0,
  // two out, a runner on third. Winter league games stand in for minor
  // league ones (the same API and document); minor league baseball was
  // between seasons when these were recorded.
  GameSnapshot top = game("mlbstats_game_top.json", kLicey);
  MCHECK(top.state == GameState::IN);
  MCHECK_EQ(top.short_detail, std::string("Top 6th"));
  MCHECK(top.mlb.half == Half::TOP);
  MCHECK_EQ(top.period, 6);
  MCHECK_EQ((int) top.mlb.balls, 2);
  MCHECK_EQ((int) top.mlb.strikes, 0);
  MCHECK_EQ((int) top.mlb.outs, 2);
  MCHECK_EQ((int) top.mlb.bases, (int) Baseball::THIRD);
  MCHECK_EQ(top.mlb.batter, std::string("A. Ortiz"));
  MCHECK_EQ(top.mlb.pitcher, std::string("J. Garcia"));
  MCHECK_EQ(top.possession, 1);  // the visitors bat in the top half
  MCHECK_EQ(top.team_score, 4);
  MCHECK_EQ(top.opp_score, 2);
  MCHECK_EQ(top.mlb.team_hits, 5);
  MCHECK_EQ(top.mlb.opp_hits, 3);
  MCHECK_EQ(top.mlb.team_errors, 1);
  MCHECK_EQ(baseball_count(top), std::string("2-0"));
  TickerOptions o;
  MCHECK_EQ(status_text(top, o, ""), std::string("2-0, 2 outs | P J. Garcia, AB A. Ortiz | Top 6th"));
  MCHECK_EQ(clock_text(top), std::string("Top 6th"));
  o.baseball_count = false;
  MCHECK_EQ(status_text(top, o, ""), std::string("Top 6th"));
  GameSnapshot esc = game("mlbstats_game_top.json", kEscogido);
  MCHECK_EQ(esc.possession, 2);
  MCHECK(esc.team_home);

  // The next read, Middle 6th: LIC scored a run in between.
  GameSnapshot mid = game("mlbstats_game_middle.json", kLicey);
  MCHECK_EQ(mid.short_detail, std::string("Mid 6th"));
  MCHECK(mid.mlb.half == Half::MID);
  MCHECK_EQ((int) mid.mlb.balls, -1);  // the last count stays in the line score
  MCHECK_EQ((int) mid.mlb.outs, -1);
  MCHECK_EQ((int) mid.mlb.bases, 0);
  MCHECK_EQ(mid.possession, 0);
  MCHECK(mid.mlb.batter.empty());
  MCHECK_EQ(mid.team_score, 5);
  o = TickerOptions{};
  MCHECK_EQ(status_text(mid, o, ""), std::string("LIC 6 H 1 E, ESC 3 H 0 E"));
  Splash run = decide_splash(top, mid, true);
  MCHECK_EQ(run.text, std::string("RUN!"));
  MCHECK_EQ(run.color, 0xFFFFFFu);  // no team colors from the MLB Stats API: white
  GameSnapshot esc_mid = game("mlbstats_game_middle.json", kEscogido);
  MCHECK_EQ(decide_splash(esc, esc_mid, true).text, std::string("LIC SCORES"));
  MCHECK(decide_splash(esc, esc_mid, false).text.empty());
  // Live games: nobody is ours, the visitors are the snapshot's team
  MCHECK_EQ(decide_splash(top, mid, true, true).text, std::string("LIC SCORES"));

  // MAR @ ORI, Bot 9th, 2-2, none out: the home side bats.
  GameSnapshot bot = game("mlbstats_game_bottom.json", kOriente);
  MCHECK_EQ(bot.short_detail, std::string("Bot 9th"));
  MCHECK(bot.mlb.half == Half::BOTTOM);
  MCHECK_EQ((int) bot.mlb.balls, 2);
  MCHECK_EQ((int) bot.mlb.strikes, 2);
  MCHECK_EQ((int) bot.mlb.outs, 0);
  MCHECK_EQ((int) bot.mlb.bases, 0);
  MCHECK_EQ(bot.possession, 1);
  MCHECK_EQ(bot.mlb.batter, std::string("R. Cuadrado"));
  MCHECK_EQ(bot.opp_id, kMargarita);
  MCHECK_EQ(status_text(bot, o, ""), std::string("2-2, 0 outs | P A. Hernandez, AB R. Cuadrado | Bot 9th"));

  // The same game a few reads on: End 9th, MAR 10-6, then Game Over
  // (codedGameState O): the win splash, from MAR's side and in Live games.
  GameSnapshot end = game("mlbstats_game_end.json", kMargarita);
  MCHECK_EQ(end.short_detail, std::string("End 9th"));
  MCHECK(end.mlb.half == Half::END);
  MCHECK_EQ((int) end.mlb.balls, -1);
  MCHECK_EQ(end.opp_id, kOriente);
  GameSnapshot over = game("mlbstats_game_over.json", kMargarita);
  MCHECK(over.state == GameState::POST);
  MCHECK(over.completed);
  MCHECK_EQ(over.short_detail, std::string("Final"));
  MCHECK(over.team_winner);
  MCHECK_EQ(over.team_record, std::string("11-11"));
  Splash win = decide_splash(end, over, true);
  MCHECK_EQ(win.text, std::string("MAR WINS!"));
  MCHECK_EQ(decide_splash(end, over, true, true).text, std::string("MAR WINS!"));
  GameSnapshot ori_end = game("mlbstats_game_end.json", kOriente);
  GameSnapshot ori_over = game("mlbstats_game_over.json", kOriente);
  MCHECK(decide_splash(ori_end, ori_over, true).text.empty());  // our side lost
  MCHECK_EQ(status_text(over, o, ""), std::string("Final | MAR 12 H 3 E, ORI 7 H 0 E | MAR 11-11 | ORI 15-6"));

  // LIC @ ESC, Bot 6th, bases loaded after a walk (the count still 4-2)
  GameSnapshot loaded = game("mlbstats_game_loaded.json", kEscogido);
  MCHECK_EQ(loaded.short_detail, std::string("Bot 6th"));
  MCHECK_EQ((int) loaded.mlb.bases, (int) (Baseball::FIRST | Baseball::SECOND | Baseball::THIRD));
  MCHECK_EQ((int) loaded.mlb.balls, 4);
  MCHECK_EQ((int) loaded.mlb.outs, 2);
  MCHECK_EQ(loaded.possession, 1);
  MCHECK_EQ(loaded.mlb.batter, std::string("Y. Navarro"));
  MCHECK_EQ(loaded.opp_id, kLicey);

  // AGU @ GIG, rain delay in the Top 6th (statusCode IR) with runners on
  // second and third: the clock row says so, the count and runners wait.
  GameSnapshot d = game("mlbstats_game_delay.json", kGigantes);
  MCHECK(d.state == GameState::IN);
  MCHECK_EQ(d.short_detail, std::string("Rain Delay"));
  MCHECK(d.mlb.half == Half::NONE);
  MCHECK_EQ((int) d.mlb.balls, -1);
  MCHECK_EQ((int) d.mlb.bases, 0);
  MCHECK_EQ(d.possession, 0);
  MCHECK_EQ(d.team_score, 3);
  MCHECK_EQ(d.opp_score, 0);
  MCHECK_EQ(d.opp_id, kAguilas);
  MCHECK_EQ(status_text(d, o, ""), std::string("GIG 4 H 1 E, AGU 1 H 0 E"));
  MCHECK_EQ(clock_text(d), std::string("Rain Delay"));

  // Other delays and replay reviews, from the same document
  auto with_status = [](const std::string &code, const std::string &reason) {
    JsonDocument doc;
    deserializeJson(doc, slurp("mlbstats_game_delay.json"));
    JsonObject st = doc["dates"][0]["games"][0]["status"];
    st["statusCode"] = code;
    st["codedGameState"] = code.substr(0, 1);
    st["reason"] = reason;
    std::string json;
    serializeJson(doc, json);
    GameSnapshot g;
    game_read(json, kGigantes, 0, g);
    return g;
  };
  MCHECK_EQ(with_status("IL", "Lightning").short_detail, std::string("Lightning Delay"));
  MCHECK_EQ(with_status("II", "Inclement Weather").short_detail, std::string("Delayed"));
  MCHECK_EQ(with_status("IZ", "About to Resume").short_detail, std::string("Delayed"));
  GameSnapshot review = with_status("IH", "Review");  // played through: the half and count show
  MCHECK_EQ(review.short_detail, std::string("Top 6th"));
  MCHECK_EQ((int) review.mlb.bases, (int) (Baseball::SECOND | Baseball::THIRD));
  MCHECK_EQ((int) review.mlb.outs, 1);
  MCHECK_EQ(with_status("MF", "Close play at 1st").short_detail, std::string("Top 6th"));
  MCHECK_EQ(with_status("NH", "Home run").short_detail, std::string("Top 6th"));
  GameSnapshot warm = with_status("PW", "");
  MCHECK(warm.state == GameState::PRE);
  MCHECK_EQ(warm.short_detail, std::string("Warmup"));
  MCHECK_EQ(with_status("PR", "Rain").short_detail, std::string("Delayed"));
  GameSnapshot sus = with_status("TR", "Rain");
  MCHECK(sus.state == GameState::POST && !sus.completed);
  MCHECK_EQ(sus.short_detail, std::string("Suspended"));
  MCHECK_EQ(with_status("CR", "Rain").short_detail, std::string("Canceled"));
  MCHECK_EQ(with_status("UO", "").short_detail, std::string("Suspended"));
}

void test_game_documents() {
  GameSnapshot g;
  MCHECK(!game_read(std::string("{"), kDurham, 0, g));
  MCHECK(!game_read(std::string("[]"), kDurham, 0, g));
  MCHECK(!game_read(std::string("{\"dates\":5}"), kDurham, 0, g));
  MCHECK(!game_read(std::string("{\"dates\":[]}"), kDurham, 0, g));
  MCHECK(!game_read(std::string("{\"dates\":[{\"games\":[{\"gamePk\":0}]}]}"), kDurham, 0, g));
  // A game with a team but nothing else: a pre-game card with no numbers
  MCHECK(game_read(std::string("{\"dates\":[{\"games\":[{\"gamePk\":7,\"teams\":{\"home\":{\"team\":{\"id\":234}}}}]}]}"),
                        kDurham, 0, g));
  MCHECK(g.state == GameState::PRE);
  MCHECK_EQ(g.event_id, std::string("7"));
  MCHECK_EQ(g.kickoff_epoch, (int64_t) 0);
  MCHECK(g.team_record.empty());
  // Numbers out of range are clamped, not wrapped
  MCHECK(game_read(std::string("{\"dates\":[{\"games\":[{\"gamePk\":7,\"status\":{\"codedGameState\":\"I\"},"
                                    "\"teams\":{\"home\":{\"team\":{\"id\":234},\"score\":-5},"
                                    "\"away\":{\"team\":{\"id\":1},\"score\":99999999999}},"
                                    "\"linescore\":{\"currentInning\":3,\"inningState\":\"Top\",\"balls\":9,"
                                    "\"strikes\":-1,\"outs\":7}}]}]}"),
                        kDurham, 0, g));
  MCHECK_EQ(g.team_score, 0);
  MCHECK_EQ(g.opp_score, 9999);
  MCHECK_EQ((int) g.mlb.balls, 4);
  MCHECK_EQ((int) g.mlb.strikes, 0);
  MCHECK_EQ((int) g.mlb.outs, 3);
}

void test_team() {
  const int64_t nov = at("2026-11-08T23:00:00Z");
  // Between seasons the coming days list nothing...
  Schedule s;
  MCHECK_EQ(team_read(slurp("mlbstats_team_none.json"), kDurham, nov, s), 0);
  MCHECK(s.valid);
  MCHECK(s.event_id.empty());
  // ...and the far read has next season's opener, ids and dates only.
  MCHECK_EQ(team_read(slurp("mlbstats_far_dur.json"), kDurham, nov, s), 1);
  MCHECK_EQ(s.event_id, std::string("860674"));
  MCHECK_EQ(s.kickoff_epoch, at("2027-03-26T22:35:00Z"));
  MCHECK(!s.time_tbd);
  MCHECK_EQ(s.opp_id, 0u);
  MCHECK(s.team_record.empty());
  // which the board shows as the off-season clock with the opener under it
  MCHECK(off_season(GameState::PRE, s.kickoff_epoch, nov, season_horizon(Sport::BASEBALL)));

  // DUR's April trip to Scranton, as read the morning of 4/10: the 4/7 game
  // postponed, 4/8 and 4/9 final, a doubleheader on 4/10 (game 2 with no
  // start time) and again on 4/11 after game 2 was postponed once more.
  const std::string apr10 = reopened("mlbstats_team_dur_202604.json", "2026-04-10T00:00:00Z");
  std::vector<Upcoming> up;
  MCHECK_EQ(team_read(apr10, kDurham, at("2026-04-10T15:00:00Z"), s, &up, 4), 1);
  MCHECK_EQ(s.event_id, std::string("815845"));  // game 1
  MCHECK_EQ(s.kickoff_epoch, at("2026-04-10T20:35:00Z"));
  MCHECK_EQ(s.opp_id, kScranton);
  MCHECK_EQ(s.opp_abbr, std::string("SWB"));
  MCHECK_EQ(s.team_record, std::string("3-9"));
  MCHECK_EQ(up.size(), (size_t) 4);
  if (up.size() == 4) {
    MCHECK_EQ(up[0].event_id, std::string("815845"));
    MCHECK_EQ(up[1].event_id, std::string("815849"));  // game 2: TBD, at midnight Eastern
    MCHECK(up[1].time_tbd);
    MCHECK_EQ(up[1].kickoff_epoch, at("2026-04-10T05:00:00Z"));
    MCHECK_EQ(up[2].event_id, std::string("815847"));  // 4/11 game 1; its game 2 is 815849 again
    MCHECK_EQ(up[3].event_id, std::string("815848"));
    MCHECK_EQ(up[0].opp_name, std::string("RailRiders"));
    MCHECK_EQ(up[0].opp_abbr, std::string("SWB"));
    MCHECK(!up[0].home);
  }
  // Without the list the read stops at the first game.
  MCHECK_EQ(team_read(apr10, kDurham, at("2026-04-10T15:00:00Z"), s), 1);
  MCHECK_EQ(s.event_id, std::string("815845"));

  // Game 1 over: game 2 is next, no start time, read every minute on its day.
  const std::string g2 = reopened("mlbstats_team_dur_202604.json", "2026-04-10T20:40:00Z");
  const int64_t evening = at("2026-04-10T23:30:00Z");
  MCHECK_EQ(team_read(g2, kDurham, evening, s), 1);
  MCHECK_EQ(s.event_id, std::string("815849"));
  MCHECK(s.time_tbd);
  MCHECK_EQ(s.kickoff_epoch, at("2026-04-10T05:00:00Z"));
  MCHECK(pre_poll_near(s.kickoff_epoch - evening, s.time_tbd, true));
  MCHECK(!pre_poll_near(s.kickoff_epoch - evening, s.time_tbd, false));  // not a football TBD
  MCHECK(!pre_poll_near(s.kickoff_epoch - at("2026-04-09T12:00:00Z"), true, true));  // the day before
  MCHECK(!pre_poll_near(s.kickoff_epoch - at("2026-04-11T12:00:00Z"), true, true));  // its day is over
  // and the game read for it is that date's entry, not 4/7's or 4/11's
  GameSnapshot g;
  MCHECK(game_read(slurp("mlbstats_game_postponed.json"), kDurham, s.kickoff_epoch, g));
  MCHECK_EQ(g.kickoff_epoch, at("2026-04-10T05:00:00Z"));

  // A postponed game is passed over: on 4/7 the next game is 4/8's.
  const std::string apr8 = reopened("mlbstats_team_dur_202604.json", "2026-04-08T00:00:00Z");
  MCHECK_EQ(team_read(apr8, kDurham, at("2026-04-07T23:00:00Z"), s), 1);
  MCHECK_EQ(s.event_id, std::string("815856"));
  // A game whose status never moved past Scheduled a day later is left behind.
  MCHECK_EQ(team_read(apr8, kDurham, at("2026-04-09T18:00:00Z"), s), 1);
  MCHECK_EQ(s.event_id, std::string("815855"));
  MCHECK_EQ(team_read(apr8, kDurham, at("2026-04-09T16:00:00Z"), s), 1);
  MCHECK_EQ(s.event_id, std::string("815856"));  // 17.4 hours: still the one

  // A game on now comes first, and the list has only games not started.
  {
    JsonDocument doc;
    deserializeJson(doc, apr10);
    for (JsonObject d : doc["dates"].as<JsonArray>())
      for (JsonObject x : d["games"].as<JsonArray>())
        if ((x["gamePk"] | 0u) == 815845u)
          x["status"]["codedGameState"] = "I";
    std::string live;
    serializeJson(doc, live);
    MCHECK_EQ(team_read(live, kDurham, at("2026-04-10T21:30:00Z"), s, &up, 4), 1);
    MCHECK_EQ(s.event_id, std::string("815845"));
    MCHECK_EQ(up.size(), (size_t) 4);
    if (!up.empty())
      MCHECK_EQ(up[0].event_id, std::string("815849"));
  }
  // The recorded document as it stands: every game over, nothing to show.
  MCHECK_EQ(team_read(slurp("mlbstats_team_dur_202604.json"), kDurham, at("2026-04-18T12:00:00Z"), s, &up, 4),
            0);
  MCHECK(up.empty());
  // Not a schedule document
  MCHECK_EQ(team_read(std::string("{"), kDurham, nov, s), -1);
  MCHECK_EQ(team_read(std::string("[]"), kDurham, nov, s), -1);
  MCHECK_EQ(team_read(std::string("{\"copyright\":\"x\"}"), kDurham, nov, s), -1);
}

void test_day() {
  // A day of winter league games, read 23:16 UTC: eight on, one final, seven
  // to come (one listed for 21:21 still Scheduled two hours on).
  std::vector<LiveGame> live;
  std::vector<Final> finals;
  std::vector<Pending> pending;
  int64_t next = -1;
  MCHECK(day_read(slurp("mlbstats_day_winter.json"), live, &next, &finals, &pending));
  MCHECK_EQ(live.size(), (size_t) 8);
  MCHECK_EQ(finals.size(), (size_t) 1);
  MCHECK_EQ(pending.size(), (size_t) 7);
  MCHECK_EQ(next, at("2026-11-08T21:21:00Z"));
  if (!live.empty()) {
    MCHECK_EQ(live[0].event_id, std::string("870006"));
    MCHECK_EQ(live[0].away_id, kMargarita);
    MCHECK_EQ(live[0].home_id, kOriente);
    MCHECK_EQ(live[0].away_abbr, std::string("MAR"));
    MCHECK_EQ(live[0].home_abbr, std::string("ORI"));
    MCHECK_EQ((int) live[0].league, 0);  // the caller stamps it
  }
  if (!finals.empty()) {
    MCHECK_EQ(finals[0].detail, std::string("Final"));
    MCHECK_EQ(finals[0].away_abbr, std::string("CAR"));
    MCHECK_EQ(finals[0].away_score, 2);
    MCHECK_EQ(finals[0].home_score, 1);
    MCHECK(!finals[0].away_record.empty());
    MCHECK_EQ((int) finals[0].rank, 99);
  }
  // Sorted by start, at most kPendingKeep
  for (size_t i = 1; i < pending.size(); i++)
    MCHECK(pending[i - 1].kickoff_epoch <= pending[i].kickoff_epoch);
  if (!pending.empty()) {
    MCHECK_EQ(pending[0].event_id, std::string("872084"));
    MCHECK(pending[0].tv.empty());
  }
  // The scan alone
  live.clear();
  MCHECK(day_read(slurp("mlbstats_day_winter.json"), live));
  MCHECK_EQ(live.size(), (size_t) 8);

  // A full minor league day at the four levels: 58 finals, three postponed.
  // The rotation keeps a day's kRecapCap earliest finals (recap_merge), so
  // the parse keeps only those, in start order.
  const std::string day = slurp("mlbstats_day_milb_20260814.json");
  std::vector<int64_t> starts;
  {
    JsonDocument doc;
    deserializeJson(doc, day);
    for (JsonObject d : doc["dates"].as<JsonArray>())
      for (JsonObject g : d["games"].as<JsonArray>())
        if (std::string(g["status"]["codedGameState"] | "") == "F")
          starts.push_back(at(g["gameDate"] | ""));
  }
  MCHECK_EQ(starts.size(), (size_t) 58);
  std::stable_sort(starts.begin(), starts.end());
  live.clear();
  finals.clear();
  pending.clear();
  MCHECK(day_read(day, live, &next, &finals, &pending));
  MCHECK(live.empty());
  MCHECK(pending.empty());
  MCHECK_EQ(next, (int64_t) 0);
  MCHECK_EQ(finals.size(), kRecapCap);
  for (size_t i = 0; i < finals.size() && i < starts.size(); i++)
    MCHECK_EQ(finals[i].kickoff_epoch, starts[i]);
  for (const auto &f : finals) {
    MCHECK(f.detail.rfind("Final", 0) == 0);
    MCHECK(!f.away_abbr.empty() && !f.home_abbr.empty());
  }
  // What the rotation keeps of it is the same with or without the cap.
  {
    std::vector<RecapDay> days;
    std::vector<Final> stamped = finals;
    for (auto &f : stamped)
      f.league = (uint8_t) League::MILB;
    recap_absorb(days, stamped, {}, at("2026-08-15T03:00:00Z"));
    MCHECK_EQ(days.size(), (size_t) 1);
    if (!days.empty())
      MCHECK_EQ(days[0].finals.size(), kRecapCap);
  }
  // A scan that already holds another league's finals keeps them all.
  finals.assign(30, Final{});
  live.clear();
  MCHECK(day_read(day, live, nullptr, &finals));
  MCHECK_EQ(finals.size(), (size_t) 30 + kRecapCap);
  // MEM @ DUR, Final 12-2, and an 11-inning final, from a day of two
  const std::string two =
      "{\"dates\":[{\"games\":[{\"gamePk\":816688,\"gameDate\":\"2026-08-14T22:45:00Z\",\"status\":"
      "{\"codedGameState\":\"F\"},\"teams\":{\"away\":{\"team\":{\"id\":235,\"abbreviation\":\"MEM\"},\"score\":12,"
      "\"leagueRecord\":{\"wins\":66,\"losses\":52}},\"home\":{\"team\":{\"id\":234,\"abbreviation\":\"DUR\"},"
      "\"score\":2}},\"linescore\":{\"currentInning\":9,\"scheduledInnings\":9}},{\"gamePk\":826771,\"gameDate\":"
      "\"2026-08-14T23:05:00Z\",\"status\":{\"codedGameState\":\"F\"},\"teams\":{\"away\":{\"team\":{\"id\":1}},"
      "\"home\":{\"team\":{\"id\":2}}},\"linescore\":{\"currentInning\":11,\"scheduledInnings\":9}}]}]}";
  finals.clear();
  MCHECK(day_read(two, live, nullptr, &finals));
  MCHECK_EQ(finals.size(), (size_t) 2);
  if (finals.size() == 2) {
    Final &f = finals[0];
    MCHECK_EQ(f.away_abbr, std::string("MEM"));
    MCHECK_EQ(f.away_score, 12);
    MCHECK_EQ(f.home_score, 2);
    MCHECK_EQ(f.away_record, std::string("66-52"));
    MCHECK(f.home_record.empty());
    MCHECK_EQ(f.detail, std::string("Final"));
    MCHECK_EQ(finals[1].detail, std::string("Final/11"));
    // The finals rotation's card: MLB's logos, a baseball board
    f.league = (uint8_t) League::MILB;
    GameSnapshot card = final_snapshot(f);
    MCHECK_EQ(card.team_logo, mlb_logo_url(kMemphis));
    MCHECK_EQ(card.opp_logo, mlb_logo_url(kDurham));
    MCHECK(card.sport == Sport::BASEBALL);
  }
  Pending p;
  p.league = (uint8_t) League::MILB;
  p.away_id = kMemphis;
  p.home_id = kDurham;
  MCHECK_EQ(pending_snapshot(p).opp_logo, mlb_logo_url(kDurham));
  // Not a schedule document
  MCHECK(!day_read(std::string("{"), live));
  MCHECK(!day_read(std::string("{\"dates\":{}}"), live));
}

void test_host_wait() {
  // MLB's run of failures is its own: ESPN's Backoff is a different object.
  HostWait w;
  MCHECK(!w.waiting(1000));
  uint32_t ms = w.fail(false, 0, 0, 1000);
  MCHECK(ms >= kBackoffBaseMs / 2 && ms <= kBackoffBaseMs);
  MCHECK(w.waiting(1000 + ms - 1));
  MCHECK(!w.waiting(1000 + ms));
  MCHECK_EQ((int) w.run.streak, 1);
  // A refusal waits the longer schedule and honors Retry-After
  ms = w.fail(true, 20 * 60 * 1000, 0, 5000);
  MCHECK(w.run.throttled);
  MCHECK(ms >= 20u * 60 * 1000);
  MCHECK(w.waiting(5000));
  // Refresh Now: read now, keep the run
  w.end_wait();
  MCHECK(!w.waiting(5000));
  MCHECK_EQ((int) w.run.streak, 2);
  // An answer clears it
  w.clear();
  MCHECK(!w.waiting(5000));
  MCHECK_EQ((int) w.run.streak, 0);
  MCHECK(!w.run.throttled);
  // Across the millis() wrap
  w.fail(false, 0, 0, 0xFFFFFF00u);
  MCHECK(w.waiting(0x10));
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
  MCHECK(!deserializeJson(doc, buf));
  MCHECK(doc["mlb"].isNull());  // no minor league read yet
  h.mlb_status = 429;
  h.mlb_ago = 30;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  MCHECK(!deserializeJson(doc, buf));
  MCHECK_EQ(doc["espn"].as<int>(), 200);
  MCHECK_EQ(doc["mlb"].as<int>(), 429);
  MCHECK_EQ(doc["mlb_ago"].as<int>(), 30);
  h.espn_ago = -1;  // only minor league reads so far
  health_json(buf, sizeof(buf), h);
  doc.clear();
  MCHECK(!deserializeJson(doc, buf));
  MCHECK(doc["espn"].isNull());
  MCHECK_EQ(doc["mlb"].as<int>(), 429);
}

}  // namespace

int run_mlbstats_tests(int &total_checks) {
  test_urls();
  test_league();
  test_words();
  test_game_final();
  test_game_postponed();
  test_game_pre();
  test_game_live();
  test_game_documents();
  test_team();
  test_day();
  test_host_wait();
  test_health();
  total_checks += checks;
  return failures;
}
