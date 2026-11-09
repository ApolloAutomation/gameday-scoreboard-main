// Active bets the owner types in on the device page: a point spread or a
// moneyline on a team, or a total on a team's game. The panel never connects
// to a sportsbook. It compares each bet with the score of a game it already
// reads (the game on the board, and in Favorite teams each favorite's card)
// and says where the bet stands: the line before the start, COVERING, NOT
// COVERING or PUSH while the game is on, and WON, LOST or PUSH after the
// final until local midnight, when the bet is dropped.
//
// Where bets come from is a BetSource: the device page's manual entries
// (ManualBets, kept in flash) are the only one now. A bet tracker's API could
// be another source of the same Bet records, and the panel would follow its
// bets the same way (bets_observe, bets_expire). Likewise a BetGame is any
// game the panel has read: the big-play watcher's reads of other games
// (watch.h) feed theirs too, to the bets tied to them already (bets_update).
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include "leagues.h"
#include "teams.h"

namespace espn {

// How many bets the panel keeps. The saved blob holds exactly this many: a
// blob that changes size fails to load and loses every bet in it, so more
// would need a new blob.
constexpr int kBetSlots = 8;
// Lines in half points: a spread up to 99.5 either way, a total up to 999.5.
constexpr int kBetMaxSpreadHalf = 199;
constexpr int kBetMaxTotalHalf = 1999;
// A bet whose game the panel stopped reading before its final (another mode
// or team took the board, or the game was called off) is dropped this long
// after the start.
constexpr int64_t kBetUnreadSeconds = 36 * 3600;

enum class BetType : uint8_t { SPREAD = 0, TOTAL = 1, MONEYLINE = 2 };
// A spread or a moneyline is on the team; a total is over or under the line.
enum class BetSide : uint8_t { TEAM = 0, OVER = 1, UNDER = 2 };
// A graded bet, kept until local midnight.
enum class BetResult : uint8_t { NONE = 0, WON = 1, LOST = 2, PUSH = 3 };

// One bet, laid out as the saved blob keeps it: fixed size, no padding.
// Zero (Bet{}) is an empty slot.
struct Bet {
  uint32_t team_id;   // the team bet on, or for a total a team in the game; 0 = empty slot
  uint32_t placed;    // when it was entered, epoch seconds; 0 when the clock wasn't set
  uint32_t event;     // ESPN's id of the game it applies to, 0 until the panel reads that game
  uint32_t opp_id;    // that game's other team
  uint32_t kickoff;   // that game's start, epoch seconds
  uint32_t expires;   // local midnight after the final, 0 before the final
  int16_t line;       // half points: a spread from the team's side (-7 is -3.5), a total; 0 for a moneyline
  uint16_t team_score, opp_score;  // at the final
  uint8_t league;     // League
  uint8_t type;       // BetType
  uint8_t side;       // BetSide
  uint8_t result;     // BetResult
  uint8_t moved;      // 1 once ESPN moved its game to another start: it stays on that game
  uint8_t spare;
};
static_assert(sizeof(Bet) == 36, "Bet is the saved blob's layout: keep its size");

inline bool bet_empty(const Bet &b) { return b.team_id == 0; }

// What a bet says, without the game: a known league with a team, and a type
// with its side and a line in range. A slot read back from flash that isn't
// is emptied.
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

// A line as the device page sends it, into half points: a spread "-3.5",
// "+7", "7" or "0" (pick'em), a total "47.5" or "47" with no sign. Whole or
// half points only ("3.0" is 3, "3.25" is refused), nothing before or after.
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

// The type word of the device page's bet= key and the state document.
inline const char *bet_type_key(const Bet &b) {
  if (b.type == (uint8_t) BetType::TOTAL)
    return b.side == (uint8_t) BetSide::UNDER ? "under" : "over";
  return b.type == (uint8_t) BetType::MONEYLINE ? "ml" : "spread";
}

// A game the panel has read, for the bets on it. Either team may be "team".
enum class BetPhase : uint8_t { PRE, LIVE, FINAL, OFF };  // OFF: ended without being completed (postponed, canceled)
struct BetGame {
  uint8_t league{0};
  uint32_t event{0};  // ESPN's event id; 0 when it has none the panel can keep
  uint32_t team_id{0}, opp_id{0};
  int team_score{0}, opp_score{0};
  int64_t kickoff{0};
  BetPhase phase{BetPhase::PRE};
};

// ESPN's event id as the saved bet keeps it: all digits and under 2^32
// (they are around 400 million), else 0, which no bet ties to.
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

// The bet's team plays in g (a total names either team of its game).
inline bool bet_in_game(const Bet &b, const BetGame &g) {
  return !bet_empty(b) && g.event != 0 && b.league == g.league && (b.team_id == g.team_id || b.team_id == g.opp_id);
}

// g's score from the bet's team's side.
inline void bet_score(const Bet &b, const BetGame &g, int &us, int &them) {
  bool ours = b.team_id == g.team_id;
  us = ours ? g.team_score : g.opp_score;
  them = ours ? g.opp_score : g.team_score;
}

// Where the bet stands at this score, in half points: above 0 it is covering
// (won at the final), below 0 not covering, 0 level (a push). A spread adds
// its line to the team's score, a total compares both scores with its line,
// and a moneyline is the team ahead or not.
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

// g could be the bet's game: anything but a game that ended (a final, or
// called off) before the bet went in, which was most likely over by then.
// With no clock when the bet went in, an ended game can't be told apart.
inline bool bet_game_open(const Bet &b, const BetGame &g) {
  return !((g.phase == BetPhase::FINAL || g.phase == BetPhase::OFF) && (b.placed == 0 || g.kickoff < b.placed));
}

// The panel read g. A bet is for its team's next or current game, so a bet
// with no game yet takes g when bet_game_open, and a bet tied to a later game
// of the team moves to g when g starts earlier, while that later game hasn't
// started and the bet has no result: the panel read the later game first (the
// team was the other side of the game on the board, or another favorite's
// card came first). A bet whose game ESPN moved to another start stays on it.
// The bet on g keeps its details and, at the final, its result, until
// midnight (the local midnight after now). True when the bet changed and its
// source should keep it.
inline bool bet_observe(Bet &b, const BetGame &g, int64_t now, int64_t midnight) {
  if (!bet_in_game(b, g))
    return false;
  bool changed = false;
  bool tied = false;
  if (b.event != g.event) {
    bool earlier = b.event != 0 && b.result == (uint8_t) BetResult::NONE && !b.moved && b.kickoff != 0 &&
                   now < (int64_t) b.kickoff && g.kickoff > 0 && g.kickoff < (int64_t) b.kickoff;
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

// Done with: its result has been up until midnight, or the panel never saw
// its game's final (kBetUnreadSeconds after the start).
inline bool bet_expired(const Bet &b, int64_t now) {
  if (bet_empty(b))
    return false;
  if (b.result != (uint8_t) BetResult::NONE)
    return now >= (int64_t) b.expires;
  return b.event != 0 && b.kickoff != 0 && now - (int64_t) b.kickoff >= kBetUnreadSeconds;
}

// Where a bet stands, for the ticker and the device page.
enum class BetStatus : uint8_t {
  WAITING,       // no game yet: the panel hasn't read the team's next game
  PRE,           // its game hasn't started: the line only
  UNREAD,        // its game has started but the panel isn't reading it now
  COVERING,
  NOT_COVERING,
  LEVEL,         // exactly on the line during the game
  WON,
  LOST,
  PUSH,          // exactly on the line at the final
  VOID,          // the game ended without being completed (postponed, canceled)
};

// g: the bet's game as the panel has it now, nullptr when it has none.
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

// The state document's name for a status.
inline const char *bet_status_key(BetStatus s) {
  static const char *const kNames[] = {"waiting", "pre", "unread", "covering", "not_covering",
                                       "level",   "won", "lost",   "push",     "void"};
  return kNames[(uint8_t) s];
}

// The word the ticker puts after the bet: "" before the start or with no game.
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

// Half points as a line: "3.5", "7"; with sign, "+7", "-3.5".
inline std::string bet_points(int half, bool sign) {
  std::string out = sign && half > 0 ? "+" : half < 0 ? "-" : "";
  int a = half < 0 ? -half : half;
  out += std::to_string(a / 2);
  if (a % 2)
    out += ".5";
  return out;
}

// The team's abbreviation from the team list, "" for one not in it.
inline const char *bet_team_abbr(uint8_t league, uint32_t id) {
  for (size_t t = 0; t < kTeamCount; t++)
    if ((uint8_t) kTeams[t].league == league && kTeams[t].espn_id == id)
      return kTeams[t].abbr;
  return "";
}

// The bet in a few words: "BUF -3.5", "BUF +7", "BUF PK", "OVER 47.5",
// "UNDER 47", "BUF ML".
inline std::string bet_label(const Bet &b) {
  if (b.type == (uint8_t) BetType::TOTAL)
    return std::string(b.side == (uint8_t) BetSide::UNDER ? "UNDER " : "OVER ") + bet_points(b.line, false);
  std::string out = bet_team_abbr(b.league, b.team_id);
  if (b.type == (uint8_t) BetType::MONEYLINE)
    return out + " ML";
  return out + " " + (b.line == 0 ? std::string("PK") : bet_points(b.line, true));
}

// The ticker's part for a bet on the game on the board: "BUF -3.5
// COVERING", "OVER 47.5 WON", and before the start "BET BUF -3.5".
inline std::string bet_ticker(const Bet &b, BetStatus s) {
  const char *word = bet_word(s);
  return *word ? bet_label(b) + " " + word : "BET " + bet_label(b);
}

// Where the panel's bets come from. The device page's manual entries are
// the only source now; a bet tracker's API could be another, handing the
// panel the same Bet records. The panel ties each bet to its game, grades it
// and drops it (bets_observe, bets_expire) and tells the source through
// changed(), so a source that keeps its bets saves them then.
class BetSource {
 public:
  virtual ~BetSource() = default;
  virtual uint8_t size() const = 0;
  virtual Bet &at(uint8_t i) = 0;  // i below size(); an empty slot is bet_empty
  virtual void changed() = 0;
};

// Puts b in its slot (1 to kBetSlots, replacing the bet there) or, for slot
// 0, in the first empty slot, so two device pages adding a bet at the same
// time each get their own. The slot it went in, 0 when every slot is in use.
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

// The bets entered on the device page, one per slot (1 to kBetSlots), saved
// in the panel's flash as one blob.
struct BetBlob {
  Bet bets[kBetSlots];
};

class ManualBets final : public BetSource {
 public:
  uint8_t size() const override { return kBetSlots; }
  Bet &at(uint8_t i) override { return this->blob.bets[i]; }
  void changed() override { this->dirty = true; }
  // From flash: a slot that doesn't make sense (a newer firmware's bet type,
  // a league this one doesn't know) is emptied.
  void loaded() {
    for (auto &b : this->blob.bets)
      if (!bet_empty(b) && !bet_valid(b))
        b = Bet{};
  }
  BetBlob blob{};
  bool dirty{false};  // changed since the last save
};

// The panel read g: each of the source's bets on it ties to it, keeps its
// details or takes its result.
inline void bets_observe(BetSource &src, const BetGame &g, int64_t now, int64_t midnight) {
  for (uint8_t i = 0; i < src.size(); i++)
    if (bet_observe(src.at(i), g, now, midnight))
      src.changed();
}

// The panel read g for another reason than following it: the big-play
// watcher's look at another game, or the board on that game's big play for a
// moment (watch.h). Only a bet tied to g already keeps its details and takes
// its result; none ties to g or moves to it, which only the games the panel
// follows do (bets_observe).
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

// Drops the source's bets that are done. True when one went.
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
