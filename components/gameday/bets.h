// Bets the owner enters; graded from games already read.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include "leagues.h"
#include "teams.h"

namespace espn {

// Blob size: changing it loses every saved bet.
constexpr int kBetSlots = 8;
// Half points: spread to ±99.5, total to 999.5.
constexpr int kBetMaxSpreadHalf = 199;
constexpr int kBetMaxTotalHalf = 1999;
// Drops bets whose game stopped being read before final.
constexpr int64_t kBetUnreadSeconds = 36 * 3600;

enum class BetType : uint8_t { SPREAD = 0, TOTAL = 1, MONEYLINE = 2 };
// Spreads and moneylines use TEAM; totals OVER or UNDER.
enum class BetSide : uint8_t { TEAM = 0, OVER = 1, UNDER = 2 };
// A graded bet, kept until local midnight.
enum class BetResult : uint8_t { NONE = 0, WON = 1, LOST = 2, PUSH = 3 };

// Saved blob layout: fixed size, no padding.
struct Bet {
  uint32_t team_id;   // team bet on (totals: either); 0 empty
  uint32_t placed;    // entered at, epoch seconds; 0 if clock unset
  uint32_t event;     // ESPN game id; 0 until that game is read
  uint32_t opp_id;    // that game's other team
  uint32_t kickoff;   // that game's start, epoch seconds
  uint32_t expires;   // local midnight after the final, 0 before the final
  int16_t line;       // half points: spread (team's side), total; moneyline 0
  uint16_t team_score, opp_score;  // at the final
  uint8_t league;     // a League value
  uint8_t type;       // a BetType value
  uint8_t side;       // a BetSide value
  uint8_t result;     // a BetResult value
  uint8_t moved;      // 1: ESPN moved its game; bet stays on it
  uint8_t spare;
};
static_assert(sizeof(Bet) == 36, "Bet is the saved blob's layout: keep its size");

inline bool bet_empty(const Bet &b) { return b.team_id == 0; }

// Flash slots failing these checks are emptied.
inline bool bet_valid(const Bet &b) {
  if (b.team_id == 0 || b.league >= kLeagueCount || is_leaderboard((League) b.league) ||
      b.result > (uint8_t) BetResult::PUSH || b.moved > 1)
    return false;
  bool known = false;
  for (size_t t = 0; t < kTeamCount && !known; t++)
    known = (uint8_t) kTeams[t].league == b.league && kTeams[t].espn_id == b.team_id;
  if (!known)
    return false;
  switch ((BetType) b.type) {
    case BetType::SPREAD:
      return b.side == (uint8_t) BetSide::TEAM && b.line >= -kBetMaxSpreadHalf && b.line <= kBetMaxSpreadHalf;
    case BetType::TOTAL:
      return (b.side == (uint8_t) BetSide::OVER || b.side == (uint8_t) BetSide::UNDER) && b.line >= 1 &&
             b.line <= kBetMaxTotalHalf;
    case BetType::MONEYLINE:
      return b.side == (uint8_t) BetSide::TEAM && b.line == 0;
  }
  return false;
}

// Whole or half points only; totals take no sign.
inline bool parse_bet_line(const std::string &v, bool spread, int16_t &half) {
  size_t i = 0;
  bool neg = false;
  if (spread && i < v.size() && (v[i] == '+' || v[i] == '-')) {
    neg = v[i] == '-';
    i++;
  }
  size_t digits = i;
  long whole = 0;
  while (i < v.size() && v[i] >= '0' && v[i] <= '9' && i - digits < 4)
    whole = whole * 10 + (v[i++] - '0');
  if (i == digits)
    return false;
  long h = whole * 2;
  if (i < v.size() && v[i] == '.') {
    if (++i >= v.size() || (v[i] != '5' && v[i] != '0'))
      return false;
    h += v[i++] == '5' ? 1 : 0;
  }
  if (i != v.size())
    return false;
  if (neg)
    h = -h;
  if (spread ? (h < -kBetMaxSpreadHalf || h > kBetMaxSpreadHalf) : (h < 1 || h > kBetMaxTotalHalf))
    return false;
  half = (int16_t) h;
  return true;
}

// Wire word shared with the page and state document.
inline const char *bet_type_key(const Bet &b) {
  if (b.type == (uint8_t) BetType::TOTAL)
    return b.side == (uint8_t) BetSide::UNDER ? "under" : "over";
  return b.type == (uint8_t) BetType::MONEYLINE ? "ml" : "spread";
}

// Either team may be "team".
enum class BetPhase : uint8_t { PRE, LIVE, FINAL, OFF };  // OFF: ended without being completed (postponed, canceled)
struct BetGame {
  uint8_t league{0};
  uint32_t event{0};  // ESPN's event id; 0 if not storable
  uint32_t team_id{0}, opp_id{0};
  int team_score{0}, opp_score{0};
  int64_t kickoff{0};
  BetPhase phase{BetPhase::PRE};
};

// ESPN ids are numeric, around 400 million; fits 32 bits.
inline uint32_t bet_event_id(const std::string &id) {
  if (id.empty() || id.size() > 10)
    return 0;
  uint64_t n = 0;
  for (char c : id) {
    if (c < '0' || c > '9')
      return 0;
    n = n * 10 + (uint64_t) (c - '0');
  }
  return n > UINT32_MAX ? 0 : (uint32_t) n;
}

// A total's team_id may be either side; props share it.
template<typename T> inline bool bet_in_game(const T &b, const BetGame &g) {
  return b.team_id != 0 && g.event != 0 && b.league == g.league && (b.team_id == g.team_id || b.team_id == g.opp_id);
}

// g's score from the bet's team's side.
inline void bet_score(const Bet &b, const BetGame &g, int &us, int &them) {
  bool ours = b.team_id == g.team_id;
  us = ours ? g.team_score : g.opp_score;
  them = ours ? g.opp_score : g.team_score;
}

// Sign matters: positive covers, negative doesn't, 0 pushes.
inline int bet_margin(const Bet &b, int us, int them) {
  switch ((BetType) b.type) {
    case BetType::SPREAD:
      return 2 * (us - them) + b.line;
    case BetType::TOTAL: {
      int m = 2 * (us + them) - b.line;
      return b.side == (uint8_t) BetSide::UNDER ? -m : m;
    }
    case BetType::MONEYLINE:
      return us - them;
  }
  return 0;
}

// Games ended before the bet was placed can't be it.
template<typename T> inline bool bet_game_open(const T &b, const BetGame &g) {
  return !((g.phase == BetPhase::FINAL || g.phase == BetPhase::OFF) && (b.placed == 0 || g.kickoff < b.placed));
}

// False unless g is b's game; marks moves in changed.
template<typename T> inline bool bet_tie(T &b, const BetGame &g, int64_t now, bool &changed) {
  bool tied = false;
  if (b.event != g.event) {
    bool earlier = b.event != 0 && b.result == 0 && !b.moved && b.kickoff != 0 && now < (int64_t) b.kickoff &&
                   g.kickoff > 0 && g.kickoff < (int64_t) b.kickoff;
    if ((b.event != 0 && !earlier) || !bet_game_open(b, g))
      return false;
    b.event = g.event;
    b.moved = 0;
    changed = tied = true;
  }
  uint32_t opp = b.team_id == g.team_id ? g.opp_id : g.team_id;
  uint32_t kick = g.kickoff > 0 && g.kickoff <= (int64_t) UINT32_MAX ? (uint32_t) g.kickoff : 0;
  if (b.opp_id != opp || b.kickoff != kick) {
    if (!tied && b.kickoff != 0 && b.kickoff != kick)
      b.moved = 1;  // a postponed game ESPN moved keeps the bet
    b.opp_id = opp;
    b.kickoff = kick;
    changed = true;
  }
  return true;
}

// A later game read first yields to an earlier one.
inline bool bet_observe(Bet &b, const BetGame &g, int64_t now, int64_t midnight) {
  if (!bet_in_game(b, g))
    return false;
  bool changed = false;
  if (!bet_tie(b, g, now, changed))
    return false;
  if (g.phase != BetPhase::FINAL)
    return changed;
  int us = 0, them = 0;
  bet_score(b, g, us, them);
  int m = bet_margin(b, us, them);
  BetResult r = m > 0 ? BetResult::WON : m < 0 ? BetResult::LOST : BetResult::PUSH;
  uint16_t ts = (uint16_t) (us < 0 ? 0 : us > UINT16_MAX ? UINT16_MAX : us);
  uint16_t os = (uint16_t) (them < 0 ? 0 : them > UINT16_MAX ? UINT16_MAX : them);
  if (b.result == (uint8_t) r && b.team_score == ts && b.opp_score == os)
    return changed;  // graded already; a score ESPN corrects grades again
  if (b.result == (uint8_t) BetResult::NONE)
    b.expires = midnight > now && midnight <= (int64_t) UINT32_MAX ? (uint32_t) midnight : (uint32_t) (now + 1);
  b.result = (uint8_t) r;
  b.team_score = ts;
  b.opp_score = os;
  return true;
}

inline bool bet_expired(const Bet &b, int64_t now) {
  if (bet_empty(b))
    return false;
  if (b.result != (uint8_t) BetResult::NONE)
    return now >= (int64_t) b.expires;
  return b.event != 0 && b.kickoff != 0 && now - (int64_t) b.kickoff >= kBetUnreadSeconds;
}

// Order matches bet_status_key's names.
enum class BetStatus : uint8_t {
  WAITING,       // no game read yet for the team
  PRE,           // its game hasn't started: the line only
  UNREAD,        // started, but the panel isn't reading it
  COVERING,
  NOT_COVERING,
  LEVEL,         // exactly on the line during the game
  WON,
  LOST,
  PUSH,          // exactly on the line at the final
  VOID,          // the game ended without being completed (postponed, canceled)
};

// g is nullptr when the panel has no game.
inline BetStatus bet_status(const Bet &b, const BetGame *g, int64_t now) {
  switch ((BetResult) b.result) {
    case BetResult::WON:
      return BetStatus::WON;
    case BetResult::LOST:
      return BetStatus::LOST;
    case BetResult::PUSH:
      return BetStatus::PUSH;
    case BetResult::NONE:
      break;
  }
  if (b.event == 0)
    return BetStatus::WAITING;
  if (g != nullptr && g->event == b.event) {
    if (g->phase == BetPhase::PRE)
      return BetStatus::PRE;
    if (g->phase == BetPhase::OFF)
      return BetStatus::VOID;
    int us = 0, them = 0;
    bet_score(b, *g, us, them);
    int m = bet_margin(b, us, them);
    if (g->phase == BetPhase::FINAL)
      return m > 0 ? BetStatus::WON : m < 0 ? BetStatus::LOST : BetStatus::PUSH;
    return m > 0 ? BetStatus::COVERING : m < 0 ? BetStatus::NOT_COVERING : BetStatus::LEVEL;
  }
  return (int64_t) b.kickoff > now ? BetStatus::PRE : BetStatus::UNREAD;
}

// Keys in the state document; keep BetStatus order.
inline const char *bet_status_key(BetStatus s) {
  static const char *const kNames[] = {"waiting", "pre", "unread", "covering", "not_covering",
                                       "level",   "won", "lost",   "push",     "void"};
  return kNames[(uint8_t) s];
}

inline const char *bet_word(BetStatus s) {
  switch (s) {
    case BetStatus::COVERING:
      return "COVERING";
    case BetStatus::NOT_COVERING:
      return "NOT COVERING";
    case BetStatus::LEVEL:
    case BetStatus::PUSH:
      return "PUSH";
    case BetStatus::WON:
      return "WON";
    case BetStatus::LOST:
      return "LOST";
    case BetStatus::VOID:
      return "VOID";
    default:
      return "";
  }
}

// Half points as "3.5", or "+7" with sign.
inline std::string bet_points(int half, bool sign) {
  std::string out = sign && half > 0 ? "+" : half < 0 ? "-" : "";
  int a = half < 0 ? -half : half;
  out += std::to_string(a / 2);
  if (a % 2)
    out += ".5";
  return out;
}

inline const char *bet_team_abbr(uint8_t league, uint32_t id) {
  for (size_t t = 0; t < kTeamCount; t++)
    if ((uint8_t) kTeams[t].league == league && kTeams[t].espn_id == id)
      return kTeams[t].abbr;
  return "";
}

// e.g. "BUF -3.5", "BUF PK", "OVER 47.5", "BUF ML".
inline std::string bet_label(const Bet &b) {
  if (b.type == (uint8_t) BetType::TOTAL)
    return std::string(b.side == (uint8_t) BetSide::UNDER ? "UNDER " : "OVER ") + bet_points(b.line, false);
  std::string out = bet_team_abbr(b.league, b.team_id);
  if (b.type == (uint8_t) BetType::MONEYLINE)
    return out + " ML";
  return out + " " + (b.line == 0 ? std::string("PK") : bet_points(b.line, true));
}

// e.g. "BUF -3.5 COVERING"; before start "BET BUF -3.5".
inline std::string bet_ticker(const Bet &b, BetStatus s) {
  const char *word = bet_word(s);
  return *word ? bet_label(b) + " " + word : "BET " + bet_label(b);
}

// The device page's chip colors (app.css)
constexpr uint32_t kBetGood = 0x2ED573, kBetBad = 0xFF3B3B, kBetEven = 0xFFD000;

// 0 keeps the ticker's own color
inline uint32_t bet_color(BetStatus s) {
  switch (s) {
    case BetStatus::COVERING:
    case BetStatus::WON:
      return kBetGood;
    case BetStatus::NOT_COVERING:
    case BetStatus::LOST:
      return kBetBad;
    case BetStatus::LEVEL:
    case BetStatus::PUSH:
    case BetStatus::VOID:
      return kBetEven;
    default:
      return 0;
  }
}

// Abstract so a bet tracker API could feed bets.
class BetSource {
 public:
  virtual ~BetSource() = default;
  virtual uint8_t size() const = 0;
  virtual Bet &at(uint8_t i) = 0;  // i below size(); an empty slot is bet_empty
  virtual void changed() = 0;
};

// Slot 0 takes a free slot; concurrent pages don't collide.
inline uint8_t bet_put(Bet (&bets)[kBetSlots], uint8_t slot, const Bet &b) {
  if (slot > kBetSlots)
    return 0;
  for (uint8_t i = 0; slot == 0 && i < kBetSlots; i++)
    if (bet_empty(bets[i]))
      slot = i + 1;
  if (slot == 0)
    return 0;
  bets[slot - 1] = b;
  return slot;
}

// Saved to flash as one fixed-size blob.
struct BetBlob {
  Bet bets[kBetSlots];
};

class ManualBets final : public BetSource {
 public:
  uint8_t size() const override { return kBetSlots; }
  Bet &at(uint8_t i) override { return this->blob.bets[i]; }
  void changed() override { this->dirty = true; }
  // Empties slots from newer firmware or unknown leagues.
  void loaded() {
    for (auto &b : this->blob.bets)
      if (!bet_empty(b) && !bet_valid(b))
        b = Bet{};
  }
  BetBlob blob{};
  bool dirty{false};  // changed since the last save
};

inline void bets_observe(BetSource &src, const BetGame &g, int64_t now, int64_t midnight) {
  for (uint8_t i = 0; i < src.size(); i++)
    if (bet_observe(src.at(i), g, now, midnight))
      src.changed();
}

// Unfollowed reads never tie or move a bet.
inline bool bet_update(Bet &b, const BetGame &g, int64_t now, int64_t midnight) {
  if (bet_empty(b) || b.event == 0 || b.event != g.event || b.league != g.league)
    return false;
  return bet_observe(b, g, now, midnight);
}

inline void bets_update(BetSource &src, const BetGame &g, int64_t now, int64_t midnight) {
  for (uint8_t i = 0; i < src.size(); i++)
    if (bet_update(src.at(i), g, now, midnight))
      src.changed();
}

inline bool bets_expire(BetSource &src, int64_t now) {
  bool any = false;
  for (uint8_t i = 0; i < src.size(); i++) {
    if (bet_expired(src.at(i), now)) {
      src.at(i) = Bet{};
      any = true;
    }
  }
  if (any)
    src.changed();
  return any;
}

}  // namespace espn
