// Which play animation a big play gets
// An unknown play gets none, never a guess
#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "alerts.h"
#include "px565.h"

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

// The scoring run's yards: the last before "touchdown"
inline int td_yards(const std::string &low) {
  const size_t td = low.find("touchdown");
  if (td == std::string::npos)
    return play_yards(low);
  int best = 0;
  for (size_t at = low.find("yard"); at != std::string::npos && at < td; at = low.find("yard", at + 1)) {
    size_t b = at;
    if (b > 0 && (low[b - 1] == ' ' || low[b - 1] == '-'))
      b--;
    size_t e = b;
    while (b > 0 && isdigit((unsigned char) low[b - 1]))
      b--;
    if (b < e) {
      const int n = atoi(low.c_str() + b);
      if (n >= 0 && n <= 110)
        best = n;
    }
  }
  return best > 0 ? best : play_yards(low);
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
  r.dist = (int16_t) detail::td_yards(low);
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

// What a caption says beyond the kind
enum class Say : uint8_t {
  PLAIN,
  PICK_SIX,
  FUMBLE_TD,
  KICK_RET,
  PUNT_RET,
  BLOCK_TD,
  SLAM,
  WALK_OFF,
  PENALTY,
  OWN_GOAL,
};

namespace detail {

// A caption's letters: upper case, else dropped
inline std::string say_clean(const std::string &in) {
  std::string out;
  for (char c : in) {
    if (c >= 'a' && c <= 'z')
      c = (char) (c - 'a' + 'A');
    if (font3x5_has(c) && !(c == ' ' && (out.empty() || out.back() == ' ')))
      out += c;
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

// A name the caption can show, else ""
inline std::string say_name(const std::string &raw) {
  std::string out;
  for (char c : raw) {
    if (c >= 'a' && c <= 'z')
      c = (char) (c - 'a' + 'A');
    if (!((c >= 'A' && c <= 'Z') || c == '\'' || c == '-' || c == '.' || c == ' '))
      return "";
    out += c;
  }
  return out.size() >= 2 && out.size() <= 12 ? out : "";
}

inline void say_add(std::string &out, const std::string &c) {
  if (c.empty() || c[0] == ' ' || c.back() == ' ' || c.find("  ") != std::string::npos)
    return;
  if (("|" + out + "|").find("|" + c + "|") != std::string::npos)
    return;
  out += out.empty() ? c : "|" + c;
}

// Name, yards, the long words, then shorter
inline void say_ladder(std::string &out, const std::string &n, const std::string &y, const char *lw, const char *sw) {
  const std::string l = lw, w = sw;
  if (!n.empty() && !y.empty()) {
    say_add(out, n + " " + y + " " + l);
    say_add(out, n + " " + y + " " + w);
  }
  if (!y.empty()) {
    say_add(out, y + " " + l);
    say_add(out, y + " " + w);
  }
  if (!n.empty()) {
    say_add(out, n + " " + l);
    say_add(out, n + " " + w);
  }
  say_add(out, l);
  say_add(out, w);
}

inline bool name_char(char c) { return isalpha((unsigned char) c) || c == '\'' || c == '-'; }

// The last "J.Chase" or "P. Durkin" before end
inline std::string initial_name(const std::string &t, size_t end) {
  std::string best;
  end = end < t.size() ? end : t.size();
  for (size_t i = 0; i + 2 < end; i++) {
    if (!isupper((unsigned char) t[i]) || (i > 0 && isalnum((unsigned char) t[i - 1])))
      continue;
    size_t j = i + 1;
    if (islower((unsigned char) t[j]))
      j++;
    if (j >= end || t[j] != '.')
      continue;
    j++;
    if (j < end && t[j] == ' ')
      j++;
    if (j >= end || !isupper((unsigned char) t[j]))
      continue;
    size_t k = j;
    while (k < end && name_char(t[k]))
      k++;
    if (k - j < 2)
      continue;
    // Two words: "A.St. Brown"
    if (k - j <= 3 && k + 2 < end && t[k] == '.' && t[k + 1] == ' ' && isupper((unsigned char) t[k + 2])) {
      k += 2;
      while (k < end && name_char(t[k]))
        k++;
    }
    best = t.substr(j, k - j);
    i = k;
  }
  return best;
}

inline bool name_suffix(const std::string &w) {
  return w == "Jr." || w == "Sr." || w == "Jr" || w == "Sr" || w == "II" || w == "III" || w == "IV";
}

// The capitalized word before at, past "Jr."
inline std::string word_before(const std::string &t, size_t at) {
  at = at < t.size() ? at : t.size();
  for (int tries = 0; tries < 2 && at > 0; tries++) {
    size_t e = at;
    while (e > 0 && t[e - 1] == ' ')
      e--;
    size_t b = e;
    while (b > 0 && t[b - 1] != ' ' && t[b - 1] != '(')
      b--;
    const std::string w = t.substr(b, e - b);
    if (w.empty())
      return "";
    if (name_suffix(w)) {
      at = b;
      continue;
    }
    // "J.Doe" names the last part
    const size_t dot = w.find('.');
    const std::string last = dot != std::string::npos && dot + 1 < w.size() ? w.substr(dot + 1) : w;
    if (!isupper((unsigned char) last[0]))
      return "";
    for (char c : last)
      if (!name_char(c))
        return "";
    return last;
  }
  return "";
}

// "Joey Slye 46 Yd Field Goal": "Slye"
inline std::string yd_name(const std::string &t) {
  for (size_t at = t.find(" Yd"); at != std::string::npos; at = t.find(" Yd", at + 1)) {
    size_t b = at;
    while (b > 0 && isdigit((unsigned char) t[b - 1]))
      b--;
    if (b < at && b > 0 && t[b - 1] == ' ')
      return word_before(t, b - 1);
  }
  return "";
}

inline size_t find_low(const std::string &t, const char *w) { return anim_lower(t).find(w); }

// Who scored, in ESPN's words; "" when unsure
inline std::string scorer(AnimKind k, const GameSnapshot &prev, const GameSnapshot &cur) {
  const std::string &t = cur.last_play;
  std::string n;
  if (k == AnimKind::TD_PASS || k == AnimKind::TD_RUN || k == AnimKind::TD_RETURN) {
    n = initial_name(t, find_low(t, "touchdown"));
    if (n.empty())
      n = yd_name(t);
  } else if (k == AnimKind::HOME_RUN) {
    const size_t at = t.find(" homered");
    if (at == std::string::npos)
      return "";
    n = word_before(t, at);
  } else if (k == AnimKind::SOCCER_GOAL) {
    if (cur.soc.goals.size() <= prev.soc.goals.size() || cur.soc.goals.back().own_goal)
      return "";
    const std::string &g = cur.soc.goals.back().name;
    const size_t sp = g.rfind(' ');
    n = ascii_fold(sp == std::string::npos ? g : g.substr(sp + 1));
  } else if (k == AnimKind::HOCKEY_GOAL) {
    if (!new_play(prev, cur))
      return "";
    size_t at = t.find(" Goal (");
    if (at == std::string::npos && t.size() >= 5 && t.compare(t.size() - 5, 5, " Goal") == 0)
      at = t.size() - 5;
    if (at == std::string::npos)
      return "";
    n = word_before(ascii_fold(t.substr(0, at)), std::string::npos);
  }
  return say_name(n);
}

inline Say td_say(const GameSnapshot &cur) {
  const std::string t = anim_lower(cur.play_type.empty() ? cur.last_play.substr(0, cur.last_play.find("TOUCHDOWN"))
                                                          : cur.play_type);
  if (t.find("intercept") != std::string::npos)
    return Say::PICK_SIX;
  if (t.find("fumble") != std::string::npos)
    return Say::FUMBLE_TD;
  if (t.find("blocked") != std::string::npos)
    return Say::BLOCK_TD;
  if (t.find("punt") != std::string::npos)
    return Say::PUNT_RET;
  if (t.find("kickoff") != std::string::npos || t.find(" kicks ") != std::string::npos)
    return Say::KICK_RET;
  return Say::PLAIN;
}

}  // namespace detail

// The caption's top line: choices longest first, '|' between
inline std::string say_words(const AnimArgs &a, Say what, const std::string &name, int runs = 0) {
  std::string out;
  const std::string n = detail::say_clean(name);
  const std::string y = a.dist > 0 ? std::to_string(a.dist) + " YD" : "";
  switch (a.kind) {
    case AnimKind::TD_PASS:
      detail::say_ladder(out, n, y, "TD PASS", "TD");
      break;
    case AnimKind::TD_RUN:
      detail::say_ladder(out, n, y, "TD RUN", "TD");
      break;
    case AnimKind::TD_RETURN: {
      static const char *const kLong[] = {"RETURN TD", "PICK SIX", "FUMBLE RETURN TD", "KICKOFF RETURN TD",
                                          "PUNT RETURN TD", "BLOCKED KICK TD"};
      static const char *const kShort[] = {"RETURN TD", "PICK SIX", "FUMBLE TD", "KICK RET TD", "PUNT RET TD",
                                           "BLOCK TD"};
      const int i = (int) what <= (int) Say::BLOCK_TD ? (int) what : 0;
      detail::say_ladder(out, n, y, kLong[i], kShort[i]);
      break;
    }
    case AnimKind::HOME_RUN: {
      const std::string r = std::to_string(runs);
      const std::string wl = what == Say::SLAM || runs == 4 ? "GRAND SLAM"
                             : what == Say::WALK_OFF        ? "WALK-OFF HOME RUN"
                             : runs >= 2                    ? r + "-RUN HOME RUN"
                                                            : "HOME RUN";
      const std::string ws = what == Say::SLAM || runs == 4 ? "GRAND SLAM"
                             : what == Say::WALK_OFF        ? "WALK-OFF HR"
                             : runs >= 2                    ? r + "-RUN HR"
                                                            : "HOME RUN";
      static const char *const kFields[] = {"", " LEFT", " LEFT CENTER", " CENTER", " RIGHT CENTER", " RIGHT"};
      const int field = (a.flags >> kAnimFieldShift) & 7;
      const std::string d = a.dist > 0 ? std::to_string(a.dist) + " FT" : "";
      const std::string f = !d.empty() && field > 0 && field < 6 ? std::string(kFields[field]) : "";
      if (!n.empty()) {
        detail::say_add(out, n + " " + wl + " " + d + f);
        detail::say_add(out, n + " " + wl + " " + d);
      }
      detail::say_add(out, wl + " " + d + f);
      if (!n.empty())
        detail::say_add(out, n + " " + wl);
      detail::say_add(out, wl + " " + d);
      detail::say_add(out, wl);
      if (!n.empty())
        detail::say_add(out, n + " " + ws);
      detail::say_add(out, ws + " " + d);
      detail::say_add(out, ws);
      break;
    }
    case AnimKind::SOCCER_GOAL:
    case AnimKind::HOCKEY_GOAL:
      if (what == Say::OWN_GOAL) {
        detail::say_add(out, "OWN GOAL");
      } else if (what == Say::PENALTY) {
        if (!n.empty())
          detail::say_add(out, n + " PENALTY GOAL");
        detail::say_add(out, "PENALTY GOAL");
        detail::say_add(out, "PEN GOAL");
      } else {
        if (!n.empty())
          detail::say_add(out, n + " GOAL");
        detail::say_add(out, "GOAL");
      }
      break;
    default:
      break;
  }
  return out;
}

// The caption's score line: "BUF 21 +6"
inline std::string say_score(const std::string &abbr, int score, int gain) {
  std::string out = detail::say_clean(abbr.size() <= 6 ? abbr : abbr.substr(0, 6));
  if (score >= 0 && score < 1000)
    out += (out.empty() ? "" : " ") + std::to_string(score);
  if (gain > 0 && gain < 100)
    out += (out.empty() ? "+" : " +") + std::to_string(gain);
  return out;
}

// A Try it play, captioned with the board's team
struct AnimSample {
  AnimArgs args;
  std::string say, score;
};
inline AnimSample anim_sample(const std::string &play, const std::string &abbr, int score,
                              const std::string &name = "") {
  AnimSample r;
  AnimArgs &a = r.args;
  int gain = 6;
  Say what = Say::PLAIN;
  if (play == "run") {
    a.kind = AnimKind::TD_RUN;
    a.dist = 12;
  } else if (play == "ret") {
    a.kind = AnimKind::TD_RETURN;
    a.dist = 98;
    what = Say::KICK_RET;
  } else if (play == "hr") {
    a.kind = AnimKind::HOME_RUN;
    a.dist = 410;
    a.flags = 3 << kAnimFieldShift;
    gain = 2;
  } else if (play == "soccer" || play == "hockey") {
    a.kind = play == "soccer" ? AnimKind::SOCCER_GOAL : AnimKind::HOCKEY_GOAL;
    gain = 1;
  } else {
    a.kind = AnimKind::TD_PASS;
    a.dist = 24;
  }
  r.say = say_words(a, what, detail::say_name(name), gain);
  r.score = abbr.empty() ? say_score("", -1, gain) : say_score(abbr, score + gain, gain);
  return r;
}

// A splash plays the scene of its alert, captioned
inline void splash_anim(Splash &s, Alert a, const GameSnapshot &prev, const GameSnapshot &cur, AlertSide side,
                        const char *name = nullptr) {
  const AnimArgs r = anim_of(a, false, cur, &prev, side == AlertSide::TEAM ? 0 : side == AlertSide::OPP ? 1 : -1);
  s.anim = (uint8_t) r.kind;
  s.anim_dist = r.dist;
  s.anim_flags = r.flags;
  if (r.kind == AnimKind::NONE)
    return;
  const int gain = s.points.size() > 1 && s.points[0] == '+' ? atoi(s.points.c_str() + 1) : 0;
  Say what = Say::PLAIN;
  if (r.kind == AnimKind::TD_RETURN)
    what = detail::td_say(cur);
  else if (a == Alert::GRAND_SLAM)
    what = Say::SLAM;
  else if (a == Alert::WALK_OFF)
    what = Say::WALK_OFF;
  else if (a == Alert::PENALTY_GOAL)
    what = Say::PENALTY;
  else if (r.kind == AnimKind::SOCCER_GOAL && cur.soc.goals.size() > prev.soc.goals.size() &&
           cur.soc.goals.back().own_goal)
    what = Say::OWN_GOAL;
  const std::string who = name != nullptr ? detail::say_name(name) : detail::scorer(r.kind, prev, cur);
  s.anim_say = say_words(r, what, who, gain);
  s.anim_score = say_score(s.abbr, side == AlertSide::OPP ? cur.opp_score : cur.team_score, gain);
}

inline const char *anim_name(AnimKind k) {
  static const char *const kNames[] = {"none", "pass", "run", "return", "fg", "hr", "soccer", "hockey"};
  return (int) k < kAnimKinds ? kNames[(int) k] : "none";
}

}  // namespace espn
