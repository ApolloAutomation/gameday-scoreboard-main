// Follow this game: the device page's pin on the game on the board in Live
// games or Favorite teams. While it holds, the mode stops moving to other
// games (Live games' switch and rescan, the favorites' playlist and lock-ons)
// until the game's final has been up for the mode's own time after a final,
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

struct FollowPin {
  bool on{false};
  std::string event_id;    // the game it holds
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

// follow=1: pins g, the game on the board. picked: the mode put g up itself
// (Live games' pick, a favorite's card), not the saved team's card Live games
// borrows with nothing live, nor the finals rotation. False, with the pin
// left as it was, when there is nothing to follow.
inline bool follow_start(FollowPin &p, bool mode_ok, bool picked, const GameSnapshot &g, int64_t now) {
  if (!mode_ok || !picked || !g.valid || g.event_id.empty() || is_board_sport(g.sport))
    return false;
  if (p.on && p.event_id == g.event_id)
    return true;  // already following it
  p = FollowPin{};
  p.on = true;
  p.event_id = g.event_id;
  p.final_epoch = g.state == GameState::POST ? now : 0;  // a final counts from the tap
  return true;
}

// Run wherever the mode would move on: true while the pin holds g, the game
// on the board, so the mode stays on it. Lets go, and says false, once the
// board has another game, or g's final has been up for after_final_s.
inline bool follow_holds(FollowPin &p, const GameSnapshot &g, int64_t now, int64_t after_final_s) {
  if (!p.on)
    return false;
  if (g.event_id != p.event_id) {
    p = FollowPin{};
    return false;
  }
  if (g.state != GameState::POST) {
    p.final_epoch = 0;  // ESPN took the final back: count from the next one
    return true;
  }
  if (p.final_epoch == 0)
    p.final_epoch = now;
  if (now - p.final_epoch < after_final_s)
    return true;
  p = FollowPin{};
  return false;
}

}  // namespace espn
