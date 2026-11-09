// Template bodies for the streaming parsers. Included from espn_parse.h.
#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <utility>

namespace espn {
namespace detail {

inline ArduinoJson::Allocator *&json_allocator_slot() {
  static ArduinoJson::Allocator *slot = nullptr;
  return slot;
}

// Where the component's JSON documents live. The firmware points this at
// PSRAM in GamedayComponent::setup(); host tests keep ArduinoJson's default.
inline ArduinoJson::Allocator *json_allocator() {
  ArduinoJson::Allocator *a = json_allocator_slot();
  return a != nullptr ? a : ArduinoJson::detail::DefaultAllocator::instance();
}

inline std::string str_or_empty(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  return s ? std::string(s) : std::string();
}

// True when the competition says its kickoff time is not set yet
// (timeValid false). A document without the field has a real time.
inline bool time_tbd(JsonObjectConst comp) {
  JsonVariantConst v = comp["timeValid"];
  return v.is<bool>() && !v.as<bool>();
}

// The fields read from one event, whether it sits in a scoreboard's events
// array or is a single-event document's root.
inline void fill_event_filter(JsonObject ev) {
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["displayClock"] = true;
  ev["status"]["period"] = true;
  ev["status"]["type"]["state"] = true;
  ev["status"]["type"]["completed"] = true;
  ev["status"]["type"]["shortDetail"] = true;
  ev["status"]["type"]["name"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["situation"]["lastPlay"]["text"] = true;
  comp["situation"]["lastPlay"]["probability"]["secondsLeft"] = true;
  comp["situation"]["downDistanceText"] = true;
  comp["situation"]["shortDownDistanceText"] = true;
  comp["situation"]["isRedZone"] = true;
  comp["situation"]["possession"] = true;
  comp["situation"]["homeTimeouts"] = true;
  comp["situation"]["awayTimeouts"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["homeAway"] = true;
  c["score"] = true;
  c["winner"] = true;
  c["records"].add<JsonObject>()["summary"] = true;
  c["curatedRank"]["current"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["color"] = true;
  c["team"]["logo"] = true;
  comp["odds"].add<JsonObject>()["details"] = true;
  comp["odds"][0]["overUnder"] = true;
  comp["broadcasts"].add<JsonObject>()["names"] = true;
  comp["venue"]["fullName"] = true;
  comp["notes"].add<JsonObject>()["headline"] = true;
}

inline void fill_scoreboard_filter(JsonDocument &f) { fill_event_filter(f["events"].add<JsonObject>()); }

inline void fill_baseball_filter(JsonObject ev) {
  JsonObject comp = ev["competitions"][0];
  JsonObject sit = comp["situation"];
  sit["balls"] = true;
  sit["strikes"] = true;
  sit["outs"] = true;
  sit["onFirst"] = true;
  sit["onSecond"] = true;
  sit["onThird"] = true;
  sit["batter"]["athlete"]["shortName"] = true;
  sit["pitcher"]["athlete"]["shortName"] = true;
  sit["lastPlay"]["type"]["text"] = true;
  sit["lastPlay"]["scoreValue"] = true;  // a grand slam (alerts.h)
  JsonObject c = comp["competitors"][0];
  c["hits"] = true;
  c["errors"] = true;
  c["probables"][0]["athlete"]["shortName"] = true;
  comp["series"]["summary"] = true;
}

// ranks: a college schedule's curatedRank (a pro league's says 99 for every
// team, a pool's worth of JSON on a season's schedule for nothing).
inline void fill_upcoming_filter(JsonDocument &f, bool ranks = false) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["neutralSite"] = true;
  comp["status"]["type"]["state"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["homeAway"] = true;
  c["team"]["id"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["shortDisplayName"] = true;
  if (ranks)
    c["curatedRank"]["current"] = true;
  comp["broadcasts"].add<JsonObject>()["media"]["shortName"] = true;
}

// finals keeps what a finished game's card needs as well (the finals
// rotation, recap.h), pending what a game later today's card needs; without
// them the scan stays as lean as it was.
inline void fill_scan_filter(JsonDocument &f, bool finals = false, bool pending = false) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["type"]["state"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["homeAway"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["conferenceId"] = true;
  if (!finals && !pending)
    return;
  c["records"].add<JsonObject>()["summary"] = true;
  c["curatedRank"]["current"] = true;
  c["team"]["color"] = true;
  if (pending)
    comp["broadcasts"].add<JsonObject>()["names"] = true;
  if (!finals)
    return;
  ev["status"]["type"]["completed"] = true;
  ev["status"]["type"]["shortDetail"] = true;
  c["score"] = true;
  c["winner"] = true;
}

// seed: keep the record's stats for the Standing setting's seed (a filter
// keeps every item's, about 9 KB more for an MLB team's 108); a college team
// or a cup's read has no use for it.
inline void fill_team_filter(JsonDocument &f, bool seed = true) {
  f["team"]["id"] = true;
  f["team"]["color"] = true;
  JsonObject item = f["team"]["record"]["items"].add<JsonObject>();
  item["summary"] = true;
  if (seed) {
    JsonObject stat = item["stats"].add<JsonObject>();
    stat["name"] = true;
    stat["value"] = true;
  }
  f["team"]["standingSummary"] = true;
  f["team"]["groups"]["id"] = true;
  JsonObject ne = f["team"]["nextEvent"].add<JsonObject>();
  ne["id"] = true;
  ne["date"] = true;
  JsonObject comp = ne["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["status"]["type"]["state"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["team"]["abbreviation"] = true;
}

inline void fill_team_game_filter(JsonDocument &f) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["type"]["state"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["competitors"].add<JsonObject>()["id"] = true;
}

// Totals run from 2.5 (soccer) to the 200s (basketball). A value this far out
// is not one, and casting it to int would be undefined behavior.
static const float kOverUnderLimit = 10000.0f;

inline std::string format_over_under(JsonVariantConst v) {
  if (v.isNull())
    return "";
  float f = v.as<float>();
  if (!(f > -kOverUnderLimit && f < kOverUnderLimit))  // also false for NaN
    return "";
  char buf[16];
  if (f == (int) f)
    snprintf(buf, sizeof(buf), "%d", (int) f);
  else
    snprintf(buf, sizeof(buf), "%.1f", f);
  return buf;
}

// A competitor's curatedRank: 1-98, 0 when ESPN says unranked (99) or sends
// none (espn_parse.cpp, one copy for every parse that reads it).
uint8_t rank_of(JsonObjectConst c);

inline bool snapshot_from_event(JsonObjectConst ev, uint32_t our_team_id, GameSnapshot &out) {
  GameSnapshot s;
  s.event_id = str_or_empty(ev["id"]);
  s.kickoff_epoch = parse_iso8601_z(str_or_empty(ev["date"]));
  JsonObjectConst status = ev["status"];
  std::string state = str_or_empty(status["type"]["state"]);
  if (state == "in")
    s.state = GameState::IN;
  else if (state == "post")
    s.state = GameState::POST;
  else
    s.state = GameState::PRE;
  s.completed = status["type"]["completed"] | false;
  s.short_detail = str_or_empty(status["type"]["shortDetail"]);
  s.status_name = str_or_empty(status["type"]["name"]);
  s.display_clock = str_or_empty(status["displayClock"]);
  s.period = status["period"] | 0;

  JsonObjectConst comp = ev["competitions"][0];
  if (comp.isNull())
    return false;
  s.time_tbd = time_tbd(comp);
  JsonObjectConst sit = comp["situation"];
  JsonObjectConst lp = sit["lastPlay"];
  s.last_play = str_or_empty(lp["text"]);
  s.play_secs_left = lp["probability"]["secondsLeft"] | -1;
  // The rest is in the filter for football (type, id, teams) and baseball
  // (type, runs) only.
  s.play_type = str_or_empty(lp["type"]["text"]);
  s.play_id = str_or_empty(lp["id"]);
  s.play_points = lp["scoreValue"] | 0;
  std::string play_team = str_or_empty(lp["end"]["team"]["id"]);  // who has the ball after it
  if (play_team.empty())
    play_team = str_or_empty(lp["team"]["id"]);
  s.down_distance = str_or_empty(sit["downDistanceText"]);
  s.short_down_distance = str_or_empty(sit["shortDownDistanceText"]);
  s.is_red_zone = sit["isRedZone"] | false;
  std::string possession = str_or_empty(sit["possession"]);
  int home_to = sit["homeTimeouts"] | -1;  // -1: not sent (carry_football)
  int away_to = sit["awayTimeouts"] | -1;

  bool found_us = false;
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    uint32_t id = (uint32_t) atol(str_or_empty(c["id"]).c_str());
    bool home = str_or_empty(c["homeAway"]) == "home";
    std::string abbr = str_or_empty(c["team"]["abbreviation"]);
    int score = atoi(str_or_empty(c["score"]).c_str());
    std::string record = str_or_empty(c["records"][0]["summary"]);
    std::string color = str_or_empty(c["team"]["color"]);
    std::string logo = dark_logo(str_or_empty(c["team"]["logo"]));
    bool winner = c["winner"] | false;
    int timeouts = home ? home_to : away_to;
    bool ours = id == our_team_id;
    if (ours)
      found_us = true;
    std::string &t_abbr = ours ? s.team_abbr : s.opp_abbr;
    t_abbr = abbr;
    (ours ? s.team_id : s.opp_id) = id;
    (ours ? s.team_score : s.opp_score) = score;
    (ours ? s.team_record : s.opp_record) = record;
    (ours ? s.team_color : s.opp_color) = color;
    (ours ? s.team_logo : s.opp_logo) = logo;
    (ours ? s.team_timeouts : s.opp_timeouts) = timeouts;
    (ours ? s.team_rank : s.opp_rank) = rank_of(c);
    if (ours) {
      s.team_winner = winner;
      s.team_home = home;
    }
    if (!possession.empty() && possession == str_or_empty(c["id"]))
      s.possession = ours ? 1 : 2;
    if (!play_team.empty() && play_team == str_or_empty(c["id"]))
      s.play_side = ours ? 1 : 2;
  }
  if (!found_us)
    return false;

  JsonObjectConst odds = comp["odds"][0];
  s.odds = str_or_empty(odds["details"]);
  s.over_under = format_over_under(odds["overUnder"]);
  s.tv = str_or_empty(comp["broadcasts"][0]["names"][0]);
  s.venue = str_or_empty(comp["venue"]["fullName"]);
  s.game_name = short_game_name(str_or_empty(comp["notes"][0]["headline"]));
  s.valid = true;
  out = std::move(s);
  return true;
}

inline Half half_from_detail(const std::string &detail) {
  if (detail.rfind("Top", 0) == 0)
    return Half::TOP;
  if (detail.rfind("Bot", 0) == 0)
    return Half::BOTTOM;
  if (detail.rfind("Mid", 0) == 0)
    return Half::MID;
  if (detail.rfind("End", 0) == 0)
    return Half::END;
  return Half::NONE;
}

// Fills s.mlb from an event already read into s by snapshot_from_event.
inline void baseball_from_event(JsonObjectConst ev, GameSnapshot &s) {
  Baseball &b = s.mlb;
  JsonObjectConst comp = ev["competitions"][0];
  b.series = str_or_empty(comp["series"]["summary"]);
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
    (ours ? b.team_hits : b.opp_hits) = c["hits"] | 0;
    (ours ? b.team_errors : b.opp_errors) = c["errors"] | 0;
    (ours ? b.team_probable : b.opp_probable) = str_or_empty(c["probables"][0]["athlete"]["shortName"]);
  }
  if (s.state != GameState::IN)
    return;
  b.half = half_from_detail(s.short_detail);
  JsonObjectConst sit = comp["situation"];
  if (b.half != Half::TOP && b.half != Half::BOTTOM)
    return;  // between half innings ESPN keeps the last count; it means nothing
  if (!sit["balls"].isNull())
    b.balls = (int8_t) (sit["balls"] | 0);
  if (!sit["strikes"].isNull())
    b.strikes = (int8_t) (sit["strikes"] | 0);
  if (!sit["outs"].isNull())
    b.outs = (int8_t) (sit["outs"] | 0);
  b.bases = ((sit["onFirst"] | false) ? Baseball::FIRST : 0) | ((sit["onSecond"] | false) ? Baseball::SECOND : 0) |
            ((sit["onThird"] | false) ? Baseball::THIRD : 0);
  b.batter = str_or_empty(sit["batter"]["athlete"]["shortName"]);
  b.pitcher = str_or_empty(sit["pitcher"]["athlete"]["shortName"]);
  // The visitors bat in the top half: that side gets the underline.
  bool we_bat = (b.half == Half::TOP) != s.team_home;
  s.possession = we_bat ? 1 : 2;
}

// Favorite players (players.h): what a game read keeps on top of its
// sport's own fields while a favorite player is in its league. Football,
// baseball and basketball: the last play's id, its points and the athletes
// in it, and ESPN's game leaders (football's for the game, basketball's per
// team, baseball's both). Soccer: each goal's scorer. Hockey: nothing, since
// ESPN names no athletes there (players.h matches the scorer's name in the
// play's words).
inline void fill_player_filter(JsonObject ev, Sport sport) {
  JsonObject comp = ev["competitions"][0];
  if (sport == Sport::SOCCER) {
    comp["details"][0]["athletesInvolved"][0]["id"] = true;
    return;
  }
  if (sport != Sport::FOOTBALL && sport != Sport::BASEBALL && sport != Sport::BASKETBALL)
    return;
  JsonObject lp = comp["situation"]["lastPlay"];
  lp["id"] = true;
  lp["scoreValue"] = true;
  JsonObject who = lp["athletesInvolved"].add<JsonObject>();
  who["id"] = true;
  who["team"]["id"] = true;
  auto leaders = [](JsonObject o) {
    JsonObject cat = o["leaders"].add<JsonObject>();
    cat["name"] = true;
    JsonObject l = cat["leaders"].add<JsonObject>();
    l["displayValue"] = true;
    l["athlete"]["id"] = true;
  };
  leaders(comp);
  leaders(comp["competitors"][0]);
}

inline uint32_t id_of(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  if (s == nullptr || *s < '0' || *s > '9')
    return 0;
  unsigned long long n = strtoull(s, nullptr, 10);
  return n > UINT32_MAX ? 0 : (uint32_t) n;
}

// The athletes of the last play and the leaders that are favorites, into s.
// Basketball keeps each category a favorite leads; the other sports' lines
// for one player say the same in every category (a batting line), so they
// keep the player's first, without the category.
inline void players_from_event(JsonObjectConst ev, const PlayerWatch &w, Sport sport, GameSnapshot &s) {
  JsonObjectConst comp = ev["competitions"][0];
  for (JsonObjectConst a : comp["situation"]["lastPlay"]["athletesInvolved"].as<JsonArrayConst>()) {
    const uint32_t id = id_of(a["id"]);
    if (id != 0 && s.play_athletes.size() < 4)
      s.play_athletes.push_back(PlayAthlete{id, id_of(a["team"]["id"])});
  }
  const bool cats = sport == Sport::BASKETBALL;
  auto take = [&](JsonArrayConst list) {
    for (JsonObjectConst cat : list)
      for (JsonObjectConst l : cat["leaders"].as<JsonArrayConst>()) {
        const uint32_t id = id_of(l["athlete"]["id"]);
        if (!w.has(id) || s.leaders.size() >= 16)
          continue;
        bool have = false;
        for (const Leader &k : s.leaders)
          have = have || (k.athlete == id && !cats);
        if (!have)
          s.leaders.push_back(Leader{id, cats ? str_or_empty(cat["name"]) : std::string(), str_or_empty(l["displayValue"])});
      }
  };
  take(comp["leaders"].as<JsonArrayConst>());
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>())
    take(c["leaders"].as<JsonArrayConst>());
}

// Soccer (sport_soccer.cpp): the status name, details, form, stats and
// moneyline on top of the shared event filter, read into s.soc.
void fill_soccer_filter(JsonObject ev);
void soccer_from_event(JsonObjectConst ev, GameSnapshot &s);
// Basketball (sport_basketball.cpp): the extra fields to keep, and s.nba
// filled from an event already read into s by snapshot_from_event.
void fill_basketball_filter(JsonObject ev);
void basketball_from_event(JsonObjectConst ev, GameSnapshot &s);
// Hockey's extra filter fields and their parse (sport_hockey.cpp)
void fill_hockey_filter(JsonObject ev);
void hockey_from_event(JsonObjectConst ev, GameSnapshot &s);
// The leaderboard sports' JSON helpers (sport_golf.cpp), one call per field
// read rather than the lookup inlined at every one. keep sets each key to
// true in a filter object; field, text, when and flag read one key (a string,
// an ISO time, a bool); clamp_int an integer clamped to [lo, hi], def when it
// is missing or not a number; status_of ESPN's status name.
void keep(JsonObject o, std::initializer_list<const char *> keys);
JsonVariantConst field(JsonVariantConst v, const char *key);
std::string text(JsonVariantConst v, const char *key);
int64_t when(JsonVariantConst v, const char *key);
bool flag(JsonVariantConst v, const char *key);
int clamp_int(JsonVariantConst v, int lo, int hi, int def);
GolfStatus status_of(const std::string &name);
// Golf (sport_golf.cpp). The header: sports[0].leagues[0].events[] and their
// competitors (the top 25). The calendar: leagues[0].calendar[] only, so a
// scoreboard's events and their hole-by-hole scores drop out as they stream.
void fill_golf_filter(JsonDocument &f);
void fill_golf_calendar_filter(JsonDocument &f);
int golf_from_doc(JsonObjectConst root, GameSnapshot &out);
int golf_next_from_doc(JsonObjectConst root, int64_t now_epoch, const std::string &after_event, GolfNext &out);
// Racing (sport_racing.cpp). The header: sports[0].leagues[0].events[], one
// per session, and their competitors' order, names, car numbers, gaps and
// laps down. The calendar: leagues[0].calendar[] (each entry's event id is
// in its $ref).
void fill_racing_filter(JsonDocument &f);
void fill_racing_calendar_filter(JsonDocument &f);
int racing_from_doc(JsonObjectConst root, GameSnapshot &out);
int racing_next_from_doc(JsonObjectConst root, int64_t now_epoch, const std::string &after_event, int64_t after_start,
                         GolfNext &out);

// What parse_event reads for sport: the shared event fields and the sport's
// own (football's last play type, id and teams; the baseball, soccer,
// basketball and hockey extras), on a single-event document's root or on a
// day scoreboard's event. game_from_event reads one event with them,
// oriented around our_team_id. Both in espn_parse.cpp.
void fill_game_filter(JsonObject ev, Sport sport);
bool game_from_event(JsonObjectConst ev, Sport sport, uint32_t our_team_id, GameSnapshot &out);
// A day scoreboard's event as far as saying whether it is wanted
// (espn_parse.cpp).
DayGame day_game(JsonObjectConst ev);

}  // namespace detail

// Must be set before any parse runs, since the fetch task reads it unlocked.
inline void set_json_allocator(ArduinoJson::Allocator *allocator) { detail::json_allocator_slot() = allocator; }

// Parses the team endpoint. `input` is anything ArduinoJson can read from:
// a std::string, or a reader object with read() and readBytes().
template<typename TInput> bool parse_team(TInput &input, Schedule &out, bool seed = true) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_team_filter(filter, seed);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  JsonObjectConst team = doc["team"];
  if (team.isNull())
    return false;
  Schedule s;
  s.team_color = detail::str_or_empty(team["color"]);
  s.team_record = detail::str_or_empty(team["record"]["items"][0]["summary"]);
  // The first item is the season's ("total"); its playoffSeed is a number.
  for (JsonObjectConst st : team["record"]["items"][0]["stats"].as<JsonArrayConst>()) {
    if (detail::str_or_empty(st["name"]) != "playoffSeed")
      continue;
    float seed = st["value"] | 0.0f;
    if (seed >= 1.0f && seed < 100.0f)  // also false for NaN
      s.seed = (uint8_t) seed;
    break;
  }
  s.place = place_short(detail::str_or_empty(team["standingSummary"]));
  // ESPN sends the group id as a string ("1"), sometimes as a number.
  JsonVariantConst group = team["groups"]["id"];
  s.group = group.is<const char *>() ? (uint32_t) atol(group.as<const char *>()) : (group | 0);
  JsonObjectConst ne = team["nextEvent"][0];
  if (!ne.isNull()) {
    s.event_id = detail::str_or_empty(ne["id"]);
    s.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ne["date"]));
    s.time_tbd = detail::time_tbd(ne["competitions"][0]);
    s.next_final = detail::str_or_empty(ne["competitions"][0]["status"]["type"]["state"]) == "post";
    std::string us = detail::str_or_empty(team["id"]);
    for (JsonObjectConst c : ne["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      std::string id = detail::str_or_empty(c["id"]);
      if (id.empty() || id == us)
        continue;
      s.opp_id = (uint32_t) atol(id.c_str());
      s.opp_abbr = detail::str_or_empty(c["team"]["abbreviation"]);
    }
  }
  s.valid = true;
  out = std::move(s);
  return true;
}

// Games that have not started yet, in the order ESPN lists them (by date).
template<typename TInput>
bool parse_upcoming(TInput &input, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out, bool ranks = false) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_upcoming_filter(filter, ranks);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  out.clear();
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (out.size() >= max)
      break;
    JsonObjectConst comp = ev["competitions"][0];
    if (detail::str_or_empty(comp["status"]["type"]["state"]) != "pre")
      continue;
    Upcoming u;
    u.event_id = detail::str_or_empty(ev["id"]);
    u.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ev["date"]));
    u.time_tbd = detail::time_tbd(comp);
    u.neutral = comp["neutralSite"] | false;
    for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
      uint32_t id = (uint32_t) atol(detail::str_or_empty(c["team"]["id"]).c_str());
      bool home = detail::str_or_empty(c["homeAway"]) == "home";
      if (id == our_team_id) {
        u.home = home;
      } else {
        u.opp_id = id;
        u.opp_abbr = detail::str_or_empty(c["team"]["abbreviation"]);
        u.opp_name = detail::str_or_empty(c["team"]["shortDisplayName"]);
        u.opp_rank = detail::rank_of(c);
      }
    }
    JsonVariantConst tv = comp["broadcasts"][0]["media"]["shortName"];
    u.tv = detail::str_or_empty(tv);
    if (!u.event_id.empty() && u.opp_id != 0)
      out.push_back(u);
  }
  return true;
}

namespace detail {
// The better rank of a game's two sides (rank_of, 0 unranked), 99 when
// neither is ranked.
inline uint8_t best_rank(uint8_t a, uint8_t b) {
  uint8_t rank = 99;
  for (uint8_t r : {a, b})
    if (r != 0 && r < rank)
      rank = r;
  return rank;
}
}  // namespace detail

// Lists the in-progress games in a scoreboard document. With next_kickoff,
// also reports the earliest kickoff among the games not started yet, or 0
// when there is none. A game whose time is still TBD is listed at midnight
// Eastern with timeValid false, and does not count. With finals, also lists
// the games that were played to the end (not postponed or canceled). With
// pending, also lists the games next_kickoff counts (a postponed or
// canceled game is "post", not "pre"): the kPendingKeep best by
// pending_before(), in that order. Finals and pending come with no league
// set: the caller knows which league it asked for.
template<typename TInput, typename TOut>
bool parse_live_games(TInput &input, TOut &out, int64_t *next_kickoff = nullptr,
                      std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_scan_filter(filter, finals != nullptr, pending != nullptr);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (next_kickoff != nullptr)
    *next_kickoff = 0;
  // Kept apart from *pending until the end, so the cap counts this document only.
  std::vector<Pending> later;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    std::string state = detail::str_or_empty(ev["status"]["type"]["state"]);
    bool tbd = detail::time_tbd(ev["competitions"][0]);
    uint32_t away = 0;
    if (finals != nullptr || pending != nullptr)
      for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>())
        if (detail::str_or_empty(c["homeAway"]) != "home")
          away = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
    if (state == "pre" && !tbd) {
      int64_t k = parse_iso8601_z(detail::str_or_empty(ev["date"]));
      if (k > 0 && next_kickoff != nullptr && (*next_kickoff == 0 || k < *next_kickoff))
        *next_kickoff = k;
      GameSnapshot g;
      if (k > 0 && pending != nullptr && away != 0 && detail::snapshot_from_event(ev, away, g)) {
        Pending p;
        p.rank = detail::best_rank(g.team_rank, g.opp_rank);
        p.kickoff_epoch = k;
        p.event_id = std::move(g.event_id);
        p.away_id = g.team_id;
        p.home_id = g.opp_id;
        p.away_abbr = std::move(g.team_abbr);
        p.home_abbr = std::move(g.opp_abbr);
        p.away_color = std::move(g.team_color);
        p.home_color = std::move(g.opp_color);
        p.away_record = std::move(g.team_record);
        p.home_record = std::move(g.opp_record);
        p.away_rank = g.team_rank;
        p.home_rank = g.opp_rank;
        p.tv = std::move(g.tv);
        // Sorted as it goes and never more than kPendingKeep: a college
        // Saturday has sixty games to come.
        auto at = std::upper_bound(later.begin(), later.end(), p, pending_before);
        if (later.size() < kPendingKeep || at != later.end()) {
          later.insert(at, std::move(p));
          if (later.size() > kPendingKeep)
            later.pop_back();
        }
      }
    }
    if (state == "post" && finals != nullptr && (ev["status"]["type"]["completed"] | false)) {
      GameSnapshot g;
      if (away != 0 && detail::snapshot_from_event(ev, away, g)) {
        Final fin;
        fin.rank = detail::best_rank(g.team_rank, g.opp_rank);
        fin.kickoff_epoch = g.kickoff_epoch;
        fin.detail = g.short_detail;
        fin.away_id = g.team_id;
        fin.home_id = g.opp_id;
        fin.away_score = g.team_score;
        fin.home_score = g.opp_score;
        fin.away_abbr = g.team_abbr;
        fin.home_abbr = g.opp_abbr;
        fin.away_color = g.team_color;
        fin.home_color = g.opp_color;
        fin.away_record = g.team_record;
        fin.home_record = g.opp_record;
        fin.away_rank = g.team_rank;
        fin.home_rank = g.opp_rank;
        finals->push_back(std::move(fin));
      }
      continue;
    }
    if (state != "in")
      continue;
    LiveGame g;
    g.event_id = detail::str_or_empty(ev["id"]);
    for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      bool home = detail::str_or_empty(c["homeAway"]) == "home";
      std::string abbr = detail::str_or_empty(c["team"]["abbreviation"]);
      if (home) {
        g.home_abbr = abbr;
        g.home_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
        g.group = (uint32_t) atol(detail::str_or_empty(c["team"]["conferenceId"]).c_str());
      } else {
        g.away_abbr = abbr;
        g.away_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
      }
    }
    if (!g.event_id.empty() && g.away_id != 0)
      out.push_back(g);
  }
  if (pending != nullptr)
    for (auto &p : later)
      pending->push_back(std::move(p));
  return true;
}

// The first game in a day's scoreboard with our team that is not over yet
// (game 2 of a doubleheader after game 1's final). skip_event is a game the
// panel already saw finish, which the scoreboard may still list as live.
// Returns 1 and fills event_id, kickoff_epoch and time_tbd, 0 when the team
// does not play that day, -1 when the document does not parse.
template<typename TInput>
int parse_team_game(TInput &input, uint32_t our_team_id, const std::string &skip_event, Schedule &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_team_game_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (detail::str_or_empty(ev["status"]["type"]["state"]) == "post")
      continue;
    if (!skip_event.empty() && detail::str_or_empty(ev["id"]) == skip_event)
      continue;
    for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      if ((uint32_t) atol(detail::str_or_empty(c["id"]).c_str()) != our_team_id)
        continue;
      out.event_id = detail::str_or_empty(ev["id"]);
      out.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ev["date"]));
      out.time_tbd = detail::time_tbd(ev["competitions"][0]);
      return out.event_id.empty() ? 0 : 1;
    }
  }
  return 0;
}

// Parses a single-event document (event_url): the root object is the event.
// ESPN serves it with the same fields as the event's entry in the day's
// scoreboard, at a tenth of the size or less. Returns false on a JSON error,
// when our team is not in it, or when event_id is given and the document is
// some other game.
// players: the favorite players in the game's league (players.h); with
// any, the read keeps their leader lines and the last play's athletes too.
template<typename TInput>
bool parse_event(TInput &input, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                 const std::string &event_id = "", const PlayerWatch *players = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_game_filter(filter.to<JsonObject>(), sport);
  const bool watch = players != nullptr && players->n > 0;
  if (watch)
    detail::fill_player_filter(filter.as<JsonObject>(), sport);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (!event_id.empty() && detail::str_or_empty(doc["id"]) != event_id)
    return false;
  if (!detail::game_from_event(doc.as<JsonObjectConst>(), sport, our_team_id, out))
    return false;
  if (watch)
    detail::players_from_event(doc.as<JsonObjectConst>(), *players, sport, out);
  return true;
}

// A golf tour's scoreboard header (header_url). 1: the tour's event in
// out (sport GOLF), the primary one when it lists two. 0: the tour lists
// none. -1: not a header document.
template<typename TInput> int parse_golf(TInput &input, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_golf_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  return detail::golf_from_doc(doc.as<JsonObjectConst>(), out);
}

// A season calendar (golf_calendar_url). 1: the first event by start that
// started less than a day before now or starts after it, and isn't
// after_event, in out. 0: none ahead. -1: not a scoreboard document.
template<typename TInput>
int parse_golf_calendar(TInput &input, int64_t now_epoch, const std::string &after_event, GolfNext &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_golf_calendar_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  return detail::golf_next_from_doc(doc.as<JsonObjectConst>(), now_epoch, after_event, out);
}

// A racing series' scoreboard header (header_url). 1: its race weekend in
// out (sport RACING): the one with a session on track, else the one whose
// last session with results ran latest, else the next. 0: the series lists
// none. -1: not a header document.
template<typename TInput> int parse_racing(TInput &input, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_racing_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  return detail::racing_from_doc(doc.as<JsonObjectConst>(), out);
}

// A racing series' calendar (golf_calendar_url). 1: the first race weekend by
// start that isn't over (its race started less than six hours ago), isn't
// after_event and doesn't start before after_start (the start of the event
// just over), in out. 0: none ahead. -1: not a scoreboard document.
template<typename TInput>
int parse_racing_calendar(TInput &input, int64_t now_epoch, const std::string &after_event, int64_t after_start,
                          GolfNext &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_racing_calendar_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  return detail::racing_next_from_doc(doc.as<JsonObjectConst>(), now_epoch, after_event, after_start, out);
}

// Parses a scoreboard document and extracts the event with `event_id`.
// Returns false on a JSON error or when the event is not in the document.
template<typename TInput>
bool parse_scoreboard(TInput &input, const std::string &event_id, uint32_t our_team_id, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_scoreboard_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (detail::str_or_empty(ev["id"]) != event_id)
      continue;
    return detail::snapshot_from_event(ev, our_team_id, out);
  }
  return false;
}

}  // namespace espn
