// Follow this game: the device page's pin on the game on the board in Live
// games or Favorite teams. While it holds, the mode stops moving to other
// games (Live games' switch and rescan, the favorites' playlist and lock-ons)
// until the game ends and its final has been up for the mode's own time,
// then it lets go by itself. Memory only: a restart lets it go, and so does a
// change to what the mode follows. My team follows one team already, so it
// has no pin.
// Pure logic on epoch seconds, no ESPHome includes, so the host tests cover
// the rules.
#pragma once

#include <cstdint>
#include <string>

#include "espn_parse.h"
#include "set_request.h"

namespace espn {

// A game that never finishes would hold the pin for good, where Switch every
// moved past it: ESPN stops answering for it (the event is gone), or keeps it
// in progress long after it ended. The pin lets go after half an hour with no
// good read, as long as the longest Switch every, or 8 hours past the start.
constexpr int64_t kFollowUnreadSeconds = 30 * 60;
constexpr int64_t kFollowLongestGameSeconds = 8 * 3600;

struct FollowPin {
  bool on{false};
  std::string event_id;    // the game it holds
  int64_t since{0};        // when follow=1 pinned it
  int64_t final_epoch{0};  // when the panel first had that game as a final, 0 while it isn't one
};

// Can the saved mode follow one game? Live games (and the football-only live
// modes kept from before) and Favorite teams with a favorite set, which
// without one runs as My team.
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

// How long the pin keeps a final up: After the final in Favorite teams.
// Live games moves on at its first look after a final, pin or no pin.
inline int64_t follow_after_final_s(bool favorites, int64_t release_s) { return favorites ? release_s : 0; }

// follow=1: pins g, the game on the board. picked: the mode put g up itself
// (Live games' pick, a favorite's card), not the saved team's card Live games
// borrows with nothing live, nor the finals rotation. False, with the pin
// left as it was, when there is nothing to follow. A final is over already,
// so it takes a pin only while it still has one.
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

// Whether follow=1 would pin g now (follow_start), the pin left as it is:
// the device page shows the switch only then.
inline bool follow_can_start(const FollowPin &p, bool mode_ok, bool picked, const GameSnapshot &g) {
  FollowPin copy = p;
  return follow_start(copy, mode_ok, picked, g, 0);
}

// When g started, for the 8 hour limit: not before the pin, and for a game
// with no start time yet (a midnight placeholder), the end of its day.
inline int64_t follow_start_epoch(const FollowPin &p, const GameSnapshot &g) {
  int64_t start = g.time_tbd ? g.kickoff_epoch + 24 * 3600 : g.kickoff_epoch;
  return start > p.since ? start : p.since;
}

// Run wherever the mode would move on: true while the pin holds g, the game
// on the board, so the mode stays on it. unread_s: the seconds since the last
// good read. Lets go, and says false, once the board has another game, g's
// final has been up for after_final_s, or g never finishes (the limits above,
// the unread one counted from the pin at the earliest).
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
