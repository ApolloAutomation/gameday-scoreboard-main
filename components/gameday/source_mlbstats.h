// No ESPHome includes; read sizes are in fixtures/README.md.
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

static constexpr const char *kMlbStatsRoot = "https://statsapi.mlb.com/";

// Its failures and refusals are counted apart from ESPN's.
inline bool is_mlb_url(const std::string &url) {
  return url.compare(0, strlen(kMlbStatsRoot), kMlbStatsRoot) == 0;
}

// Team::group holds the level's MLB Stats sport id.
static const uint32_t kMilbTripleA = 11, kMilbDoubleA = 12, kMilbHighA = 13, kMilbSingleA = 14;
const char *milb_level_name(uint32_t level);

// All four levels, in case the team changed level.
std::string mlb_team_url(uint32_t team_id, int64_t now_epoch);
// The API allows over 45 days for one level only.
std::string mlb_far_url(uint32_t team_id, uint32_t level, int64_t now_epoch);
// Not /linescore: it lacks status and team ids.
std::string mlb_game_url(const std::string &game_pk);
// day_epoch's day as scan_url counts it.
std::string mlb_day_url(int64_t day_epoch);
// A 64 px PNG on MLB's image server.
std::string mlb_logo_url(uint32_t team_id);

// Matches ESPN short names: "J. Malloy", accents dropped.
std::string mlb_short_name(const std::string &full);
// "1st", "2nd", "3rd", "4th", "11th", "21st".
std::string mlb_ordinal(int n);

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

// Returns 1 found, 0 none, -1 not a schedule.
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

// Postponed games list once per date; read the nearest near_epoch.
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

// Finals and pending come with no league set.
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
