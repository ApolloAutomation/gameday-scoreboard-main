// Minor league baseball from the MLB Stats API: URLs, filters and the parse
// into the shared snapshot types (source_mlbstats.h). Pure, no ESPHome
// includes. Built for size, as sport_golf.cpp is: it runs once a poll at
// most. Only GCC has the pragma.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include "source_mlbstats.h"

#include "recap.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace espn {

using detail::clamp_int;
using detail::keep;
using detail::text;

static const char *const kSchedule = "https://statsapi.mlb.com/api/v1/schedule";
static const char *const kLevels = "11,12,13,14";
// fields= keeps only these keys, wherever they sit in the document. A parent
// must be listed for its children to come through ("teams,away,team,id").
static const char *const kTeamFields =
    "dates,games,gamePk,gameDate,status,codedGameState,startTimeTBD,teams,away,home,team,id,abbreviation,teamName,"
    "leagueRecord,wins,losses,gameNumber";
static const char *const kFarFields = "dates,games,gamePk,gameDate,status,codedGameState,startTimeTBD";
static const char *const kGameFields =
    "dates,games,gamePk,gameDate,status,codedGameState,statusCode,detailedState,reason,startTimeTBD,teams,away,home,"
    "team,id,abbreviation,score,leagueRecord,wins,losses,isWinner,probablePitcher,fullName,linescore,currentInning,"
    "inningState,scheduledInnings,balls,strikes,outs,offense,defense,batter,pitcher,first,second,third,runs,hits,"
    "errors,venue,name";
static const char *const kDayFields =
    "dates,games,gamePk,gameDate,status,codedGameState,startTimeTBD,teams,away,home,team,id,abbreviation,score,"
    "leagueRecord,wins,losses,linescore,currentInning,scheduledInnings";

// The team read's days around today, and the far read's.
static const int kTeamDaysBack = 1, kTeamDaysAhead = 10, kFarDaysAhead = 240;
// A game not started this long after its listed start is left over from a
// past day (a status never updated): not the game to show. A game with no
// start time yet is listed at midnight Eastern, so its day has to be over.
static const int64_t kStaleStart = 18 * 3600, kStaleTbd = 30 * 3600;
static const int64_t kEastern = 5 * 3600;

const char *milb_level_name(uint32_t level) {
  switch (level) {
    case kMilbTripleA:
      return "Triple-A";
    case kMilbDoubleA:
      return "Double-A";
    case kMilbHighA:
      return "High-A";
    case kMilbSingleA:
      return "Single-A";
    default:
      return "";
  }
}

// The day an epoch falls in, as scan_url counts it for a daily league (US
// Eastern, turning over at 4 AM): days since 1970-01-01.
static int64_t mlb_day(int64_t epoch) {
  int64_t s = epoch - scan_day_shift(League::MILB);
  return s >= 0 ? s / 86400 : (s - 86399) / 86400;
}

static std::string ymd(int64_t day) {
  int y, m, d;
  detail::civil_from_epoch(day * 86400, y, m, d);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
  return buf;
}

std::string mlb_team_url(uint32_t team_id, int64_t now_epoch) {
  int64_t day = mlb_day(now_epoch);
  return std::string(kSchedule) + "?sportId=" + kLevels + "&teamId=" + std::to_string(team_id) +
         "&startDate=" + ymd(day - kTeamDaysBack) + "&endDate=" + ymd(day + kTeamDaysAhead) +
         "&hydrate=team&fields=" + kTeamFields;
}

std::string mlb_far_url(uint32_t team_id, uint32_t level, int64_t now_epoch) {
  if (level < kMilbTripleA || level > kMilbSingleA)
    level = kMilbTripleA;
  int64_t day = mlb_day(now_epoch);
  return std::string(kSchedule) + "?sportId=" + std::to_string(level) + "&teamId=" + std::to_string(team_id) +
         "&startDate=" + ymd(day + kTeamDaysAhead + 1) + "&endDate=" + ymd(day + kFarDaysAhead) +
         "&fields=" + kFarFields;
}

std::string mlb_game_url(const std::string &game_pk) {
  return std::string(kSchedule) + "?gamePk=" + game_pk + "&hydrate=linescore,team,probablePitcher&fields=" +
         kGameFields;
}

std::string mlb_day_url(int64_t day_epoch) {
  return std::string(kSchedule) + "?sportId=" + kLevels + "&date=" + ymd(mlb_day(day_epoch)) +
         "&hydrate=linescore,team&fields=" + kDayFields;
}

std::string mlb_logo_url(uint32_t team_id) {
  return "https://midfield.mlbstatic.com/v1/team/" + std::to_string(team_id) + "/spots/64";
}

std::string mlb_short_name(const std::string &full) {
  std::string name = ascii_fold(full);
  size_t start = name.find_first_not_of(' ');
  if (start == std::string::npos)
    return "";
  size_t space = name.find(' ', start);
  if (space == std::string::npos)
    return name.substr(start);
  size_t rest = name.find_first_not_of(' ', space);
  if (rest == std::string::npos)
    return name.substr(start, space - start);
  return std::string(1, name[start]) + ". " + name.substr(rest);
}

std::string mlb_ordinal(int n) {
  const char *suffix = "th";
  if (n % 100 < 11 || n % 100 > 13) {
    if (n % 10 == 1)
      suffix = "st";
    else if (n % 10 == 2)
      suffix = "nd";
    else if (n % 10 == 3)
      suffix = "rd";
  }
  return std::to_string(n) + suffix;
}

// The first letter of codedGameState: S scheduled and P pre-game or warmup
// (or a delayed start); I in progress, M and N a challenge or a review; F and
// O final, Q and R forfeited; D postponed, C canceled, T and U suspended.
// Anything else (X unknown, W writing) reads as not started.
MlbState mlb_state(const std::string &coded) {
  switch (coded.empty() ? 'S' : coded[0]) {
    case 'I':
    case 'M':
    case 'N':
      return MlbState::LIVE;
    case 'F':
    case 'O':
    case 'Q':
    case 'R':
      return MlbState::FINAL;
    case 'D':
    case 'C':
    case 'T':
    case 'U':
      return MlbState::OFF;
    default:
      return MlbState::PRE;
  }
}

namespace detail {

static void keep_side_team(JsonObject side, std::initializer_list<const char *> team_keys) {
  keep(side["team"].to<JsonObject>(), team_keys);
  keep(side["leagueRecord"].to<JsonObject>(), {"wins", "losses"});
}

void fill_mlb_team_filter(JsonDocument &f) {
  JsonObject g = f["dates"].add<JsonObject>()["games"].add<JsonObject>();
  keep(g, {"gamePk", "gameDate", "gameNumber"});
  keep(g["status"].to<JsonObject>(), {"codedGameState", "startTimeTBD"});
  JsonObject teams = g["teams"].to<JsonObject>();
  keep_side_team(teams["away"].to<JsonObject>(), {"id", "abbreviation", "teamName"});
  keep_side_team(teams["home"].to<JsonObject>(), {"id", "abbreviation", "teamName"});
}

void fill_mlb_game_filter(JsonDocument &f) {
  JsonObject g = f["dates"].add<JsonObject>()["games"].add<JsonObject>();
  keep(g, {"gamePk", "gameDate", "scheduledInnings"});
  keep(g["status"].to<JsonObject>(), {"codedGameState", "statusCode", "detailedState", "reason", "startTimeTBD"});
  JsonObject teams = g["teams"].to<JsonObject>();
  JsonObject ls = g["linescore"].to<JsonObject>();
  for (const char *side : {"away", "home"}) {
    JsonObject t = teams[side].to<JsonObject>();
    keep_side_team(t, {"id", "abbreviation"});
    keep(t, {"score", "isWinner"});
    t["probablePitcher"]["fullName"] = true;
    keep(ls["teams"][side].to<JsonObject>(), {"hits", "errors"});
  }
  keep(ls, {"currentInning", "inningState", "scheduledInnings", "balls", "strikes", "outs"});
  JsonObject off = ls["offense"].to<JsonObject>();
  off["batter"]["fullName"] = true;
  for (const char *base : {"first", "second", "third"})
    off[base]["id"] = true;
  ls["defense"]["pitcher"]["fullName"] = true;
  g["venue"]["name"] = true;
}

void fill_mlb_day_filter(JsonDocument &f) {
  JsonObject g = f["dates"].add<JsonObject>()["games"].add<JsonObject>();
  keep(g, {"gamePk", "gameDate"});
  keep(g["status"].to<JsonObject>(), {"codedGameState", "startTimeTBD"});
  JsonObject teams = g["teams"].to<JsonObject>();
  for (const char *side : {"away", "home"}) {
    JsonObject t = teams[side].to<JsonObject>();
    keep_side_team(t, {"id", "abbreviation"});
    t["score"] = true;
  }
  keep(g["linescore"].to<JsonObject>(), {"currentInning", "scheduledInnings"});
}

// The listed start, or for a game with no start time yet (startTimeTBD: game
// 2 of a doubleheader, after game 1) midnight Eastern of its day: the
// placeholder ESPN uses, which the kickoff labels and tbd_day_epoch expect.
static int64_t game_start(JsonObjectConst g, bool &tbd) {
  int64_t kick = parse_iso8601_z(text(g, "gameDate"));
  tbd = g["status"]["startTimeTBD"] | false;
  if (tbd && kick > 0) {
    int64_t s = kick - kEastern;
    int64_t day = s >= 0 ? s / 86400 : (s - 86399) / 86400;
    kick = day * 86400 + kEastern;
  }
  return kick;
}

static uint32_t team_id(JsonObjectConst side) { return side["team"]["id"] | 0u; }

// "66-52" from leagueRecord, "" when it isn't there.
static std::string record(JsonObjectConst side) {
  JsonObjectConst r = side["leagueRecord"];
  int w = clamp_int(r["wins"], 0, 999, -1), l = clamp_int(r["losses"], 0, 999, -1);
  if (w < 0 || l < 0)
    return "";
  return std::to_string(w) + "-" + std::to_string(l);
}

// One side of a game, read once whichever read it is in (a field a read
// doesn't keep comes back empty or 0), then handed to us or them.
struct Side {
  uint32_t id{0};
  int score{0}, hits{0}, errors{0};
  bool winner{false};
  std::string abbr, name, record, probable;
};

static Side read_side(JsonObjectConst g, const char *key) {
  JsonObjectConst t = g["teams"][key];
  JsonObjectConst line = g["linescore"]["teams"][key];
  Side s;
  s.id = team_id(t);
  s.score = clamp_int(t["score"], 0, 9999, 0);
  s.hits = clamp_int(line["hits"], 0, 999, 0);
  s.errors = clamp_int(line["errors"], 0, 999, 0);
  s.winner = t["isWinner"] | false;
  s.abbr = text(t["team"], "abbreviation");
  s.name = ascii_fold(text(t["team"], "teamName"));
  s.record = record(t);
  s.probable = mlb_short_name(text(t["probablePitcher"], "fullName"));
  return s;
}

int mlb_team_from_doc(JsonObjectConst root, uint32_t our_id, int64_t now, Schedule &out,
                      std::vector<Upcoming> *upcoming, size_t max) {
  JsonArrayConst dates = root["dates"];
  if (dates.isNull())
    return -1;
  Schedule s;
  s.valid = true;
  bool found = false;
  if (upcoming != nullptr)
    upcoming->clear();
  for (JsonObjectConst d : dates) {
    for (JsonObjectConst g : d["games"].as<JsonArrayConst>()) {
      uint32_t pk = g["gamePk"] | 0u;
      MlbState st = mlb_state(text(g["status"], "codedGameState"));
      if (pk == 0 || st == MlbState::FINAL || st == MlbState::OFF)
        continue;
      bool tbd = false;
      int64_t kick = game_start(g, tbd);
      if (st == MlbState::PRE && now > 0 && kick > 0 && now - kick > (tbd ? kStaleTbd : kStaleStart))
        continue;
      Side away = read_side(g, "away"), home = read_side(g, "home");
      bool at_home = our_id != 0 && home.id == our_id;
      Side &us = at_home ? home : away, &them = at_home ? away : home;
      std::string id = std::to_string(pk);
      if (!found) {
        found = true;
        s.event_id = id;
        s.kickoff_epoch = kick;
        s.time_tbd = tbd;
        s.opp_id = them.id;
        s.opp_abbr = them.abbr;
        s.team_record = us.record;
      }
      if (upcoming == nullptr)
        break;
      bool dup = false;
      for (const auto &u : *upcoming)
        dup = dup || u.event_id == id;
      if (st != MlbState::PRE || dup || upcoming->size() >= max || them.id == 0)
        continue;
      Upcoming u;
      u.event_id = id;
      u.kickoff_epoch = kick;
      u.time_tbd = tbd;
      u.opp_id = them.id;
      u.opp_abbr = std::move(them.abbr);
      u.opp_name = std::move(them.name);
      u.home = at_home;
      upcoming->push_back(std::move(u));
    }
    if (found && upcoming == nullptr)
      break;
  }
  out = std::move(s);
  return found ? 1 : 0;
}

// "Top 5th", "Mid 5th", "Bot 5th", "End 5th": ESPN's words, which
// half_from_detail and the clock row read.
static std::string inning_text(const std::string &state, int inning) {
  const char *w = state == "Top" ? "Top" : state == "Bottom" ? "Bot" : state == "Middle" ? "Mid"
                : state == "End"  ? "End"
                                  : nullptr;
  if (w == nullptr || inning <= 0)
    return "In Progress";
  return std::string(w) + " " + mlb_ordinal(inning);
}

// A delay in an inning: statusCode I plus the reason's letter (IR rain, IZ
// about to resume). IH is a replay review, played through.
static bool is_delay(const std::string &code) { return code.size() == 2 && code[0] == 'I' && code[1] != 'H'; }

// "Rain Delay" for the weather the ticker can name in a word, else "Delayed".
static std::string delay_text(const std::string &reason) {
  for (const char *r : {"Rain", "Snow", "Fog", "Lightning", "Wind", "Cold"})
    if (reason == r)
      return reason + " Delay";
  return "Delayed";
}

// "Final", or "Final/10" after extra innings and "Final/7" when it was called
// early: the innings played when they differ from the innings scheduled.
static std::string final_text(int inning, int scheduled) {
  if (inning > 0 && scheduled > 0 && inning != scheduled)
    return "Final/" + std::to_string(inning);
  return "Final";
}

static int scheduled_innings(JsonObjectConst g) {
  int n = clamp_int(g["linescore"]["scheduledInnings"], 0, 99, 0);
  return n > 0 ? n : clamp_int(g["scheduledInnings"], 0, 99, 9);
}

bool mlb_game_from_doc(JsonObjectConst root, uint32_t our_id, int64_t near_epoch, GameSnapshot &out) {
  JsonArrayConst dates = root["dates"];
  if (dates.isNull())
    return false;
  JsonObjectConst g;
  int64_t best = 0, kick = 0;
  bool tbd = false;
  for (JsonObjectConst d : dates) {
    for (JsonObjectConst e : d["games"].as<JsonArrayConst>()) {
      if ((e["gamePk"] | 0u) == 0)
        continue;
      bool t = false;
      int64_t k = game_start(e, t);
      int64_t gap = k > near_epoch ? k - near_epoch : near_epoch - k;
      if (g.isNull() || gap <= best) {  // ties go to the later entry
        g = e;
        best = gap;
        kick = k;
        tbd = t;
      }
    }
  }
  if (g.isNull())
    return false;
  JsonObjectConst status = g["status"];
  const std::string code = text(status, "statusCode");
  const std::string coded = text(status, "codedGameState");
  JsonObjectConst ls = g["linescore"];
  const int inning = clamp_int(ls["currentInning"], 0, 99, 0);
  const std::string inning_state = text(ls, "inningState");

  GameSnapshot s;
  s.sport = Sport::BASEBALL;
  s.event_id = std::to_string(g["gamePk"] | 0u);
  s.kickoff_epoch = kick;
  s.time_tbd = tbd;
  s.period = inning;
  s.venue = ascii_fold(text(g["venue"], "name"));
  switch (mlb_state(coded)) {
    case MlbState::PRE:
      s.state = GameState::PRE;
      // PW warmup; P plus a reason letter is a delayed start
      s.short_detail = code == "PW" ? "Warmup" : code.size() == 2 ? "Delayed" : "Scheduled";
      break;
    case MlbState::LIVE:
      s.state = GameState::IN;
      s.short_detail = is_delay(code) ? delay_text(text(status, "reason")) : inning_text(inning_state, inning);
      break;
    case MlbState::FINAL:
      s.state = GameState::POST;
      s.completed = true;
      s.short_detail = final_text(inning, scheduled_innings(g));
      break;
    case MlbState::OFF:
      // ESPN's post state without a result
      s.state = GameState::POST;
      s.short_detail = coded[0] == 'D' ? "Postponed" : coded[0] == 'C' ? "Canceled" : "Suspended";
      break;
  }

  Side away = read_side(g, "away"), home = read_side(g, "home");
  s.team_home = our_id != 0 && home.id == our_id;
  if (!s.team_home && (our_id == 0 || away.id != our_id))
    return false;
  Side &us = s.team_home ? home : away, &them = s.team_home ? away : home;
  Baseball &b = s.mlb;
  s.team_id = us.id;
  s.opp_id = them.id;
  s.team_abbr = std::move(us.abbr);
  s.opp_abbr = std::move(them.abbr);
  s.team_score = us.score;
  s.opp_score = them.score;
  s.team_record = std::move(us.record);
  s.opp_record = std::move(them.record);
  s.team_logo = mlb_logo_url(us.id);
  s.opp_logo = them.id != 0 ? mlb_logo_url(them.id) : std::string();
  s.team_winner = us.winner;
  b.team_hits = us.hits;
  b.opp_hits = them.hits;
  b.team_errors = us.errors;
  b.opp_errors = them.errors;
  b.team_probable = std::move(us.probable);
  b.opp_probable = std::move(them.probable);
  if (s.state == GameState::IN) {
    b.half = half_from_detail(s.short_detail);
    if (b.half == Half::TOP || b.half == Half::BOTTOM) {
      // Between half innings the API keeps the last count, as ESPN does.
      if (!ls["balls"].isNull())
        b.balls = (int8_t) clamp_int(ls["balls"], 0, 4, 0);
      if (!ls["strikes"].isNull())
        b.strikes = (int8_t) clamp_int(ls["strikes"], 0, 3, 0);
      if (!ls["outs"].isNull())
        b.outs = (int8_t) clamp_int(ls["outs"], 0, 3, 0);
      JsonObjectConst off = ls["offense"];
      b.bases = (off["first"].isNull() ? 0 : Baseball::FIRST) | (off["second"].isNull() ? 0 : Baseball::SECOND) |
                (off["third"].isNull() ? 0 : Baseball::THIRD);
      b.batter = mlb_short_name(text(off["batter"], "fullName"));
      b.pitcher = mlb_short_name(text(ls["defense"]["pitcher"], "fullName"));
      // The visitors bat in the top half: that side gets the underline.
      bool we_bat = (b.half == Half::TOP) != s.team_home;
      s.possession = we_bat ? 1 : 2;
    }
  }
  s.valid = true;
  out = std::move(s);
  return true;
}

bool mlb_day_from_doc(JsonObjectConst root, std::vector<LiveGame> &out, int64_t *next_kickoff,
                      std::vector<Final> *finals, std::vector<Pending> *pending) {
  JsonArrayConst dates = root["dates"];
  if (dates.isNull())
    return false;
  if (next_kickoff != nullptr)
    *next_kickoff = 0;
  // Kept apart, so the cap counts this document only.
  std::vector<Pending> later;
  // A full day is 60 finals at the four levels, and the finals rotation keeps
  // a day's kRecapCap earliest (recap_merge). The API lists a day's games in
  // start order (postponed ones last), so the first kRecapCap are those: the
  // rest never sit in internal RAM.
  size_t kept = 0;
  for (JsonObjectConst d : dates) {
    for (JsonObjectConst g : d["games"].as<JsonArrayConst>()) {
      uint32_t pk = g["gamePk"] | 0u;
      Side away = read_side(g, "away"), home = read_side(g, "home");
      if (pk == 0 || away.id == 0 || home.id == 0)
        continue;
      bool tbd = false;
      int64_t kick = game_start(g, tbd);
      switch (mlb_state(text(g["status"], "codedGameState"))) {
        case MlbState::PRE: {
          if (tbd || kick <= 0)
            break;
          if (next_kickoff != nullptr && (*next_kickoff == 0 || kick < *next_kickoff))
            *next_kickoff = kick;
          if (pending == nullptr)
            break;
          Pending p;
          p.kickoff_epoch = kick;
          p.event_id = std::to_string(pk);
          p.away_id = away.id;
          p.home_id = home.id;
          p.away_abbr = std::move(away.abbr);
          p.home_abbr = std::move(home.abbr);
          p.away_record = std::move(away.record);
          p.home_record = std::move(home.record);
          auto at = std::upper_bound(later.begin(), later.end(), p, pending_before);
          if (later.size() < kPendingKeep || at != later.end()) {
            later.insert(at, std::move(p));
            if (later.size() > kPendingKeep)
              later.pop_back();
          }
          break;
        }
        case MlbState::FINAL: {
          if (finals == nullptr || kept >= kRecapCap)
            break;
          kept++;
          Final f;
          f.kickoff_epoch = kick;
          f.detail = final_text(clamp_int(g["linescore"]["currentInning"], 0, 99, 0), scheduled_innings(g));
          f.away_id = away.id;
          f.home_id = home.id;
          f.away_score = away.score;
          f.home_score = home.score;
          f.away_abbr = std::move(away.abbr);
          f.home_abbr = std::move(home.abbr);
          f.away_record = std::move(away.record);
          f.home_record = std::move(home.record);
          finals->push_back(std::move(f));
          break;
        }
        case MlbState::LIVE: {
          LiveGame lg;
          lg.event_id = std::to_string(pk);
          lg.away_id = away.id;
          lg.home_id = home.id;
          lg.away_abbr = std::move(away.abbr);
          lg.home_abbr = std::move(home.abbr);
          out.push_back(std::move(lg));
          break;
        }
        case MlbState::OFF:
          break;
      }
    }
  }
  if (pending != nullptr)
    for (auto &p : later)
      pending->push_back(std::move(p));
  return true;
}

}  // namespace detail
}  // namespace espn
