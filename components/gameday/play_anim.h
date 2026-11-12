// Which play animation a big play gets
// An unknown play gets none, never a guess
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "alerts.h"

namespace espn {

enum class AnimKind : uint8_t { NONE, TD_PASS, TD_RUN, TD_RETURN, FIELD_GOAL, HOME_RUN, SOCCER_GOAL, HOCKEY_GOAL };
constexpr int kAnimKinds = 8;

// AnimArgs.flags bits
constexpr uint8_t kAnimMirror = 1;  // the board's right team scored
constexpr uint8_t kAnimShort = 2;
constexpr uint8_t kAnimDeep = 4;

struct AnimArgs {
  AnimKind kind{AnimKind::NONE};
  int16_t dist{0};  // yards; 0 unknown
  uint8_t flags{0};
};

namespace detail {

inline std::string anim_lower(const std::string &s) {
  std::string o(s);
  for (char &c : o)
    c = (char) tolower((unsigned char) c);
  return o;
}

// "for 22 yards", "22 Yd Run", "22-yard"; 0 when none
inline int play_yards(const std::string &low) {
  for (const char *unit : {" yard", "-yard", " yd"}) {
    size_t at = 0;
    while ((at = low.find(unit, at)) != std::string::npos) {
      size_t b = at;
      while (b > 0 && isdigit((unsigned char) low[b - 1]))
        b--;
      if (b < at) {
        const int n = atoi(low.c_str() + b);
        if (n > 0 && n <= 110)
          return n;
      }
      at++;
    }
  }
  return 0;
}

inline AnimKind td_from_type(const std::string &type) {
  const std::string t = anim_lower(type);
  if (t.find("passing touchdown") != std::string::npos)
    return AnimKind::TD_PASS;
  if (t.find("rushing touchdown") != std::string::npos)
    return AnimKind::TD_RUN;
  if (t.find("touchdown") != std::string::npos)
    return AnimKind::TD_RETURN;
  return AnimKind::NONE;
}

// Only the words before "touchdown": the try comes after
inline AnimKind td_from_words(const std::string &low) {
  const size_t at = low.find("touchdown");
  if (at == std::string::npos)
    return AnimKind::NONE;
  const std::string pre = low.substr(0, at);
  for (const char *w : {"intercept", "fumble", "return", "punt", "kickoff", "blocked"})
    if (pre.find(w) != std::string::npos)
      return AnimKind::TD_RETURN;
  if (pre.compare(0, 5, "pass ") == 0 || pre.find(" pass") != std::string::npos)
    return AnimKind::TD_PASS;
  return AnimKind::TD_RUN;
}

// "(401 feet)": 300 to 520, else 0
inline int homer_feet(const std::string &low) {
  const size_t at = low.find(" feet");
  if (at == std::string::npos)
    return 0;
  size_t b = at;
  while (b > 0 && isdigit((unsigned char) low[b - 1]))
    b--;
  const int n = b < at ? atoi(low.c_str() + b) : 0;
  return n <= 0 ? 0 : n < 300 ? 300 : n > 520 ? 520 : n;
}

// "homered to right center": 1 left to 5 right
inline int homer_field(const std::string &low) {
  const size_t at = low.find("homered to ");
  if (at == std::string::npos)
    return 0;
  const std::string w = low.substr(at + 11, 12);
  if (w.compare(0, 11, "left center") == 0)
    return 2;
  if (w.compare(0, 12, "right center") == 0)
    return 4;
  if (w.compare(0, 4, "left") == 0)
    return 1;
  if (w.compare(0, 5, "right") == 0)
    return 5;
  return w.compare(0, 6, "center") == 0 ? 3 : 0;
}

// Pinned on side (0 team, 1 opponent); no shootouts
inline bool goal_known(const GameSnapshot &prev, const GameSnapshot &cur, int side) {
  if (side != 0 && side != 1)
    return false;
  const int mine = side ? score_gain(prev.opp_score, cur.opp_score) : score_gain(prev.team_score, cur.team_score);
  const int other = side ? score_gain(prev.team_score, cur.team_score) : score_gain(prev.opp_score, cur.opp_score);
  if (mine <= 0)
    return false;
  if (cur.sport == Sport::SOCCER) {
    const Soccer &f = cur.soc;
    if (f.team_shootout >= 0 || f.opp_shootout >= 0 || f.status.find("SHOOTOUT") != std::string::npos)
      return false;
    // The newest goal names the side it counts for
    if (f.goals.size() > prev.soc.goals.size())
      return f.goals.back().ours == (side == 0);
    return other == 0;
  }
  if (cur.sport == Sport::HOCKEY)
    return !(cur.period >= 5 && !cur.nhl.playoffs) && other == 0;
  return false;
}

}  // namespace detail

// AnimArgs.flags bits 3-5: the home run's field, detail::homer_field
constexpr int kAnimFieldShift = 3;

// mirror: the board's right team scored; goals need prev, side
inline AnimArgs anim_of(Alert a, bool mirror, const GameSnapshot &cur, const GameSnapshot *prev = nullptr,
                        int side = -1) {
  AnimArgs r;
  if (a == Alert::SOCCER_GOAL || a == Alert::PENALTY_GOAL || a == Alert::HOCKEY_GOAL) {
    if (prev == nullptr || !detail::goal_known(*prev, cur, side))
      return r;
    r.kind = a == Alert::HOCKEY_GOAL ? AnimKind::HOCKEY_GOAL : AnimKind::SOCCER_GOAL;
    r.flags = mirror ? kAnimMirror : 0;
    return r;
  }
  if (a == Alert::HOME_RUN || a == Alert::GRAND_SLAM || (a == Alert::WALK_OFF && detail::home_run(cur))) {
    const std::string low = detail::anim_lower(cur.last_play);
    r.kind = AnimKind::HOME_RUN;
    r.dist = (int16_t) detail::homer_feet(low);
    r.flags = (uint8_t) (detail::homer_field(low) << kAnimFieldShift);
    return r;
  }
  if (a != Alert::TOUCHDOWN)
    return r;
  const std::string low = detail::anim_lower(cur.last_play);
  r.kind = cur.play_type.empty() ? detail::td_from_words(low) : detail::td_from_type(cur.play_type);
  if (r.kind == AnimKind::NONE)
    return r;
  r.dist = (int16_t) detail::play_yards(low);
  if (low.find("deep") != std::string::npos)
    r.flags |= kAnimDeep;
  else if (low.find("short") != std::string::npos)
    r.flags |= kAnimShort;
  else if (r.dist >= 25)
    r.flags |= kAnimDeep;
  else if (r.dist > 0 && r.dist <= 9)
    r.flags |= kAnimShort;
  if (mirror)
    r.flags |= kAnimMirror;
  return r;
}

// A splash plays the scene of its alert
inline void splash_anim(Splash &s, Alert a, const GameSnapshot &prev, const GameSnapshot &cur, AlertSide side) {
  const AnimArgs r = anim_of(a, false, cur, &prev, side == AlertSide::TEAM ? 0 : side == AlertSide::OPP ? 1 : -1);
  s.anim = (uint8_t) r.kind;
  s.anim_dist = r.dist;
  s.anim_flags = r.flags;
}

inline const char *anim_name(AnimKind k) {
  static const char *const kNames[] = {"none", "pass", "run", "return", "fg", "hr", "soccer", "hockey"};
  return (int) k < kAnimKinds ? kNames[(int) k] : "none";
}

}  // namespace espn
