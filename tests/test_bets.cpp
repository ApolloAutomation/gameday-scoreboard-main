// Manual bets (components/gameday/bets.h): the lines the device page sends,
// where a bet stands at a score, which game it ties to, its result and when
// it goes. The bet= and betdel= keys themselves are in test_set.cpp and
// fixtures/set_cases.json.
#include <cstdio>
#include <string>

#include "bets.h"
#include "set_request.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define BCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

constexpr uint32_t BUF = 2, MIA = 15;  // NFL
constexpr int64_t T0 = 1793000000;      // a Sunday in November 2026
constexpr int64_t KICK = T0 + 3 * 3600;
constexpr int64_t MIDNIGHT = T0 + 12 * 3600;

Bet make(BetType type, BetSide side, int16_t line, uint32_t team = BUF, uint8_t league = (uint8_t) League::NFL) {
  Bet b{};
  b.team_id = team;
  b.league = league;
  b.type = (uint8_t) type;
  b.side = (uint8_t) side;
  b.line = line;
  b.placed = (uint32_t) T0;
  return b;
}

// BUF at home against MIA, oriented the way the board had it.
BetGame game(BetPhase phase, int buf, int mia, uint32_t event = 401772001, bool buf_first = true) {
  BetGame g;
  g.league = (uint8_t) League::NFL;
  g.event = event;
  g.team_id = buf_first ? BUF : MIA;
  g.opp_id = buf_first ? MIA : BUF;
  g.team_score = buf_first ? buf : mia;
  g.opp_score = buf_first ? mia : buf;
  g.kickoff = KICK;
  g.phase = phase;
  return g;
}

std::string line_of(const char *v, bool spread) {
  int16_t h = 12345;
  return parse_bet_line(v, spread, h) ? std::to_string(h) : "refused";
}

void test_lines() {
  BCHECK(line_of("-3.5", true) == "-7", "-3.5");
  BCHECK(line_of("+7", true) == "14", "+7");
  BCHECK(line_of("7", true) == "14", "7");
  BCHECK(line_of("0", true) == "0", "pick'em");
  BCHECK(line_of("-0.5", true) == "-1", "-0.5");
  BCHECK(line_of("3.0", true) == "6", "3.0");
  BCHECK(line_of("99.5", true) == "199" && line_of("-99.5", true) == "-199", "the largest spreads");
  BCHECK(line_of("100", true) == "refused", "a spread past 99.5");
  BCHECK(line_of("3.25", true) == "refused", "quarter points");
  BCHECK(line_of("3.", true) == "refused", "a point with nothing after it");
  BCHECK(line_of(".5", true) == "refused", "no whole part");
  BCHECK(line_of("", true) == "refused" && line_of("-", true) == "refused", "nothing");
  BCHECK(line_of(" 3", true) == "refused" && line_of("3 ", true) == "refused", "spaces");
  BCHECK(line_of("3.5x", true) == "refused", "trailing text");
  BCHECK(line_of("--3", true) == "refused" && line_of("+-3", true) == "refused", "two signs");
  BCHECK(line_of("pk", true) == "refused", "PK is the page's to turn into 0");
  BCHECK(line_of("00000007", true) == "refused", "too many digits");
  BCHECK(line_of("47.5", false) == "95", "a total");
  BCHECK(line_of("47", false) == "94", "a whole total");
  BCHECK(line_of("0.5", false) == "1", "the smallest total");
  BCHECK(line_of("999.5", false) == "1999", "the largest total");
  BCHECK(line_of("1000", false) == "refused", "a total past 999.5");
  BCHECK(line_of("0", false) == "refused", "a total of 0");
  BCHECK(line_of("+47.5", false) == "refused" && line_of("-47.5", false) == "refused", "a total has no sign");
}

void test_margin() {
  Bet minus = make(BetType::SPREAD, BetSide::TEAM, -7);  // BUF -3.5
  BCHECK(bet_margin(minus, 24, 20) == 1, "won by 4 covers -3.5");
  BCHECK(bet_margin(minus, 23, 20) == -1, "won by 3 doesn't cover -3.5");
  Bet three = make(BetType::SPREAD, BetSide::TEAM, -6);  // BUF -3
  BCHECK(bet_margin(three, 23, 20) == 0, "won by exactly 3 is a push on -3");
  Bet plus = make(BetType::SPREAD, BetSide::TEAM, 14);  // BUF +7
  BCHECK(bet_margin(plus, 14, 20) > 0, "lost by 6 covers +7");
  BCHECK(bet_margin(plus, 13, 20) == 0, "lost by 7 is a push on +7");
  BCHECK(bet_margin(plus, 10, 20) < 0, "lost by 10 doesn't cover +7");
  Bet over = make(BetType::TOTAL, BetSide::OVER, 95);  // over 47.5
  BCHECK(bet_margin(over, 28, 20) > 0 && bet_margin(over, 27, 20) < 0, "over 47.5");
  Bet under = make(BetType::TOTAL, BetSide::UNDER, 94);  // under 47
  BCHECK(bet_margin(under, 20, 20) > 0 && bet_margin(under, 27, 20) == 0 && bet_margin(under, 28, 20) < 0,
         "under 47");
  Bet ml = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  BCHECK(bet_margin(ml, 21, 17) > 0 && bet_margin(ml, 17, 21) < 0 && bet_margin(ml, 17, 17) == 0, "moneyline");
}

void test_status() {
  Bet b = make(BetType::SPREAD, BetSide::TEAM, -7);
  BCHECK(bet_status(b, nullptr, T0) == BetStatus::WAITING, "no game yet");
  BetGame pre = game(BetPhase::PRE, 0, 0);
  bet_observe(b, pre, T0, MIDNIGHT);
  BCHECK(bet_status(b, &pre, T0) == BetStatus::PRE, "before kickoff: the line only");
  BCHECK(std::string(bet_word(BetStatus::PRE)).empty(), "no word before kickoff");
  BetGame live = game(BetPhase::LIVE, 14, 7);
  BCHECK(bet_status(b, &live, KICK + 600) == BetStatus::COVERING, "up 7 covers -3.5");
  live = game(BetPhase::LIVE, 10, 7);
  BCHECK(bet_status(b, &live, KICK + 600) == BetStatus::NOT_COVERING, "up 3 doesn't cover -3.5");
  Bet three = make(BetType::SPREAD, BetSide::TEAM, -6);
  bet_observe(three, live, KICK, MIDNIGHT);
  BCHECK(bet_status(three, &live, KICK + 600) == BetStatus::LEVEL, "up exactly 3 on -3 is level");
  BCHECK(std::string(bet_word(BetStatus::LEVEL)) == "PUSH", "level says PUSH");
  // Oriented the other way: the board's "team" is MIA.
  BetGame flipped = game(BetPhase::LIVE, 14, 7, 401772001, false);
  BCHECK(bet_status(b, &flipped, KICK + 600) == BetStatus::COVERING, "BUF is the board's opponent");
  BetGame off = game(BetPhase::OFF, 0, 0);
  BCHECK(bet_status(b, &off, KICK) == BetStatus::VOID, "a postponed game");
  BetGame other = game(BetPhase::LIVE, 14, 7, 401772999);
  BCHECK(bet_status(b, &other, T0) == BetStatus::PRE, "another game: kickoff still ahead");
  BCHECK(bet_status(b, nullptr, KICK + 60) == BetStatus::UNREAD, "started, and the panel isn't reading it");
  BCHECK(std::string(bet_status_key(BetStatus::NOT_COVERING)) == "not_covering", "state document name");
  BCHECK(std::string(bet_status_key(BetStatus::VOID)) == "void", "last state document name");
}

void test_observe() {
  // Ties to the team's game before it, or during it.
  Bet b = make(BetType::SPREAD, BetSide::TEAM, -7);
  BCHECK(bet_observe(b, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT) && b.event == 401772001 && b.opp_id == MIA &&
             b.kickoff == (uint32_t) KICK,
         "ties to the team's next game");
  BCHECK(!bet_observe(b, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT), "the same read again changes nothing");
  Bet live = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  BCHECK(bet_observe(live, game(BetPhase::LIVE, 3, 0, 401772001, false), KICK, MIDNIGHT) && live.event == 401772001 &&
             live.opp_id == MIA,
         "ties to the game in progress, either side");
  // Another league's team with the same id, another team, no event id.
  Bet nba = make(BetType::MONEYLINE, BetSide::TEAM, 0, 2, (uint8_t) League::NBA);
  BCHECK(!bet_observe(nba, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT) && nba.event == 0, "Boston isn't Buffalo");
  Bet ari = make(BetType::MONEYLINE, BetSide::TEAM, 0, 22);
  BCHECK(!bet_observe(ari, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT), "another team's game");
  Bet noid = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  BCHECK(!bet_observe(noid, game(BetPhase::PRE, 0, 0, 0), T0, MIDNIGHT), "a game with no id it can keep");
  Bet empty{};
  BCHECK(!bet_observe(empty, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT), "an empty slot");

  // A final the panel sees first: started after the bet went in, it was the
  // bet's game; started before, it was likely over already.
  Bet late = make(BetType::SPREAD, BetSide::TEAM, -7);
  late.placed = (uint32_t) (KICK - 60);
  BCHECK(bet_observe(late, game(BetPhase::FINAL, 24, 20), KICK + 4 * 3600, MIDNIGHT) &&
             late.result == (uint8_t) BetResult::WON && late.event == 401772001,
         "a final that started after the bet grades it");
  Bet after = make(BetType::SPREAD, BetSide::TEAM, -7);
  after.placed = (uint32_t) (KICK + 3 * 3600);
  BCHECK(!bet_observe(after, game(BetPhase::FINAL, 24, 20), KICK + 4 * 3600, MIDNIGHT) && after.event == 0,
         "a final from before the bet: the next game");
  Bet noclock = make(BetType::SPREAD, BetSide::TEAM, -7);
  noclock.placed = 0;
  BCHECK(!bet_observe(noclock, game(BetPhase::FINAL, 24, 20), KICK + 4 * 3600, MIDNIGHT),
         "no clock when it went in: a final can't be told apart");
  BCHECK(bet_observe(noclock, game(BetPhase::PRE, 0, 0, 401772002), T0, MIDNIGHT) && noclock.event == 401772002,
         "but the next game takes it");
  Bet postponed = make(BetType::SPREAD, BetSide::TEAM, -7);
  BetGame off = game(BetPhase::OFF, 0, 0);
  BCHECK(bet_observe(postponed, off, T0 + 60, MIDNIGHT) && postponed.result == (uint8_t) BetResult::NONE &&
             bet_status(postponed, &off, KICK) == BetStatus::VOID,
         "a called-off game after the bet: void, not graded");
  Bet called = make(BetType::SPREAD, BetSide::TEAM, -7);
  called.placed = (uint32_t) (KICK + 60);
  BCHECK(!bet_observe(called, off, KICK + 120, MIDNIGHT) && called.event == 0, "a called-off game from before the bet");

  // Bound: another game of the team leaves it alone (a doubleheader).
  BCHECK(!bet_observe(b, game(BetPhase::LIVE, 7, 0, 401772555), KICK, MIDNIGHT) && b.event == 401772001,
         "the team's other game");
  // ESPN moves the game: the bet keeps it.
  BetGame moved = game(BetPhase::PRE, 0, 0);
  moved.kickoff = KICK + 86400;
  BCHECK(bet_observe(b, moved, T0, MIDNIGHT) && b.kickoff == (uint32_t) (KICK + 86400), "a game moved to another day");
  BCHECK(bet_observe(b, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT) && b.kickoff == (uint32_t) KICK, "and back");
  // During the game nothing is saved.
  BCHECK(!bet_observe(b, game(BetPhase::LIVE, 14, 7), KICK + 600, MIDNIGHT), "a live score isn't saved");

  // The final grades it, with the score, until midnight.
  BCHECK(bet_observe(b, game(BetPhase::FINAL, 23, 20), KICK + 86400 + 4 * 3600, MIDNIGHT + 86400) &&
             b.result == (uint8_t) BetResult::LOST && b.team_score == 23 && b.opp_score == 20 &&
             b.expires == (uint32_t) (MIDNIGHT + 86400),
         "lost at the final");
  BCHECK(!bet_observe(b, game(BetPhase::FINAL, 23, 20), KICK + 86400 + 5 * 3600, MIDNIGHT + 2 * 86400) &&
             b.expires == (uint32_t) (MIDNIGHT + 86400),
         "the same final again keeps its midnight");
  BCHECK(bet_observe(b, game(BetPhase::FINAL, 24, 20), KICK + 86400 + 5 * 3600, MIDNIGHT + 2 * 86400) &&
             b.result == (uint8_t) BetResult::WON && b.expires == (uint32_t) (MIDNIGHT + 86400),
         "a corrected score grades it again");
  BCHECK(bet_status(b, nullptr, KICK + 86400 * 2) == BetStatus::WON, "the result stays without the game");

  Bet push = make(BetType::TOTAL, BetSide::OVER, 94);  // over 47
  bet_observe(push, game(BetPhase::PRE, 0, 0, 401772001, false), T0, MIDNIGHT);
  BCHECK(bet_observe(push, game(BetPhase::FINAL, 27, 20), KICK + 4 * 3600, MIDNIGHT) &&
             push.result == (uint8_t) BetResult::PUSH,
         "exactly 47: a push");
  // A final right at midnight still gets a moment on the page.
  Bet edge = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  bet_observe(edge, game(BetPhase::FINAL, 1, 0), KICK + 3600, KICK + 3600);
  BCHECK(edge.result == (uint8_t) BetResult::WON && edge.expires == (uint32_t) (KICK + 3601), "a midnight not ahead");
}

// A bet is for its team's next or current game, whichever of the team's
// games the panel reads first.
BetGame nba(BetPhase phase, uint32_t event, uint32_t home, uint32_t away, int64_t kickoff, int hs = 0, int as = 0) {
  BetGame g;
  g.league = (uint8_t) League::NBA;
  g.event = event;
  g.team_id = home;
  g.opp_id = away;
  g.team_score = hs;
  g.opp_score = as;
  g.kickoff = kickoff;
  g.phase = phase;
  return g;
}

void test_earliest_game() {
  constexpr uint32_t BOS = 2, MIA_H = 14, NYK = 18;
  const int64_t mon = T0 + 8 * 3600, wed = mon + 2 * 86400;  // BOS at NYK Monday night, MIA vs BOS Wednesday
  // My team is MIA: its next game, MIA vs BOS on Wednesday, is on the board
  // when the owner enters BOS -4.5 on Monday morning for BOS at NYK that night.
  Bet b = make(BetType::SPREAD, BetSide::TEAM, -9, BOS, (uint8_t) League::NBA);
  BCHECK(bet_observe(b, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT) && b.event == 401800002,
         "the first BOS game the panel reads takes the bet");
  BCHECK(bet_observe(b, nba(BetPhase::PRE, 401800001, NYK, BOS, mon), T0 + 60, MIDNIGHT) && b.event == 401800001 &&
             b.opp_id == NYK && b.kickoff == (uint32_t) mon,
         "BOS's earlier game takes it from the later one");
  BCHECK(!bet_observe(b, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0 + 120, MIDNIGHT) && b.event == 401800001,
         "the later game doesn't take it back");
  BetGame live = nba(BetPhase::LIVE, 401800001, NYK, BOS, mon, 90, 100);
  BCHECK(!bet_observe(b, live, mon + 3600, MIDNIGHT) && bet_status(b, &live, mon + 3600) == BetStatus::COVERING,
         "up 10 at NYK covers -4.5, and the ticker of that game shows it");
  BCHECK(bet_observe(b, nba(BetPhase::FINAL, 401800001, NYK, BOS, mon, 104, 108), mon + 3 * 3600, MIDNIGHT) &&
             b.result == (uint8_t) BetResult::LOST,
         "graded at Monday's final, a 4 point win");
  BCHECK(!bet_observe(b, nba(BetPhase::FINAL, 401800002, MIA_H, BOS, wed, 110, 100), wed + 3 * 3600, MIDNIGHT) &&
             b.result == (uint8_t) BetResult::LOST && b.event == 401800001,
         "Wednesday's final leaves it alone");

  // Read in the other order: the earlier game keeps it.
  Bet o = make(BetType::SPREAD, BetSide::TEAM, -9, BOS, (uint8_t) League::NBA);
  bet_observe(o, nba(BetPhase::PRE, 401800001, NYK, BOS, mon), T0, MIDNIGHT);
  BCHECK(!bet_observe(o, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT) && o.event == 401800001,
         "a later game read second doesn't take it");

  // The earlier game already on, or a final the panel missed live that
  // started after the bet went in: still the team's game.
  Bet on = make(BetType::MONEYLINE, BetSide::TEAM, 0, BOS, (uint8_t) League::NBA);
  bet_observe(on, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT);
  BCHECK(bet_observe(on, nba(BetPhase::LIVE, 401800001, NYK, BOS, mon, 50, 40), mon + 3600, MIDNIGHT) &&
             on.event == 401800001,
         "the game in progress takes it");
  Bet missed = make(BetType::MONEYLINE, BetSide::TEAM, 0, BOS, (uint8_t) League::NBA);
  bet_observe(missed, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT);
  BCHECK(bet_observe(missed, nba(BetPhase::FINAL, 401800001, NYK, BOS, mon, 99, 101), mon + 4 * 3600, MIDNIGHT) &&
             missed.event == 401800001 && missed.result == (uint8_t) BetResult::WON,
         "a final after the bet takes it and grades it");
  Bet before = make(BetType::MONEYLINE, BetSide::TEAM, 0, BOS, (uint8_t) League::NBA);
  before.placed = (uint32_t) (mon + 4 * 3600);
  bet_observe(before, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), mon + 4 * 3600, MIDNIGHT);
  BCHECK(!bet_observe(before, nba(BetPhase::FINAL, 401800001, NYK, BOS, mon, 99, 101), mon + 5 * 3600, MIDNIGHT) &&
             before.event == 401800002,
         "a final from before the bet doesn't");

  // Once its game has started, or has a result, the bet stays on it.
  Bet started = make(BetType::MONEYLINE, BetSide::TEAM, 0, BOS, (uint8_t) League::NBA);
  bet_observe(started, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT);
  BCHECK(!bet_observe(started, nba(BetPhase::FINAL, 401800001, NYK, BOS, mon, 99, 101), wed + 60, MIDNIGHT) &&
             started.event == 401800002,
         "its game has started");
  // A game ESPN moved later stays the bet's game, even past another of the team's.
  Bet moved = make(BetType::MONEYLINE, BetSide::TEAM, 0, BOS, (uint8_t) League::NBA);
  bet_observe(moved, nba(BetPhase::PRE, 401800001, NYK, BOS, mon), T0, MIDNIGHT);
  BCHECK(bet_observe(moved, nba(BetPhase::PRE, 401800001, NYK, BOS, wed + 86400), T0 + 60, MIDNIGHT) && moved.moved,
         "ESPN moves the game past Wednesday");
  BCHECK(!bet_observe(moved, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0 + 120, MIDNIGHT) &&
             moved.event == 401800001 && moved.kickoff == (uint32_t) (wed + 86400),
         "Wednesday's game doesn't take it");

  // In one pass over the board and the favorites' cards, the order the panel
  // reads them doesn't matter.
  ManualBets m;
  m.at(0) = make(BetType::SPREAD, BetSide::TEAM, -9, BOS, (uint8_t) League::NBA);
  bets_observe(m, nba(BetPhase::PRE, 401800002, MIA_H, BOS, wed), T0, MIDNIGHT);
  bets_observe(m, nba(BetPhase::PRE, 401800001, NYK, BOS, mon), T0, MIDNIGHT);
  BCHECK(m.dirty && m.at(0).event == 401800001, "the earliest game read in the pass");
}

void test_put() {
  Bet blob[kBetSlots]{};
  Bet a = make(BetType::SPREAD, BetSide::TEAM, -7);
  Bet c = make(BetType::MONEYLINE, BetSide::TEAM, 0, MIA);
  BCHECK(bet_put(blob, 0, a) == 1 && blob[0].line == -7, "slot 0: the first empty slot");
  BCHECK(bet_put(blob, 3, c) == 3 && blob[2].team_id == MIA, "slot 3: that slot");
  BCHECK(bet_put(blob, 0, c) == 2 && blob[1].team_id == MIA, "slot 0 again: the next empty one");
  BCHECK(bet_put(blob, 0, a) == 4, "and past the ones in use");
  BCHECK(bet_put(blob, 1, c) == 1 && blob[0].team_id == MIA, "a slot named replaces its bet");
  for (int i = 4; i < kBetSlots; i++)
    bet_put(blob, 0, a);
  Bet before[kBetSlots];
  for (int i = 0; i < kBetSlots; i++)
    before[i] = blob[i];
  BCHECK(bet_put(blob, 0, c) == 0, "every slot in use: nowhere");
  bool same = true;
  for (int i = 0; i < kBetSlots; i++)
    same = same && blob[i].team_id == before[i].team_id && blob[i].type == before[i].type;
  BCHECK(same, "and nothing changes");
  BCHECK(bet_put(blob, kBetSlots + 1, a) == 0, "a slot past the blob");
}

void test_expire() {
  Bet b = make(BetType::SPREAD, BetSide::TEAM, -7);
  BCHECK(!bet_expired(b, T0 + 365 * 86400), "a bet with no game waits");
  bet_observe(b, game(BetPhase::PRE, 0, 0), T0, MIDNIGHT);
  BCHECK(!bet_expired(b, KICK + kBetUnreadSeconds - 1), "its game not read since, a day and a half on");
  BCHECK(bet_expired(b, KICK + kBetUnreadSeconds), "a day and a half after the start with no final");
  bet_observe(b, game(BetPhase::FINAL, 30, 3), KICK + 4 * 3600, MIDNIGHT);
  BCHECK(!bet_expired(b, MIDNIGHT - 1), "the result stays until midnight");
  BCHECK(bet_expired(b, MIDNIGHT), "and goes at midnight");
  Bet empty{};
  BCHECK(!bet_expired(empty, T0), "an empty slot");
}

void test_text() {
  BCHECK(bet_label(make(BetType::SPREAD, BetSide::TEAM, -7)) == "BUF -3.5", "BUF -3.5");
  BCHECK(bet_label(make(BetType::SPREAD, BetSide::TEAM, 14)) == "BUF +7", "BUF +7");
  BCHECK(bet_label(make(BetType::SPREAD, BetSide::TEAM, 0)) == "BUF PK", "pick'em");
  BCHECK(bet_label(make(BetType::SPREAD, BetSide::TEAM, -1)) == "BUF -0.5", "half a point");
  BCHECK(bet_label(make(BetType::TOTAL, BetSide::OVER, 95)) == "OVER 47.5", "over");
  BCHECK(bet_label(make(BetType::TOTAL, BetSide::UNDER, 94)) == "UNDER 47", "under");
  BCHECK(bet_label(make(BetType::MONEYLINE, BetSide::TEAM, 0)) == "BUF ML", "moneyline");
  Bet b = make(BetType::SPREAD, BetSide::TEAM, -7);
  BCHECK(bet_ticker(b, BetStatus::PRE) == "BET BUF -3.5", "before the start");
  BCHECK(bet_ticker(b, BetStatus::COVERING) == "BUF -3.5 COVERING", "covering");
  BCHECK(bet_ticker(b, BetStatus::NOT_COVERING) == "BUF -3.5 NOT COVERING", "not covering");
  BCHECK(bet_ticker(make(BetType::TOTAL, BetSide::OVER, 95), BetStatus::LEVEL) == "OVER 47.5 PUSH", "level");
  BCHECK(bet_ticker(make(BetType::MONEYLINE, BetSide::TEAM, 0), BetStatus::WON) == "BUF ML WON", "won");
  BCHECK(bet_ticker(b, BetStatus::LOST) == "BUF -3.5 LOST" && bet_ticker(b, BetStatus::PUSH) == "BUF -3.5 PUSH" &&
             bet_ticker(b, BetStatus::VOID) == "BUF -3.5 VOID",
         "after the final");
  BCHECK(std::string(bet_type_key(make(BetType::TOTAL, BetSide::UNDER, 94))) == "under" &&
             std::string(bet_type_key(make(BetType::MONEYLINE, BetSide::TEAM, 0))) == "ml",
         "type keys");
}

void test_event_id() {
  BCHECK(bet_event_id("401772001") == 401772001u, "an NFL id");
  BCHECK(bet_event_id("704512") == 704512u, "a soccer id");
  BCHECK(bet_event_id("4294967295") == 4294967295u, "the largest that fits");
  BCHECK(bet_event_id("4294967296") == 0, "past 32 bits");
  BCHECK(bet_event_id("") == 0 && bet_event_id("12a") == 0 && bet_event_id("-1") == 0, "not an id");
}

void test_valid() {
  BCHECK(bet_valid(make(BetType::SPREAD, BetSide::TEAM, -7)), "a spread");
  BCHECK(!bet_valid(make(BetType::SPREAD, BetSide::OVER, -7)), "a spread with a side");
  BCHECK(!bet_valid(make(BetType::TOTAL, BetSide::TEAM, 95)), "a total with no side");
  BCHECK(!bet_valid(make(BetType::TOTAL, BetSide::OVER, -95)), "a negative total");
  BCHECK(!bet_valid(make(BetType::MONEYLINE, BetSide::TEAM, 3)), "a moneyline with a line");
  BCHECK(!bet_valid(make((BetType) 7, BetSide::TEAM, 0)), "a type this firmware doesn't know");
  BCHECK(!bet_valid(make(BetType::MONEYLINE, BetSide::TEAM, 0, 1106, (uint8_t) League::PGA)), "a golf tour");
  BCHECK(!bet_valid(make(BetType::MONEYLINE, BetSide::TEAM, 0, 99999)), "a team not in the list");
  BCHECK(!bet_valid(make(BetType::MONEYLINE, BetSide::TEAM, 0, BUF, 200)), "a league byte past the list");
  Bet r = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  r.result = 9;
  BCHECK(!bet_valid(r), "a result this firmware doesn't know");
  Bet mv = make(BetType::MONEYLINE, BetSide::TEAM, 0);
  mv.moved = 1;
  BCHECK(bet_valid(mv), "a bet whose game moved");
  mv.moved = 2;
  BCHECK(!bet_valid(mv), "a moved flag this firmware doesn't know");
}

void test_parse_bet() {
  SetOp op;
  BCHECK(parse_set("bet", "1,nfl,2,spread,-3.5", op, 16) && op.kind == SetKind::BET && op.slot == 1 &&
             op.bet.league == (uint8_t) League::NFL && op.bet.team_id == BUF &&
             op.bet.type == (uint8_t) BetType::SPREAD && op.bet.line == -7 && op.bet.event == 0,
         "a spread");
  BCHECK(parse_set("bet", "8,nfl,2,over,47.5", op, 16) && op.slot == 8 && op.bet.type == (uint8_t) BetType::TOTAL &&
             op.bet.side == (uint8_t) BetSide::OVER && op.bet.line == 95,
         "an over in the last slot");
  BCHECK(parse_set("bet", "2,nba,2,under,221", op, 16) && op.bet.side == (uint8_t) BetSide::UNDER &&
             op.bet.line == 442 && op.bet.league == (uint8_t) League::NBA,
         "an under");
  BCHECK(parse_set("bet", "0,nfl,2,spread,+7", op, 16) && op.slot == 0 && op.bet.line == 14,
         "slot 0, the first empty slot");
  BCHECK(parse_set("bet", "3,epl,359,ml,", op, 16) && op.bet.type == (uint8_t) BetType::MONEYLINE &&
             op.bet.line == 0 && bet_valid(op.bet),
         "a moneyline");
  const char *refused[] = {
      "",
      "9,nfl,2,spread,-3.5",
      "10,nfl,2,spread,-3.5",
      "1x,nfl,2,spread,-3.5",
      " 1,nfl,2,spread,-3.5",
      "1,NFL,2,spread,-3.5",
      "1,xfl,2,spread,-3.5",
      "1,pga,1106,ml,",
      "1,nfl,99999,spread,-3.5",
      "1,nfl,+2,spread,-3.5",
      "1,nfl,2x,spread,-3.5",
      "1,nfl,4294967298,spread,-3.5",
      "1,nfl,,spread,-3.5",
      "1,nfl,2,Spread,-3.5",
      "1,nfl,2,total,47.5",
      "1,nfl,2,spread,",
      "1,nfl,2,spread,3.25",
      "1,nfl,2,over,-47.5",
      "1,nfl,2,over,",
      "1,nfl,2,ml,0",
      "1,nfl,2,ml",
      "1,nfl,2,spread,-3.5,",
      "1,nfl,2,spread,-3.5,x",
      "1,nfl,2,spread",
      "1,nfl:2,spread,-3.5",
  };
  for (const char *v : refused)
    BCHECK(!parse_set("bet", v, op, 16), "bet=%s is refused", v);
  BCHECK(parse_set("betdel", "1", op, 16) && op.kind == SetKind::BET_DEL && op.slot == 1, "betdel 1");
  BCHECK(parse_set("betdel", "8", op, 16) && op.slot == 8, "betdel 8");
  for (const char *v : {"", "0", "9", "01", "1 ", "all", "-1"})
    BCHECK(!parse_set("betdel", v, op, 16), "betdel=%s is refused", v);
}

// The source the device page fills: changed() marks it for saving.
void test_manual_source() {
  ManualBets m;
  BCHECK(m.size() == kBetSlots && sizeof(BetBlob) == 36 * kBetSlots, "eight slots of 36 bytes");
  m.at(0) = make(BetType::SPREAD, BetSide::TEAM, -7);
  m.at(3) = make(BetType::TOTAL, BetSide::OVER, 95, MIA);
  m.at(5) = make(BetType::MONEYLINE, BetSide::TEAM, 0, 22);
  BetSource &src = m;
  bets_observe(src, game(BetPhase::LIVE, 7, 3), KICK, MIDNIGHT);
  BCHECK(m.dirty && m.at(0).event == 401772001 && m.at(3).event == 401772001 && m.at(5).event == 0,
         "both bets on the game tie to it, the other waits");
  m.dirty = false;
  bets_observe(src, game(BetPhase::LIVE, 14, 3), KICK + 600, MIDNIGHT);
  BCHECK(!m.dirty, "a new score isn't saved");
  bets_observe(src, game(BetPhase::FINAL, 31, 20), KICK + 4 * 3600, MIDNIGHT);
  BCHECK(m.dirty && m.at(0).result == (uint8_t) BetResult::WON && m.at(3).result == (uint8_t) BetResult::WON,
         "both graded at the final");
  m.dirty = false;
  BCHECK(!bets_expire(src, MIDNIGHT - 1) && !m.dirty, "nothing done before midnight");
  BCHECK(bets_expire(src, MIDNIGHT) && m.dirty && bet_empty(m.at(0)) && bet_empty(m.at(3)) && !bet_empty(m.at(5)),
         "midnight drops the graded ones");
  // Read back from flash: a slot that makes no sense is emptied.
  m.at(1) = make(BetType::SPREAD, BetSide::UNDER, -7);
  m.at(2) = make(BetType::SPREAD, BetSide::TEAM, -7);
  m.loaded();
  BCHECK(bet_empty(m.at(1)) && !bet_empty(m.at(2)) && !bet_empty(m.at(5)), "a bad slot read back is emptied");
}

}  // namespace

int run_bet_tests(int &total_checks) {
  test_lines();
  test_margin();
  test_status();
  test_observe();
  test_earliest_game();
  test_put();
  test_expire();
  test_text();
  test_event_id();
  test_valid();
  test_parse_bet();
  test_manual_source();
  total_checks += checks;
  return failures;
}
