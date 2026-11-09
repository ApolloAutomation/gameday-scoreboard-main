// Minor league hockey, the AHL and the ECHL, from HockeyTech's feed
// (lscluster.hockeytech.com), the leagues' own stats service. Each read turns
// into the same Schedule, Upcoming, GameSnapshot, LiveGame, Final and Pending
// the ESPN parsers give, so the hockey board, the ticker, Live games and the
// finals rotation draw it like the NHL. Everything HockeyTech is in this file
// and source_hockeytech.cpp, apart from the calls that hand an AHL or ECHL
// team to it (GamedayComponent's fetch functions). Pure, no ESPHome
// includes, so the host tests compile it directly.
//
// The feed wants each league's public site key and client code, the ones
// the leagues' own sites send. They are gameday-common.yaml substitutions
// (the gameday component's hockeytech: block), so they can change without a
// code edit; until they are set, a league's reads fail.
//
// Reads, measured 2026-11-09 (fixtures/README.md):
//   team   the scorebar for one team: its last game and its next five, about
//          1.8 KB a game, 11 KB in all. Up next comes from it.
//   game   the game centre's clock for one game, about 1.9 KB: the poll while
//          the board shows it. The feed's server keeps it a minute at most
//          (the scorebar four).
//   day    one day's games for Live games and the finals rotation, about
//          6.5 KB a game played (each has its goals) and 2.7 KB a game to
//          come: up to about 105 KB on a full day.
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

static constexpr const char *kHockeyTechRoot = "https://lscluster.hockeytech.com/";

// A read that goes to HockeyTech, not ESPN: its failures and refusals are
// counted on their own (GamedayComponent::open_).
inline bool is_ht_url(const std::string &url) {
  return url.compare(0, strlen(kHockeyTechRoot), kHockeyTechRoot) == 0;
}

// The league's site key and client code. Pass string literals (the YAML's
// generated code does): only the pointers are kept. Any other league is
// ignored.
void ht_set_feed(League league, const char *key, const char *client_code);
// The client code, also the league's folder on the logo server ("ahl"); ""
// until it is set.
const char *ht_client(League league);

// teams.h group for an AHL or ECHL team: the season its logo file is named
// for (402_94.png is 94; 0 for 440.png), plus this bit when the logo server
// has no copy small enough for the panel's logo cache. The panel draws that
// team's abbreviation badge instead (badge.h); the device page shows the
// full-size logo.
static const uint32_t kHtNoSmallLogo = 1u << 16;

// The team read: the scorebar for one team, its last game before today and
// its next five from today on (the feed counts games, not days, around the
// league's own today).
std::string ht_team_url(League league, uint32_t team_id);
// The game read: one game's clock from the game centre.
std::string ht_game_url(League league, const std::string &game_id);
// Every game of a day (the day day_epoch falls in, as scan_url counts it:
// Eastern, turning over at 4 AM).
std::string ht_day_url(League league, int64_t day_epoch);
// The team's 50 px logo (64 px for some), "" for a team with no copy small
// enough (kHtNoSmallLogo).
std::string ht_logo_url(League league, uint32_t team_id);

// "2026-11-07T19:00:00-05:00" to Unix seconds; a time with no offset is UTC.
int64_t ht_iso_epoch(const std::string &iso);
// A day read's start: its local date ("2026-11-07") and time ("19:00:00") in
// a North American zone ("America/Chicago"), with daylight saving time as
// the US and Canada keep it, and British Columbia, Alberta and Manitoba on
// summer time all year from 1 November 2026. An unknown zone is Eastern.
int64_t ht_local_epoch(const std::string &date, const std::string &time, const std::string &zone);

// A game's state from the feed's status code (1 not started, 2 in progress,
// 3 and 4 final, 8 postponed, 9 suspended), the words beside it for any
// other code, and the game centre's started flag.
enum class HtState : uint8_t { PRE, LIVE, FINAL, OFF };  // OFF: postponed, canceled or suspended
HtState ht_state(const std::string &code, const std::string &words, bool started = false);

namespace detail {
void fill_ht_team_filter(JsonDocument &f);
void fill_ht_game_filter(JsonDocument &f);
void fill_ht_day_filter(JsonDocument &f);
int ht_team_from_doc(JsonObjectConst root, uint32_t our_id, int64_t now, Schedule &out,
                     std::vector<Upcoming> *upcoming, size_t max, Schedule *last_final = nullptr);
bool ht_game_from_doc(JsonObjectConst root, League league, uint32_t our_id, GameSnapshot &out);
bool ht_day_from_doc(JsonObjectConst root, std::vector<LiveGame> &out, int64_t *next_kickoff,
                     std::vector<Final> *finals, std::vector<Pending> *pending);
}  // namespace detail

// The team read. 1: the first game that isn't over (in progress, or not
// started and not left over from a past day) in out.event_id, kickoff_epoch,
// time_tbd, the opponent and both records. 0: none listed (between seasons).
// -1: not a scorebar. With upcoming, also the games not started yet in date
// order, up to max, for the Up next list. Out is valid with 0 or 1. With
// last_final, also the team's latest game played to the end before the one
// in out (event_id, kickoff_epoch, time_tbd, the opponent and both records;
// not valid with none): My team today keeps it until midnight after a
// restart (gameday.cpp's team read).
template<typename TInput>
int parse_ht_team(TInput &input, uint32_t our_id, int64_t now, Schedule &out,
                  std::vector<Upcoming> *upcoming = nullptr, size_t max = 0, Schedule *last_final = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_ht_team_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return -1;
  return detail::ht_team_from_doc(doc.as<JsonObjectConst>(), our_id, now, out, upcoming, max, last_final);
}

// The game read, as a hockey GameSnapshot around our_id: the period, the
// clock, intermissions, overtime, the shootout and the final. The clock has
// no records, no start time placeholder and no game id of its own; the
// caller fills those from the schedule. False when it doesn't parse or our
// team isn't in it.
template<typename TInput>
bool parse_ht_game(TInput &input, League league, uint32_t our_id, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_ht_game_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return false;
  return detail::ht_game_from_doc(doc.as<JsonObjectConst>(), league, our_id, out);
}

// What the game read leaves to the schedule it was picked from (the team
// read's, in My team and Favorite teams): the game id when the clock gave
// none, the midnight placeholder of a game with no time yet, our record, and
// the opponent's when it is the same opponent.
void ht_fill_from_schedule(GameSnapshot &g, const Schedule &s);

// The day read, the same way parse_live_games reads an ESPN scoreboard: the
// games in progress, the earliest start still to come, and with finals and
// pending the games played to the end (the first kRecapCap) and the
// kPendingKeep first to come. Finals and pending come with no league set.
template<typename TInput>
bool parse_ht_day(TInput &input, std::vector<LiveGame> &out, int64_t *next_kickoff = nullptr,
                  std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_ht_day_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return false;
  return detail::ht_day_from_doc(doc.as<JsonObjectConst>(), out, next_kickoff, finals, pending);
}

}  // namespace espn
