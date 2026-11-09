// Minor league baseball from the MLB Stats API (statsapi.mlb.com), the one
// league that doesn't come from ESPN. Every read is its schedule endpoint,
// cut down on the server with a fields= list and again by an ArduinoJson
// filter, and each turns into the same Schedule, Upcoming, GameSnapshot,
// LiveGame, Final and Pending the ESPN parsers give, so the board, the ticker,
// Live games and the finals rotation draw it like MLB. Pure, no ESPHome
// includes, so the host tests compile it directly.
//
// Reads, measured 2026-11-08 (fixtures/README.md):
//   team   one team's games from yesterday to ten days out, all four levels:
//          about 4 KB in season, 12 bytes between seasons
//   far    the team's games 11 to 240 days out, only when the team read has
//          none: the next season's opener, about 10 KB (no teams, just the
//          game ids and dates)
//   game   one game with its line score, about 1.4 KB: the poll while the
//          board shows it. The line score endpoint (/game/<pk>/linescore) is
//          3.4 KB and has no status or team ids, so this one read does both.
//   day    a day's games at all four levels, for Live games and the finals
//          rotation: up to about 25 KB on a full day of 60 games
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

static constexpr const char *kMlbStatsRoot = "https://statsapi.mlb.com/";

// A read that goes to the MLB Stats API, not ESPN: its failures and refusals
// are counted on their own (GamedayComponent::open_).
inline bool is_mlb_url(const std::string &url) {
  return url.compare(0, strlen(kMlbStatsRoot), kMlbStatsRoot) == 0;
}

// Team::group for a minor league team: its level's MLB Stats sport id.
static const uint32_t kMilbTripleA = 11, kMilbDoubleA = 12, kMilbHighA = 13, kMilbSingleA = 14;
// "Triple-A", "Double-A", "High-A", "Single-A"; "" for anything else.
const char *milb_level_name(uint32_t level);

// The team read: its games from yesterday to ten days ahead (Eastern dates,
// turning over at 4 AM like scan_url), at all four levels so a team that
// changed level since teams.h was made is still found.
std::string mlb_team_url(uint32_t team_id, int64_t now_epoch);
// The next season: 11 to 240 days ahead, ids and dates only. The API takes
// more than 45 days only for one level, so this one asks for the team's.
std::string mlb_far_url(uint32_t team_id, uint32_t level, int64_t now_epoch);
// One game (its id is the gamePk) with the line score and probable pitchers.
std::string mlb_game_url(const std::string &game_pk);
// Every game of a day at the four levels (the day day_epoch falls in, as
// scan_url counts it).
std::string mlb_day_url(int64_t day_epoch);
// The team's logo, a 64 px PNG on MLB's image server.
std::string mlb_logo_url(uint32_t team_id);

// "Justyn-Henry Malloy" -> "J. Malloy", "Homer Bush Jr." -> "H. Bush Jr.",
// accents dropped: ESPN's short names, which the ticker's pitcher, batter
// and probables use.
std::string mlb_short_name(const std::string &full);
// "1st", "2nd", "3rd", "4th", "11th", "21st".
std::string mlb_ordinal(int n);

// A game's state from its codedGameState and statusCode.
enum class MlbState : uint8_t { PRE, LIVE, FINAL, OFF };  // OFF: postponed, canceled or suspended
MlbState mlb_state(const std::string &coded);

namespace detail {
void fill_mlb_team_filter(JsonDocument &f);
void fill_mlb_game_filter(JsonDocument &f);
void fill_mlb_day_filter(JsonDocument &f);
int mlb_team_from_doc(JsonObjectConst root, uint32_t our_id, int64_t now, Schedule &out,
                      std::vector<Upcoming> *upcoming, size_t max, Schedule *last_final = nullptr);
bool mlb_game_from_doc(JsonObjectConst root, uint32_t our_id, int64_t near_epoch, GameSnapshot &out);
bool mlb_day_from_doc(JsonObjectConst root, std::vector<LiveGame> &out, int64_t *next_kickoff,
                      std::vector<Final> *finals, std::vector<Pending> *pending);
}  // namespace detail

// The team read or the far read. 1: the first game that isn't over (in
// progress, or not started and not left over from a past day) in out.event_id,
// kickoff_epoch, time_tbd and, from the team read, the opponent and our
// record. 0: none listed. -1: not a schedule document. With upcoming, also
// the games not started yet in date order, up to max, for the Up next list.
// A postponed game is listed again on its new date with the same id; only
// the entry that isn't over counts. An entry whose start is days away from
// the date it is listed under (a placeholder MLB never played) is skipped.
// Out is valid with 0 or 1. With last_final, also the team's latest game
// played to the end before the one in out (event_id, kickoff_epoch and
// time_tbd; not valid with none): My team today keeps it until midnight
// after a restart (gameday.cpp's team read).
template<typename TInput>
int parse_mlb_team(TInput &input, uint32_t our_id, int64_t now, Schedule &out,
                   std::vector<Upcoming> *upcoming = nullptr, size_t max = 0, Schedule *last_final = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_mlb_team_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return -1;
  return detail::mlb_team_from_doc(doc.as<JsonObjectConst>(), our_id, now, out, upcoming, max, last_final);
}

// The game read, as a baseball GameSnapshot around our_id. The API lists a
// postponed or suspended game once per date it was on; the entry whose start
// is nearest near_epoch (the kickoff the board picked it for) is the one
// read, so a game postponed today reads as Postponed rather than as its new
// date. False when it doesn't parse or our team isn't in it.
template<typename TInput>
bool parse_mlb_game(TInput &input, uint32_t our_id, int64_t near_epoch, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_mlb_game_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return false;
  return detail::mlb_game_from_doc(doc.as<JsonObjectConst>(), our_id, near_epoch, out);
}

// The day read, the same way parse_live_games reads an ESPN scoreboard: the
// games in progress, the earliest start still to come, and with finals and
// pending the games played to the end (the first kRecapCap, all the finals
// rotation keeps of a day) and the kPendingKeep first to come. Finals and
// pending come with no league set.
template<typename TInput>
bool parse_mlb_day(TInput &input, std::vector<LiveGame> &out, int64_t *next_kickoff = nullptr,
                   std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_mlb_day_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return false;
  return detail::mlb_day_from_doc(doc.as<JsonObjectConst>(), out, next_kickoff, finals, pending);
}

}  // namespace espn
