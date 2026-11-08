// Pure ESPN parsing and game logic. No ESPHome includes so the host test
// binary can compile this file directly.
#pragma once

#include <climits>
#include <cstdint>
#include <ctime>
#include <string>
#include <type_traits>
#include <vector>

#include "teams.h"

namespace espn {

enum class GameState : uint8_t { NOT_FOUND, PRE, IN, POST };

// What the team endpoint tells us about the next game.
struct Schedule {
  bool valid{false};
  uint8_t league{0};  // League as uint8_t; set for live-mode games
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet: kickoff_epoch is ESPN's midnight Eastern placeholder
  uint32_t group{0};
  std::string team_color;
  std::string team_record;
  const char *comp_slug{nullptr};  // a kCups slug when the game is in that cup, nullptr for the league
  bool next_final{false};          // the team endpoint's next event is already over (ESPN lags)
  // The other side of event_id, so its logo can download early. 0 when the
  // team endpoint did not say.
  uint32_t opp_id{0};
  std::string opp_abbr;
};

enum class Half : uint8_t { NONE, TOP, MID, BOTTOM, END };

// Baseball's live state. Zero or -1 when the document has none (pre-game,
// finals, between half innings).
struct Baseball {
  static constexpr uint8_t FIRST = 1, SECOND = 2, THIRD = 4;
  int8_t balls{-1}, strikes{-1}, outs{-1};
  uint8_t bases{0};  // FIRST | SECOND | THIRD
  Half half{Half::NONE};
  int team_hits{0}, opp_hits{0}, team_errors{0}, opp_errors{0};
  std::string batter, pitcher;               // "A. Riley", while a half inning is on
  std::string team_probable, opp_probable;   // starting pitchers, pre-game
  std::string series;                        // "LAD lead series 2-1", postseason
  std::string play_type;                     // last play's type: "Home Run", "Ball"
};

// One goal from a soccer event's details. Names are plain ASCII (the panel
// fonts have no accents) with the initial dropped: "Saka", "Alan Patrick".
struct SoccerGoal {
  std::string clock;  // "52'", "45'+3'"
  std::string name;
  bool ours{false};  // counts for our side; an own goal counts for the side it helped
  bool own_goal{false}, penalty{false};
};

// Soccer's goals and cards (the event's details), form, shootout, match
// stats and three-way odds. Empty, zero or -1 when the document has none.
struct Soccer {
  std::string status;             // ESPN's status name: "STATUS_HALFTIME", "STATUS_FINAL_PEN"
  std::vector<SoccerGoal> goals;  // in match order
  int team_reds{0}, opp_reds{0}, team_yellows{0}, opp_yellows{0};
  std::string last_event;                      // newest goal or card: "85' Yellow Card: R. Garro (COR)"
  std::string team_form, opp_form;             // last five results, oldest first: "WWWWL"
  int team_shootout{-1}, opp_shootout{-1};     // penalty shootout goals
  std::string team_line, draw_line, opp_line;  // moneyline before kickoff: "-275", "+425", "+700"
  std::string team_possession, opp_possession;  // percent: "58.1"
  int team_shots{-1}, opp_shots{-1}, team_on_target{-1}, opp_on_target{-1};
  std::string note;  // "UEFA Champions League, League Phase"
};

// Where a basketball game is, from ESPN's status name.
enum class Phase : uint8_t { OTHER, PLAY, END_PERIOD, HALFTIME };

// Basketball (NBA, WNBA, men's college). The series fields are set only for
// a playoff game.
struct Basketball {
  uint8_t regulation{4};  // periods before overtime: 4 quarters, or 2 halves in college
  Phase phase{Phase::OTHER};
  std::string series;                  // ESPN's line: "ATL leads series 1-0"
  int8_t team_wins{-1}, opp_wins{-1};  // series wins so far, -1 outside a series
};

// Hockey's extras. ESPN's period 5 is the shootout in a regular-season or
// preseason game and 2OT in the playoffs.
struct Hockey {
  bool playoffs{false};  // season type 3
  std::string series;    // "CAR leads series 2-0", playoffs
};

// Golf (sport_golf.cpp): the tour's current event, or its next one, from
// its scoreboard header. GameSnapshot's shared fields carry the event:
// event_id, game_name (event name, plain ASCII), kickoff_epoch (first day,
// midnight Eastern), period (round), short_detail (ESPN's "Round 2 -
// Suspended"), venue (course and city), tv, completed (STATUS_FINAL).
// OFF is a canceled or postponed event.
enum class GolfPhase : uint8_t { PRE, LIVE, BREAK, FINAL, OFF };
// ESPN's status name, for the words: STATUS_SUSPENDED is SUSPENDED.
enum class GolfStatus : uint8_t { OTHER, SCHEDULED, IN_PROGRESS, PLAY_COMPLETE, SUSPENDED, DELAYED, FINAL, CANCELED,
                                  POSTPONED };
static const size_t kGolfRows = 10;      // kept from the header (ticker, device page)
static const size_t kGolfBoardRows = 6;  // drawn on the panel, both widths
// A leaderboard's top row on a 128-wide board: the title and the status cell,
// a space between, in spleen_8 (5 px a character).
static const size_t kBoardTitleRow = 25;

// Plain characters, no std::string, so the rows copy as a block: a snapshot
// is copied and moved all over the component, and every board that isn't
// golf carries an empty list of these.
struct GolfRow {
  uint8_t place{0};       // ESPN's place, shared by a tie; 0 when it gave none
  bool tie{false};        // another listed player has the same place
  bool par_known{false};  // score was a number or "E"
  char state{'p'};        // 'p' not started this round, 'i' on the course, 'f' finished it
  uint8_t thru{0};        // holes played this round
  bool winner{false};     // won the event (final): place 1 held alone, or ESPN's flag
  int16_t to_par{0};
  int64_t tee_epoch{0};   // this round's (or the next round's) tee time, 0 for none
  char name[24]{};        // last name, ascii_fold()ed, at most 23 chars: "McIlroy", "Neergaard-Petersen"
  char score[5]{};        // to par as ESPN writes it, at most 4 chars: "-12", "E", "+3"
  char tee[8]{};          // "8:27p" in the panel's time zone; filled on the main loop
  void set_tee(const std::string &t) { put(this->tee, sizeof(this->tee), t); }
  static void put(char *dst, size_t cap, const std::string &s) {
    size_t n = s.size() < cap ? s.size() : cap - 1;
    s.copy(dst, n);
    dst[n] = '\0';
  }
};

static_assert(std::is_trivially_copyable<GolfRow>::value, "golf rows copy as a block");

// The event after the one on the board, from the season calendar: a golf
// tour's next tournament or a racing series' next race weekend.
struct GolfNext {
  bool valid{false};
  std::string event_id, name;  // name ascii_fold()ed
  int64_t start_epoch{0}, end_epoch{0};
};

struct Golf {
  GolfPhase phase{GolfPhase::PRE};
  GolfStatus status{GolfStatus::OTHER};
  bool playoff{false};        // hadPlayoff, or a listed player's status.playoff
  int64_t end_epoch{0};       // the event's last day, midnight Eastern
  char purse[16]{};           // "$6,000,000"
  std::string defending;      // "Ben Griffin"
  std::vector<GolfRow> rows;  // leaderboard order, at most kGolfRows
  GolfNext next;              // after a final, canceled event or none listed
  bool next_unknown{false};   // after a final or a canceled event, the calendar read failed
};

// Racing (sport_racing.cpp): a series' race weekend from its scoreboard
// header, one header entry per session. GameSnapshot's shared fields carry
// the event: event_id, game_name (ESPN's full name, plain ASCII),
// kickoff_epoch (the first session's start), venue (circuit), tv, period (the
// shown session's lap), short_detail (its ESPN status), completed (the race
// is final). Phases: PRE nothing has run yet (the card), LIVE a session on
// track, BREAK a finished session's order while the next one waits, FINAL the
// race's result, OFF the race canceled or postponed.
using RacePhase = GolfPhase;
enum class RaceKind : uint8_t { RACE, PRACTICE, QUALIFYING, SPRINT_QUALIFYING, SPRINT };
static const size_t kRaceRows = 10;  // kept: the ticker's top ten, the device page
// The next race this far off or more is the off-season clock, not its card.
// F1 leaves 12 to 33 days between races and about 90 between seasons.
static const int64_t kRaceSeasonGap = 45 * 24 * 3600;

struct RaceRow {          // plain characters, copied as a block like GolfRow
  uint8_t place{0};       // 0 when ESPN gave none
  bool winner{false};     // won the race (FINAL only): ESPN's flag or place 1
  char out{0};            // 'R' retired (DNF), 'D' disqualified (DSQ), 0 otherwise
  uint16_t laps_down{0};  // |behindLaps|, race sessions only, at most 999
  char name[20]{};        // last name, ascii_fold()ed: "Verstappen", "van Gisbergen", "Stenhouse Jr."
  char num[4]{};          // car number: "1", "24"
  char gap[12]{};         // race: ESPN's behindTime when it starts with '+' ("+13.078",
                          // "+1:20.588"); timed sessions: from the lap times ("+0.253"),
                          // and the leader's own time ("1:10.763"); "" none
};
static_assert(std::is_trivially_copyable<RaceRow>::value, "race rows copy as a block");

struct Racing {
  RacePhase phase{RacePhase::PRE};
  GolfStatus status{GolfStatus::OTHER};  // the shown session's ESPN status (the race's when OFF)
  RaceKind kind{RaceKind::RACE};         // the session on the board (before any ran: the next one)
  uint8_t number{0};                     // its practice number, 1-3
  char flag{0};                          // 'R' red flag, 'Y' yellow or caution (fullStatus.flag)
  uint16_t laps{0};                      // the shown session's scheduled laps, 0 unknown
  int64_t session_start{0};              // the shown session's start, 0 before any ran
  int64_t race_epoch{0};                 // the race's start (or the last session's when there is no race)
  RaceKind next_kind{RaceKind::RACE};
  uint8_t next_number{0};
  int64_t next_start{0};                 // the weekend's next session, 0 for none
  std::vector<RaceRow> rows;             // the shown session's order, at most kRaceRows
  GolfNext next;                         // after the final or a canceled race: from the calendar
  bool next_unknown{false};              // the calendar read failed after a final
};

// One scoreboard event, resolved to "us" and "them".
struct GameSnapshot {
  // Copied and moved all over the component: one out-of-line copy of each
  // (espn_parse.cpp), not one inlined into every file that moves one.
  GameSnapshot() = default;
  GameSnapshot(const GameSnapshot &);
  GameSnapshot(GameSnapshot &&) noexcept;
  GameSnapshot &operator=(const GameSnapshot &);
  GameSnapshot &operator=(GameSnapshot &&) noexcept;
  ~GameSnapshot();
  bool valid{false};
  GameState state{GameState::NOT_FOUND};
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet (see Schedule::time_tbd)
  bool completed{false};
  std::string short_detail;
  std::string status_name;  // ESPN's status.type.name, like STATUS_HALFTIME
  std::string display_clock;
  int period{0};
  std::string team_abbr, opp_abbr;
  uint32_t team_id{0}, opp_id{0};
  int team_score{0}, opp_score{0};
  std::string team_record, opp_record;
  std::string team_color, opp_color;
  std::string team_logo, opp_logo;
  bool team_winner{false};
  int possession{0};
  int team_timeouts{-1}, opp_timeouts{-1};  // -1: no count (see carry_football)
  uint8_t to_guess{0};     // bit 0 ours, bit 1 theirs: a count the panel filled in, not ESPN's
  // Football breaks (carry_football): held from a break until the next play,
  // the last play seen in it, that play's clock in seconds (-1 none), and the
  // drive (downs, possession, score) when the break began.
  bool held{false};
  std::string break_play;
  int break_secs{-1};
  std::string break_drive;
  bool resumed{false};  // football: the second half's plays came in while ESPN's status still says halftime
  std::string last_play, down_distance;
  std::string short_down_distance;  // "3rd & 10"
  bool is_red_zone{false};
  std::string odds, over_under, tv, venue;
  // Other sports. Football leaves these at their defaults.
  Sport sport{Sport::FOOTBALL};
  bool team_home{false};
  Baseball mlb;
  Soccer soc;
  Basketball nba;  // every basketball league
  Hockey nhl;
  Golf golf;
  Racing race;
  std::string game_name;  // "Rose Bowl", "Super Bowl LX"; empty for most games
};

// One future game from the team's schedule endpoint, for the "Up next" list.
struct Upcoming {
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet (see Schedule::time_tbd)
  uint32_t opp_id{0};
  std::string opp_abbr, opp_name;
  bool home{false};     // we are the home side
  bool neutral{false};  // neutral site
  std::string tv;
};

// A game found by scanning a day's scoreboard (live-game modes).
struct LiveGame {
  uint8_t league{0};
  std::string event_id;
  uint32_t group{0};        // home team's conference, for the follow-up polls
  uint32_t away_id{0};      // shown on the left, like a broadcast
  uint32_t home_id{0};      // for its logo, fetched as soon as the game is picked
  std::string away_abbr, home_abbr;
};

// A finished game from a day's scoreboard, for the finals rotation (recap.h).
// Kept small, since a few dozen of them stay in RAM: short strings only,
// and the logos are worked out from the ids when the card goes up.
struct Final {
  uint8_t league{0};
  uint8_t rank{99};  // the better AP rank of the two sides, 99 when neither is ranked
  int64_t kickoff_epoch{0};
  std::string detail;  // "Final", "Final/OT"
  uint32_t away_id{0}, home_id{0};
  int away_score{0}, home_score{0};
  std::string away_abbr, home_abbr;
  std::string away_color, home_color;
  std::string away_record, home_record;
};

// The scoreboard card for a final, the away side as "team" like a broadcast.
GameSnapshot final_snapshot(const Final &f);
// What a team change puts on the board until the first read lands: the new
// team's logo and name, no game. It is valid only so the page draws the
// logo; a failed read drops it (is_team_placeholder) so the board says why.
GameSnapshot team_placeholder(const Team &t);
bool is_team_placeholder(const GameSnapshot &g);

// A game later today from a day's scoreboard, for the rotation's "Later
// today" cards (recap.h). As small as a Final: short strings and ids, the
// logos worked out from the ids when the card goes up.
struct Pending {
  uint8_t league{0};
  uint8_t rank{99};  // the better AP rank of the two sides, 99 when neither is ranked
  int64_t kickoff_epoch{0};
  std::string event_id;
  uint32_t away_id{0}, home_id{0};
  std::string away_abbr, home_abbr;
  std::string away_color, home_color;
  std::string away_record, home_record;
  std::string tv;  // the first network ESPN lists, "" when it lists none
};

// One scan keeps at most this many of a league's games later today: the
// most a loop of the rotation shows.
static const size_t kPendingKeep = 8;

// Ranked games first (best rank on either side), then by kickoff.
inline bool pending_before(const Pending &a, const Pending &b) {
  if (a.rank != b.rank)
    return a.rank < b.rank;
  return a.kickoff_epoch < b.kickoff_epoch;
}

// The pregame scoreboard card for a game later today, away side as "team".
GameSnapshot pending_snapshot(const Pending &p);

struct TickerOptions {
  bool clock{true};
  bool down_distance{true};   // football
  bool baseball_count{true};  // count, outs, pitcher and batter
  bool soccer_stats{true};    // goals, cards, possession and shots
  bool last_play{true};
  bool odds{true};
};

struct Splash {
  std::string text;
  uint32_t color{0};
};

const char *league_path(League league);  // the last part of the ESPN path: "nfl"
std::string team_option(const Team &t);  // "NFL: Dallas Cowboys", the Home Assistant option
std::string team_url(League league, uint32_t espn_id);
std::string schedule_url(League league, uint32_t espn_id);  // the season's games, ~200KB
// time_tbd: kickoff_epoch is a TBD game's midnight Eastern placeholder.
std::string scoreboard_url(League league, uint32_t group, int64_t kickoff_epoch, bool time_tbd = false);
std::string scan_url(League league, int64_t now_epoch);  // every game of the day for one league
// How far behind UTC the day scan_url() reads turns over. Football uses the
// Eastern date. The other leagues play past midnight Eastern on the West
// Coast (ESPN lists those games under the day they started, and rejects date
// ranges), and none start between midnight and 6 AM Eastern: their day turns
// over at 4 AM Eastern instead.
inline int64_t scan_day_shift(League league) {
  return league_sport(league) == Sport::FOOTBALL ? 5 * 3600 : 9 * 3600;
}
// One game on its own, 8 to 20 KB, where the day's scoreboard it sits in is
// up to 260 KB (NFL) or 750 KB (college). A LeagueInfo::per_event league
// polls this.
std::string event_url(League league, const std::string &event_id);
// One day's scoreboard for finding a team's next game (Eastern date of
// day_epoch). College football and men's college basketball read the team's
// conference only (130 KB for the SEC's Saturday, 835 KB for every FBS game).
std::string team_day_url(League league, uint32_t group, int64_t day_epoch);
// The same in a cup of the league (kCups): soccer/uefa.champions/... A null
// slug gives the league's own URL.
std::string team_url(League league, uint32_t espn_id, const char *comp_slug);
std::string event_url(League league, const std::string &event_id, const char *comp_slug);
std::string team_day_url(League league, uint32_t group, int64_t day_epoch, const char *comp_slug);
std::string dark_logo(const std::string &url);
std::string team_logo_url(League league, uint32_t espn_id, const char *abbr);  // for a team with no game loaded yet

bool parse_team_str(const std::string &json, Schedule &out);
// Up to `max` games not yet started, in date order, from the schedule endpoint.
bool parse_upcoming_str(const std::string &json, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out);
bool parse_scoreboard_str(const std::string &json, const std::string &event_id, uint32_t our_team_id,
                          GameSnapshot &out);
// The team's next game that is not over yet in a day's scoreboard
// (team_day_url), into out.event_id, out.kickoff_epoch and out.time_tbd.
bool parse_team_game_str(const std::string &json, uint32_t our_team_id, Schedule &out,
                         const std::string &skip_event = "");
// The single-event document from event_url(). With event_id, a document for
// some other game reads as a failure.
bool parse_event_str(const std::string &json, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                     const std::string &event_id = "");

// neutral = nobody is "our" team (live-game modes): both sides splash with their abbreviation.
Splash decide_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral = false);
uint32_t parse_color(const std::string &hex);
std::string status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
// The board and ticker with no game on the board, in place of the page's
// "No upcoming game": "Getting ready" until the clock is set (nothing can
// be read from ESPN before it), "Can't reach ESPN" while reads fail and
// ESPN has never said this team has no game. Empty for the default, and in
// a live mode, which has its own words. captive (captive.h: the Wi-Fi answered
// with a sign-in page and ESPN has not answered since) wins over all of it,
// live modes included, since nothing can be read until it ends.
std::string no_game_text(bool clock_set, bool live_mode, bool no_game_confirmed, uint32_t misses,
                         bool captive = false);
// Bench only. Every ESPN URL starts with this root; a bench build points
// it at a host that does not exist to make ESPN unreachable without
// touching the network. Pass a string literal: only the pointer is kept,
// and the worker task reads it while the main loop may set it.
void set_site_root(const char *root);
// Home Assistant's Game Status: the matchup, the state and the score, and
// nothing that changes every play, so the recorder logs a line when the game
// starts, scores or ends rather than every poll.
std::string ha_status_text(const GameSnapshot &s, const std::string &kickoff_local);
// ESPN's game headline ("College Football Playoff Quarterfinal at the Rose
// Bowl Presented by Prudential") cut down to what people call the game
// ("Rose Bowl"). ESPN has no short form of it. Empty for a note that is a
// kickoff window, not a name.
std::string short_game_name(const std::string &headline);
// "Sat 3:30 PM", or "Sat TBD" with time_tbd, when ESPN has no kickoff time
// yet (then kick_local is from tbd_day_epoch).
std::string kickoff_label(const struct tm &kick_local, const struct tm &now_local, bool time_tbd = false);
// kickoff_label's day: "Today", "Tomorrow", "Sat" within the week, else
// "Nov 20"; "" for a time the panel couldn't convert.
std::string day_label(const struct tm &kick_local, const struct tm &now_local);
// ESPN lists a game with no kickoff time yet (timeValid false: college games
// a week or two out, TBD playoff slots) at midnight Eastern of its day. That
// instant is the evening before in Central and Pacific time, so anything that
// needs the game's day uses noon Eastern instead: the same calendar day from
// Hawaii to central Europe.
inline int64_t tbd_day_epoch(int64_t placeholder) { return placeholder + 12 * 3600; }
int64_t parse_iso8601_z(const std::string &s);
// An HTTP Date header, "Sat, 17 Oct 2026 23:39:29 GMT" (the only form a
// server may send), as a UTC epoch. 0 when it is anything else or outside
// 2020 to 2099.
int64_t parse_http_date(const std::string &s);
const char *state_name(GameState s);
// "0:42 2nd" from the raw clock and period. ESPN's pre-formatted shortDetail
// lags those fields by a poll or two, so it is only used for the states that
// have no clock (Halftime, End of 3rd, Delayed).
std::string clock_text(const GameSnapshot &s);

// At halftime: ESPN's status says so, or (in documents without a status
// name) shortDetail does before the third period. A football game whose plays
// have moved on (resumed) isn't, whatever the status says.
bool is_halftime(const GameSnapshot &s);

// ESPN's status name says the period is under way and the clock has time on
// it, so a shortDetail without a clock ("End of 3rd") is left from the break.
bool clock_running(const GameSnapshot &s);

// A football break the drive doesn't carry across: halftime, or the end of the
// 2nd or 4th quarter (which ESPN also sends as in progress, "End of 2nd" at
// 0:00), until the next play comes in. ESPN keeps the last drive's down,
// possession and red zone there, which no longer apply.
bool football_break(const GameSnapshot &s);

// The game clock a football play started at, from ESPN's play text
// ("(14:52) J.Smith kicks ..." is "14:52"); "" when it has none or it is 0:00.
std::string play_clock(const std::string &last_play);

// ESPN's status and its plays come from separate feeds and drift apart around
// breaks: the status can stay at STATUS_HALFTIME while the second half's
// plays come in, and a first-half play can arrive after the break began.
// Football only, given the previous snapshot of the same board or favorite:
// - held: a break began and no play has come in since (ESPN's later texts
//   during the break, and late first-half plays, move break_play along).
// - resumed: during STATUS_HALFTIME, a play that can only be the second
//   half's: its clock is later than any seen in the break (the clock counts
//   down, so a late first-half play's is earlier), or 10:00 or later when
//   none was; or a play without a clock that moved the drive.
// - Timeouts ESPN left out (-1): the last count from the same game and half
//   (each overtime its own); 3 before kickoff, and 3 when the 3rd quarter
//   begins, also over the first half's counts ESPN keeps sending until the
//   half's first play; otherwise unknown, which hides the pips. A count the
//   panel filled in is marked in to_guess and carried only within its period.
void carry_football(const GameSnapshot &prev, GameSnapshot &next);

// The team endpoint does not say which league answered, so a freshly parsed
// Schedule carries league 0 (NFL). Favorites and the live modes choose the
// follow-up scoreboard endpoint from Schedule::league, so it must be stamped
// from the team the schedule was fetched for.
inline void adopt_league(Schedule &s, League league) { s.league = (uint8_t) league; }
// A cup's team endpoint does not name the cup either.
inline void adopt_comp(Schedule &s, const char *comp_slug) { s.comp_slug = comp_slug; }

// Per-sport text and splashes. status_text, clock_text and decide_splash hand
// any snapshot that is not football to these (sport_baseball.cpp).
std::string baseball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string baseball_clock_text(const GameSnapshot &s);
Splash baseball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The count while a half inning is on ("2-1"), else "".
std::string baseball_count(const GameSnapshot &s);
// Hockey (sport_hockey.cpp).
std::string hockey_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string hockey_clock_text(const GameSnapshot &s);
Splash hockey_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);

// Soccer (sport_soccer.cpp).
std::string soccer_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string soccer_clock_text(const GameSnapshot &s);
Splash soccer_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The latest goal for the 128-wide situation row, at most `max` characters:
// "52' Saka", "55' Hany OG", "52' Saka (P)". "" before the first goal.
std::string soccer_situation(const GameSnapshot &s, size_t max = 12);
// A W-D-L record for the label under a logo (7 characters wide): as is when
// it fits, else the league points ("38 pts").
std::string soccer_board_record(const std::string &wdl);
// Plain ASCII for the panel fonts: accents dropped ("Guimarães" -> "Guimaraes").
std::string ascii_fold(const std::string &utf8);
// One club's team reads, the league's first and then each cup's: the index of
// the one to follow, -1 when none names a game. The earliest kickoff wins. A
// game that kicked off over four hours ago, or done_event (a game the panel
// saw finish), only counts when no read names anything else.
int pick_schedule(const std::vector<Schedule> &comps, int64_t now_epoch, const std::string &done_event);
// Basketball (sport_basketball.cpp). The JSON filter and extras parse are
// declared in espn_parse_impl.h, next to ArduinoJson.
std::string basketball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string basketball_clock_text(const GameSnapshot &s);
Splash basketball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The playoff series in at most 12 characters for the situation row
// ("NY lead 2-1", "Series 2-2"), else "".
std::string basketball_series(const GameSnapshot &s);

// Leaderboard sports: golf's tours (sport_golf.cpp) and racing's series
// (sport_racing.cpp). A tour is followed like a team: its board is the
// tour's current event from the scoreboard header, and the calendar names
// the next one. The JSON filters and parses are in espn_parse_impl.h.
//
// A league's scoreboard header, the sport and league from its path: golf's
// is ~19 KB during an event and ~3 KB between them.
// https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league=pga
// https://site.api.espn.com/apis/v2/scoreboard/header?sport=racing&league=f1
std::string header_url(League league);
// A golf tour's or a racing series' calendar: the scoreboard of the first
// Tuesday on or after now (Eastern), 6-12 KB since Tuesday never has a round
// or a race; or with next_season the year after this one's whole list
// (dates=2027, 40-58 KB for golf). Never this year: golf's dates=2026 is
// 29 MB, F1's 0.8 MB.
std::string golf_calendar_url(League league, int64_t now_epoch, bool next_season);
// The board's status cell, at most 12 characters: "Round 3", "R2 suspended",
// "After R2", "Final", "Upcoming", "Canceled".
std::string golf_round_text(const GameSnapshot &s);
// The same for the ticker and Home Assistant: "Round 2 suspended", "After round 2".
std::string golf_round_long(const GameSnapshot &s);
uint32_t golf_round_color(const GameSnapshot &s);
// Board columns: the place ("T4", "12", "-", at most 2 characters), the same
// without the width limit ("T10"), the score to par ("E", "-12", "+3", at most
// 3) and holes played ("F", "12", a tee time "8:27p", "-"; "" in a final; at
// most 6).
std::string golf_pos_text(const GolfRow &r);
std::string golf_place_text(const GolfRow &r);
std::string golf_score_text(const GolfRow &r);
uint32_t golf_score_color(const GolfRow &r);
std::string golf_thru_text(const GolfRow &r, GolfPhase phase);
// "8:27p", "11:40a", "12:05p".
std::string golf_tee_label(const struct tm &t);
// "Nov 12-15", "Oct 29-Nov 1", "Aug 30".
std::string golf_dates(const struct tm &start, const struct tm &end);
// An event name in at most max characters: the sponsor tail goes first, then
// a place or a cause ("Sony Open in Hawaii" is "Sony Open"), then a trailing
// "Championship" or "Classic" ("PGA Championship" is "PGA Champ"), then words
// from the end ("World Wide Technology Championship" is "World Wide" in 17).
std::string golf_event_name(const std::string &name, size_t max);
// A leaderboard cut to the board's columns (gd_board in
// pages/gameday-live.yaml): the title (128 wide only) and the status cell,
// then up to kGolfBoardRows rows of place, name, value and detail. No rows
// when the board shows its card instead.
struct BoardLines {
  std::string title, status;
  uint32_t status_color{0xFFFFFF};
  // One panel shows the value column (golf's score to par, 3 characters, the
  // name 7). False: no value column on one panel, and the names get its room
  // (racing, 10 characters).
  bool value_narrow{true};
  // value, detail: golf's score to par and holes played; racing's car number
  // and gap (128 wide only)
  std::vector<std::string> pos, name, value, detail;
  std::vector<int> value_color, name_color;
};
// Golf's leaderboard (none before the event or for a canceled one: the board
// shows golf_card_text).
BoardLines golf_lines(const GameSnapshot &s, bool wide);
// The no-leaderboard card on the no-game screen: the event name, a line
// break, then dates (before the event), Canceled or Postponed, or the round.
// No word is longer than a line (12 characters on one panel, 21 on two):
// "International" is "Int'l".
std::string golf_card_text(const GameSnapshot &s, bool wide, const std::string &dates);
// The ticker and Home Assistant's Game Status. dates: the event's own before
// it starts, the next event's after a final or a canceled one.
std::string golf_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &dates);
std::string golf_ha_status_text(const GameSnapshot &s, const std::string &dates);
// "SCHEFFLER WINS!" when the event went final between the two reads. Each
// word of the name fits a line of the splash (10 characters on one panel, 16
// on two), so LVGL never breaks one: "YELLAMARAJ WINS!" on one panel.
Splash golf_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool wide);
// The calendar's next event as a board: a PRE golf snapshot.
GameSnapshot golf_next_snapshot(const GolfNext &n);
// ESPN's CDN edges serve snapshots of different ages: a read that goes back to
// an earlier event (one that started two days or more before), an earlier
// round or an earlier phase of the one shown.
bool golf_read_is_older(const GameSnapshot &shown, const GameSnapshot &read);
// How long until the next header read.
uint32_t golf_poll_ms(const GameSnapshot &g, int64_t now);
// Between seasons: the board shows the off-season clock (offseason.h). Not
// while a failed calendar read leaves the next event unknown (next_unknown):
// the final stays up rather than the clock saying there is none.
bool golf_off_season(const GameSnapshot &g, int64_t now);
// A final (or a canceled event) over kSeasonHorizon old that ESPN still has in
// its header, with the next event known: the board shows that one instead.
bool golf_show_next(const GameSnapshot &g, int64_t now);

// Racing (sport_racing.cpp). A series (F1, the NASCAR Cup Series) is
// followed like a golf tour: its board is the current race weekend from the
// series' scoreboard header (header_url, up to ~95 KB during an F1 weekend,
// 3-27 KB for NASCAR), one entry per session, and its calendar
// (golf_calendar_url: the coming Tuesday's scoreboard, 6-10 KB) names the
// next race. The JSON filters and parses are in espn_parse_impl.h.
//
// A session's name for the board ("FP1", "Qual", "Sprint Q", "Sprint",
// "Race") and for the ticker ("Practice 1", "Qualifying", "Sprint
// qualifying", "Sprint", "Race").
std::string racing_session_label(RaceKind kind, uint8_t number);
std::string racing_session_long(RaceKind kind, uint8_t number);
// The board's status cell, at most 12 characters: "Lap 34/56", "FP3 live",
// "Red flag", "FP2 final", "SQ final" (the sprint shootout's label shortened
// to fit its word), "Final", "Canceled", "Upcoming".
std::string racing_state_text(const GameSnapshot &s);
// The same for the ticker, Home Assistant and the device page: "Race lap
// 34/56", "Practice 2 final", "Qualifying live".
std::string racing_state_long(const GameSnapshot &s);
uint32_t racing_state_color(const GameSnapshot &s);
// Board columns: the place ("1" to "99", else "-"), a name in max characters
// (a suffix, then a leading particle dropped: "van Gisbergen" is
// "Gisbergen", "Stenhouse Jr." is "Stenhouse"), the 128-wide detail in 6
// ("DNF", "DSQ", "+1L", "+13.07", "1:10.7") and the ticker's ("+1 lap",
// "+13.078").
std::string racing_pos_text(const RaceRow &r);
std::string racing_name_fit(const std::string &name, size_t max);
std::string racing_detail_text(const RaceRow &r);
std::string racing_detail_long(const RaceRow &r);
// A timed session's gap from two lap times ("1:10.763"), in whole
// milliseconds: "+0.253", "+1:02.004"; "" when either isn't a time or the gap
// is negative.
std::string racing_gap(const std::string &time, const std::string &leader);
// An event name in at most max characters: a NASCAR name without its "NASCAR
// Cup Series at" ("Miami", and "COTA" or "Wilkesboro" when the track's whole
// name won't fit), an F1 name as its place and "GP" without the sponsor ("Sao
// Paulo GP", "US GP"), then without the "GP", then whole words.
std::string racing_event_name(const std::string &name, size_t max);
// "Today 9:30a", "Tomorrow 12:00p", "Sat 9:30a", "Nov 20 7:30p".
std::string racing_when(const struct tm &start_local, const struct tm &now_local);
// The shown session's order cut to the board's columns: kGolfBoardRows rows,
// none before the weekend or for a canceled race (racing_card_text). One
// panel has no value column (value_narrow false) and 10 characters of name.
BoardLines racing_lines(const GameSnapshot &s, bool wide);
// The no-order card: the event name, a line break, then the next session and
// when ("FP1 Fri 10:30a"), the calendar's dates ("Nov 19-21"), Canceled or
// Postponed, or the status. No word is longer than a line (12 characters on
// one panel, 21 on two).
std::string racing_card_text(const GameSnapshot &s, bool wide, const std::string &when);
// The ticker and Home Assistant's Game Status (only a new leader or session
// changes the latter, never a lap). when: the next session's day and time,
// or the event's dates (before it), or the next event's (after the race).
std::string racing_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &when);
std::string racing_ha_status_text(const GameSnapshot &s, const std::string &when);
// "ANTONELLI WINS!" when the race went final between two reads of it (never
// a practice, qualifying or a sprint). Each word fits a line of the splash
// (10 characters on one panel, 16 on two).
Splash racing_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool wide);
// The calendar's next race weekend as a PRE board, its dates only.
GameSnapshot racing_next_snapshot(const GolfNext &n);
// An older CDN copy: an event that started two days or more before the one
// shown, or the same event at an earlier session, phase or lap.
bool racing_read_is_older(const GameSnapshot &shown, const GameSnapshot &read);
// How long until the next header read: 30 s while a race or sprint runs, a
// minute in practice or qualifying, 2 minutes while a session is suspended or
// delayed (15 six hours past its start), closer to the next session as it
// nears, and 6 hours while the off-season clock is up.
uint32_t racing_poll_ms(const GameSnapshot &g, int64_t now);
// A session on track, for holding back an automatic update: LIVE, a red flag
// or a short stop included, but not one stopped six hours past its start.
bool racing_on_track(const GameSnapshot &g, int64_t now);
// The off-season clock: the next race over kRaceSeasonGap away, or none
// known a day after the last race. Not while a failed calendar read leaves
// it unknown (next_unknown).
bool racing_off_season(const GameSnapshot &g, int64_t now);
// A final (or a canceled race) over a day past its start, the next race
// known: the board shows that one instead.
bool racing_show_next(const GameSnapshot &g, int64_t now);

// The leaderboard sports' one path through the component (gameday.cpp's
// board job): each picks golf's or racing's function by the snapshot's sport.
// The calendar's next event and the flag that its read failed:
GolfNext &board_next(GameSnapshot &g);
bool &board_next_unknown(GameSnapshot &g);
// The next event as a PRE board of the sport.
GameSnapshot board_next_snapshot(Sport sport, const GolfNext &n);
bool board_show_next(const GameSnapshot &g, int64_t now);
bool board_read_is_older(const GameSnapshot &shown, const GameSnapshot &read);
Splash board_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool wide);
uint32_t board_poll_ms(const GameSnapshot &g, int64_t now);
bool board_off_season(const GameSnapshot &g, int64_t now);
BoardLines board_lines(const GameSnapshot &g, bool wide);
// when: golf's dates (before the event); racing's next session or dates.
std::string board_card_text(const GameSnapshot &g, bool wide, const std::string &when);
std::string board_event_name(Sport sport, const std::string &name, size_t max);

namespace detail {
// Ticker helpers shared by the sport files.
void add_part(std::string &out, const std::string &part);
// Text helpers shared by the leaderboard sports (sport_golf.cpp): s cut to
// max characters; lower case; the trailing spaces, commas, hyphens and
// colons dropped; the last word.
std::string cut(const std::string &s, size_t max);
std::string lower(std::string s);
void trim_right(std::string &s);
std::string last_word(const std::string &s);
// Whole words up to max, without a dangling "of", "in" or "at the", or a
// bracket left open; one long word is cut.
std::string whole_words(const std::string &s, size_t max);
// Each word (split at spaces and after hyphens, where LVGL breaks a line) in
// at most line characters, so a wrapping label never breaks one in the
// middle: "International" is "Int'l", a longer word is cut.
std::string fit_words(const std::string &s, size_t line);
std::string records_line(const GameSnapshot &s);
// The pre-game betting parts when the ticker shows odds: the line, the
// over/under and the TV listing. Soccer builds its own line.
void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o);
void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o, const std::string &odds);
// The last play, cut to 160 characters so one long description doesn't fill
// the ticker.
void add_last_play(std::string &out, std::string play);
// Splash rules shared by every sport. Two reads of one game: a splash is
// never decided across a boot, a team change or a new game.
bool same_game(const GameSnapshot &prev, const GameSnapshot &cur);
// The game went from in progress to over between the two reads.
inline bool went_final(const GameSnapshot &prev, const GameSnapshot &cur) {
  return prev.state == GameState::IN && cur.state == GameState::POST;
}
// What one side scored between the two reads, 0 when its score held or went
// down (a correction). Scores are atoi of ESPN's text, so any int can arrive:
// the difference is taken in 64 bits, and a gain past what an int holds
// counts as the largest int.
inline int score_gain(int before, int after) {
  const int64_t d = (int64_t) after - (int64_t) before;
  if (d <= 0)
    return 0;
  return d > INT_MAX ? INT_MAX : (int) d;
}
// The victory splash once a game is over: our side's "DAL WINS!", or the
// other side's when neutral (live modes). us and them are what decides it,
// the score or a soccer shootout's. Nothing for a draw, or for a game ESPN
// moved to post without finishing it (suspended or canceled).
Splash win_splash(const GameSnapshot &cur, bool neutral, int us, int them);
}  // namespace detail

// Does this cycle need to re-read the team endpoint, or only re-poll the game
// it already knows about? `force` is the Refresh Now button. It has to be its
// own flag: clearing the fetch timestamp does not force anything, because a
// still valid schedule with a zero timestamp reads as "age = millis()", which
// is under the interval for the first six hours after a restart. Unsigned
// subtraction keeps the age right across the millis() wrap.
inline bool schedule_due(bool have_schedule, bool force, uint32_t now, uint32_t fetched_ms, uint32_t interval) {
  return force || !have_schedule || (now - fetched_ms) >= interval;
}

// ESPN's team endpoint names a team's next game as soon as the current one
// starts (MIL and LAD on 2026-10-12), so a read during a game skips it.
// `today` is the team's first unfinished game on the day's scoreboard
// (parse_team_game). True when it comes before the team endpoint's game, or
// the team endpoint named none: it is the game on now.
inline bool today_game_first(const Schedule &team, const Schedule &today) {
  if (today.event_id.empty() || today.event_id == team.event_id)
    return false;
  return team.event_id.empty() || today.kickoff_epoch < team.kickoff_epoch;
}

// How long a live mode waits after a scan that found nothing in progress:
// until the day's next kickoff, but at least min_ms (the game is due and
// not under way yet) and at most max_ms (kickoffs move, and the day the scan
// reads rolls over). With no kickoff left (next_kickoff 0) it waits max_ms,
// and with no clock min_ms.
inline uint32_t no_live_rescan_ms(int64_t now_epoch, int64_t next_kickoff, uint32_t min_ms, uint32_t max_ms) {
  if (now_epoch < 1000000000)
    return min_ms;
  if (next_kickoff == 0)
    return max_ms;
  int64_t ms = (next_kickoff - now_epoch) * 1000;
  if (ms < (int64_t) min_ms)
    return min_ms;
  if (ms > (int64_t) max_ms)
    return max_ms;
  return (uint32_t) ms;
}

}  // namespace espn

// Streaming entry points live in the header because they are templates on
// the reader type (a std::string on the host, an HTTP container on device).
#include "espn_parse_impl.h"
