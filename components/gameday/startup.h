// Startup helpers: the saved team info a boot can start from, and how long
// the boot screen waits for the first scoreboard. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "espn_parse.h"

namespace espn {

// "Switch every" in Live games and "Alternate every" in Favorite teams, one
// setting: 30 seconds, or whole minutes from 1 to 30. It is saved as minutes
// (Prefs2; a saved value outside the range is treated as corrupt at boot and
// replaced with the default) plus a flag for 30 seconds (Prefs8), which counts
// only next to 1 minute, the minute it is saved with. Firmware from before
// 30 seconds reads such a panel as 1 minute, and one minute set there clears
// it.
static const int kRotateMinMinutes = 1;
static const int kRotateMaxMinutes = 30;
static const int kRotateDefaultMinutes = 1;
static const int kRotateHalfSeconds = 30;

inline bool rotate_minutes_valid(int minutes) {
  return minutes >= kRotateMinMinutes && minutes <= kRotateMaxMinutes;
}

inline int clamp_rotate_minutes(int minutes) {
  if (minutes < kRotateMinMinutes)
    return kRotateMinMinutes;
  if (minutes > kRotateMaxMinutes)
    return kRotateMaxMinutes;
  return minutes;
}

// The setting in seconds: 30, or 60 to 1800 in whole minutes.
inline bool rotate_seconds_valid(int seconds) {
  return seconds == kRotateHalfSeconds || (seconds % 60 == 0 && rotate_minutes_valid(seconds / 60));
}

// Any number of seconds to the setting: under a minute is 30 seconds, a
// minute or more is whole minutes, rounded down, at most 30.
inline int clamp_rotate_seconds(int seconds) {
  return seconds < 60 ? kRotateHalfSeconds : clamp_rotate_minutes(seconds / 60) * 60;
}

// The minutes saved for a setting, which are also the state's "rotate" for
// device pages and apps from before 30 seconds: 30 seconds is 1 minute there.
inline int rotate_minutes_of(int seconds) {
  const int s = clamp_rotate_seconds(seconds);
  return s < 60 ? kRotateMinMinutes : s / 60;
}

// The setting from what is saved: the minutes and the 30 second flag.
inline int rotate_saved_seconds(int minutes, bool half) {
  return half && minutes == kRotateMinMinutes ? kRotateHalfSeconds : clamp_rotate_minutes(minutes) * 60;
}

// A Live games turn is over once its clock, started_ms, has run the setting
// (Favorite teams times its own in pick_favorite, favorites.h). 0 is a clock
// that hasn't started.
inline bool rotate_turn_over(uint32_t now_ms, uint32_t started_ms, int seconds) {
  return started_ms != 0 && now_ms - started_ms >= (uint32_t) seconds * 1000u;
}

// The turn's clock once another game's big play (watch.h) gives the board
// back after away_ms: the time away doesn't count, so the game comes back
// for what was left of its turn. A turn that was over before stays over.
inline uint32_t rotate_turn_resume(uint32_t started_ms, uint32_t now_ms, uint32_t away_ms) {
  if (started_ms == 0)
    return 0;
  const uint32_t ran = now_ms - started_ms;
  const uint32_t out = started_ms + (away_ms < ran ? away_ms : ran);
  return out == 0 ? 1 : out;
}

// The same for Favorite teams, whose clock is the epoch second the card went up.
inline int64_t rotate_since_resume(int64_t since, int64_t now, int64_t away_s) {
  if (away_s <= 0)
    return since;
  return since + away_s < now ? since + away_s : (since < now ? now : since);
}

// The team endpoint's answer for the saved team, kept in flash so a boot can
// go straight to the scoreboard instead of reading the team endpoint (about
// 23 KB) first. What the scoreboard request needs, and the team's seed and
// place for the Standing setting, which only the team endpoint has. A blob of
// another size (an older firmware's) doesn't load: one team read rebuilds it.
struct TeamCache {
  uint8_t league;
  uint32_t team_id;
  char event_id[16];
  int64_t kickoff_epoch;
  uint32_t group;
  int64_t saved_epoch;
  uint8_t seed;
  char place[16];  // place_short(), cut to 15 characters
} __attribute__((packed));

// Older than this, the team endpoint is read again first.
static const int64_t TEAM_CACHE_MAX_AGE = 24 * 3600;
// A cached game that kicked off longer ago than this is over: read again.
static const int64_t TEAM_CACHE_GAME_OVER = 4 * 3600;

// Fills `out` from the schedule just read for this team. False when there is
// nothing worth keeping: no next game, or an event id too long for the record.
// A cup game, or one with no kickoff time yet, saves a blank record, which
// never loads. The record has no room for the cup or the TBD flag, and an
// older game must not load in its place.
inline bool team_cache_save(const Schedule &s, League league, uint32_t team_id, int64_t now_epoch,
                            TeamCache &out) {
  if (s.valid && (s.comp_slug != nullptr || s.time_tbd)) {
    out = TeamCache{};
    return true;
  }
  if (!s.valid || s.event_id.empty() || s.event_id.size() >= sizeof(out.event_id) || now_epoch <= 0)
    return false;
  out = TeamCache{};
  out.league = (uint8_t) league;
  out.team_id = team_id;
  memcpy(out.event_id, s.event_id.c_str(), s.event_id.size());
  out.kickoff_epoch = s.kickoff_epoch;
  out.group = s.group;
  out.saved_epoch = now_epoch;
  out.seed = s.seed;
  memcpy(out.place, s.place.c_str(), s.place.size() < sizeof(out.place) ? s.place.size() : sizeof(out.place) - 1);
  return true;
}

// The cached schedule, only while it can still be right: the same team,
// saved in the last day, and a game that has not been over for hours.
inline bool team_cache_load(const TeamCache &c, League league, uint32_t team_id, int64_t now_epoch,
                            Schedule &out) {
  char id[sizeof(c.event_id) + 1];
  memcpy(id, c.event_id, sizeof(c.event_id));
  id[sizeof(c.event_id)] = '\0';
  if (c.league != (uint8_t) league || c.team_id != team_id || id[0] == '\0')
    return false;
  int64_t age = now_epoch - c.saved_epoch;
  if (age < 0 || age > TEAM_CACHE_MAX_AGE)
    return false;
  if (c.kickoff_epoch < now_epoch - TEAM_CACHE_GAME_OVER)
    return false;
  out = Schedule{};
  out.valid = true;
  out.event_id = id;
  out.kickoff_epoch = c.kickoff_epoch;
  out.group = c.group;
  out.seed = c.seed;
  char place[sizeof(c.place) + 1];
  memcpy(place, c.place, sizeof(c.place));
  place[sizeof(c.place)] = '\0';
  out.place = place;
  adopt_league(out, league);
  return true;
}

// Once something asks the boot screen to hide, it stays up until the first
// real scoreboard is ready, and never longer than this after that ask. Then
// it hides over whatever the board has.
static const uint32_t BOOT_HOLD_CAP_MS = 10 * 1000;

// since_ms is millis() when the hold started; 0 means no hold is running.
// Unsigned subtraction keeps the age right across the millis() wrap.
inline bool boot_hold(bool board_ready, uint32_t since_ms, uint32_t now_ms) {
  return !board_ready && since_ms != 0 && (uint32_t) (now_ms - since_ms) < BOOT_HOLD_CAP_MS;
}

}  // namespace espn
