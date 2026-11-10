// Follow pin, memory only; no ESPHome includes.
#pragma once

#include <cstdint>
#include <string>

#include "espn_parse.h"
#include "set_request.h"

namespace espn {

// ESPN can drop a game or never end it.
constexpr int64_t kFollowUnreadSeconds = 30 * 60;
constexpr int64_t kFollowLongestGameSeconds = 8 * 3600;

struct FollowPin {
  bool on{false};
  std::string event_id;    // the game it holds
  int64_t since{0};        // when follow=1 pinned it
  int64_t final_epoch{0};  // first seen as a final; 0 until then
};

// Favorite teams without a favorite runs as My team.
inline bool follow_mode_ok(uint8_t mode, bool favorites_set) {
  switch ((Mode) mode) {
    case Mode::LIVE_NFL:
    case Mode::LIVE_NCAA:
    case Mode::LIVE_ANY:
    case Mode::LIVE:
      return true;
    case Mode::FAVORITES:
      return favorites_set;
    default:
      return false;
  }
}

// Live games leaves a final at its first look.
inline int64_t follow_after_final_s(bool favorites, int64_t release_s) { return favorites ? release_s : 0; }

// picked: the mode chose g, not a borrowed card.
inline bool follow_start(FollowPin &p, bool mode_ok, bool picked, const GameSnapshot &g, int64_t now) {
  if (!mode_ok || !picked || !g.valid || g.event_id.empty() || is_board_sport(g.sport))
    return false;
  if (p.on && p.event_id == g.event_id)
    return true;  // already following it
  if (g.state == GameState::POST)
    return false;
  p = FollowPin{};
  p.on = true;
  p.event_id = g.event_id;
  p.since = now;
  return true;
}

// The device page shows the switch only when true.
inline bool follow_can_start(const FollowPin &p, bool mode_ok, bool picked, const GameSnapshot &g) {
  FollowPin copy = p;
  return follow_start(copy, mode_ok, picked, g, 0);
}

// A TBD kickoff is a midnight placeholder: use day's end.
inline int64_t follow_start_epoch(const FollowPin &p, const GameSnapshot &g) {
  int64_t start = g.time_tbd ? g.kickoff_epoch + 24 * 3600 : g.kickoff_epoch;
  return start > p.since ? start : p.since;
}

// Call wherever the mode would move on.
inline bool follow_holds(FollowPin &p, const GameSnapshot &g, int64_t now, int64_t after_final_s, int64_t unread_s) {
  if (!p.on)
    return false;
  bool hold;
  if (g.event_id != p.event_id) {
    hold = false;
  } else if (unread_s >= kFollowUnreadSeconds && now - p.since >= kFollowUnreadSeconds) {
    hold = false;
  } else if (g.state != GameState::POST) {
    p.final_epoch = 0;  // ESPN took the final back: count from the next one
    hold = now - follow_start_epoch(p, g) < kFollowLongestGameSeconds;
  } else {
    if (p.final_epoch == 0)
      p.final_epoch = now;
    hold = now - p.final_epoch < after_final_s;
  }
  if (!hold)
    p = FollowPin{};
  return hold;
}

}  // namespace espn
