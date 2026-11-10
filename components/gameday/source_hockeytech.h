// No ESPHome includes; read sizes are in fixtures/README.md.
#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

static constexpr const char *kHockeyTechRoot = "https://lscluster.hockeytech.com/";

// Its failures and refusals are counted apart from ESPN's.
inline bool is_ht_url(const std::string &url) {
  return url.compare(0, strlen(kHockeyTechRoot), kHockeyTechRoot) == 0;
}

// Pass string literals: only the pointers are kept.
void ht_set_feed(League league, const char *key, const char *client_code);
// Also the logo server folder ("ahl"); "" until set.
const char *ht_client(League league);

// Set when no logo fits the cache: draw the badge.
static const uint32_t kHtNoSmallLogo = 1u << 16;

// The feed counts games, not days, around its today.
std::string ht_team_url(League league, uint32_t team_id);
// The server caches it a minute at most.
std::string ht_game_url(League league, const std::string &game_id);
// day_epoch's day as scan_url counts it.
std::string ht_day_url(League league, int64_t day_epoch);
// 50 px, some 64; "" when kHtNoSmallLogo is set.
std::string ht_logo_url(League league, uint32_t team_id);

// A time with no offset is UTC.
int64_t ht_iso_epoch(const std::string &iso);
// US/Canada DST; unknown zones are Eastern.
int64_t ht_local_epoch(const std::string &date, const std::string &time, const std::string &zone);

// Codes: 1 pre, 2 live, 3-4 final, 8-9 off.
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

// Returns 1 found, 0 none, -1 not a scorebar.
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

// The caller fills records, placeholder and id from the schedule.
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

// The opponent's record only when it's the same opponent.
void ht_fill_from_schedule(GameSnapshot &g, const Schedule &s);

// The away side is ours, as the board draws it.
inline void ht_live_records(Schedule &s, const LiveGame &g) {
  s.opp_id = g.home_id;
  s.team_record = g.away_record;
  s.opp_record = g.home_record;
}

// The boot cache keeps no records; take the read's.
bool ht_take_records(Schedule &into, const Schedule &read);

// Finals and pending come with no league set.
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
