// Favorite players (components/gameday/players.h): what a game read keeps
// for them, their stat lines, who scored and the splash that names them, from
// ESPN's own documents in fixtures/ with the play or the score moved on. The
// player= and playerdel= keys themselves are in test_set.cpp and
// fixtures/set_cases.json.
#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "board_layout.h"
#include "panel_text.h"
#include "players.h"
#include "splash_layout.h"
#include "watch.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define PCHECK_EQ(a, b)                                                                       \
  do {                                                                                        \
    checks++;                                                                                 \
    const std::string va = (a), vb = (b);                                                     \
    if (va != vb) {                                                                           \
      failures++;                                                                             \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                         \
  } while (0)

#define PCHECK(cond)                                           \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                          \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

constexpr uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA, MLB = (uint8_t) League::MLB,
                  NBA = (uint8_t) League::NBA, NHL = (uint8_t) League::NHL, EPL = (uint8_t) League::EPL;

// A list of favorites, the rest of the slots empty.
struct List {
  FavPlayer p[kPlayerSlots]{};
  List(std::initializer_list<FavPlayer> players) {
    for (const FavPlayer &f : players)
      player_put(this->p, f);
  }
};

// A game read the way the panel reads it with these favorites.
GameSnapshot read_json(const std::string &json, Sport sport, uint32_t team, const List &list, uint8_t league) {
  GameSnapshot s;
  const PlayerWatch w = player_watch(list.p, league);
  PCHECK(parse_event_str(json, sport, team, s, "", &w));
  return s;
}

GameSnapshot read(const char *path, Sport sport, uint32_t team, const List &list, uint8_t league) {
  return read_json(slurp(path), sport, team, list, league);
}

// One event of a scoreboard fixture, as the panel's own read of that game
// gets it (the single-event document is the event).
std::string event_of(const char *path, size_t i) {
  JsonDocument doc;
  PCHECK(!deserializeJson(doc, slurp(path), DeserializationOption::NestingLimit(40)));
  std::string out;
  serializeJson(doc["events"][i], out);
  return out;
}

std::string splash(const GameSnapshot &prev, const GameSnapshot &cur, uint8_t league, const List &list,
                   bool wide = false, uint32_t alerts = kAlertsAll, bool theirs = true, bool neutral = false) {
  return board_splash_with_players(prev, cur, theirs, neutral, alerts, wide, league, list.p).text;
}

std::string joined(const std::vector<std::string> &v) {
  std::string out;
  for (const auto &s : v)
    out += (out.empty() ? "" : " | ") + s;
  return out;
}

// ---- the list and its blob -----------------------------------------------------

void test_list() {
  FavPlayer list[kPlayerSlots]{};
  const FavPlayer allen = make_player(NFL, 3918298, 2, "ALLEN");
  PCHECK(player_valid(allen));
  PCHECK(player_put(list, allen) == 1);
  PCHECK(player_put(list, make_player(NFL, 15847, 12, "KELCE")) == 2);
  // The same player again keeps their slot and takes the new name and team.
  PCHECK(player_put(list, make_player(NFL, 3918298, 12, "J. ALLEN")) == 1);
  PCHECK_EQ(list[0].name, "J. ALLEN");
  PCHECK(list[0].team_id == 12);
  // The same id in another league is someone else.
  PCHECK(player_put(list, make_player(NBA, 3918298, 16, "ALLEN")) == 3);
  PCHECK(player_drop(list, NFL, 15847) && player_empty(list[1]));
  PCHECK(!player_drop(list, NFL, 15847));
  PCHECK(player_put(list, make_player(NHL, 3114727, 27, "O'NEIL")) == 2);  // the freed slot
  for (uint32_t id = 10; player_put(list, make_player(NFL, id, 2, "X")) != 0; id++) {
  }
  PCHECK(player_put(list, make_player(NFL, 99, 2, "NINTH")) == 0);
  PCHECK(player_put(list, make_player(NFL, 3918298, 2, "ALLEN")) == 1);  // a player in a full list
  // The watch for a game read: the league's favorites only.
  PlayerWatch w = player_watch(list, NBA);
  PCHECK(w.n == 1 && w.ids[0] == 3918298 && w.has(3918298) && !w.has(15847) && !w.has(0));
  PCHECK(player_watch(list, MLB).n == 0);
  PCHECK(player_watch(list, NFL).n == 6);

  // What flash can hand back that isn't a player: emptied at boot.
  FavPlayer bad = allen;
  bad.team_id = 99999;
  PCHECK(!player_valid(bad));
  bad = allen;
  bad.league = (uint8_t) League::PGA;
  PCHECK(!player_valid(bad));
  bad = allen;
  bad.league = 200;
  PCHECK(!player_valid(bad));
  bad = allen;
  memset(bad.name, 'A', sizeof(bad.name));  // no end
  PCHECK(!player_valid(bad));
  bad = allen;
  strcpy(bad.name, "allen");
  PCHECK(!player_valid(bad));
  bad = allen;
  bad.name[0] = 0;
  PCHECK(!player_valid(bad));
  bad = allen;
  bad.athlete = 0;
  PCHECK(!player_valid(bad) && player_empty(bad));
  PCHECK(sizeof(PlayerBlob) == 160);
}

void test_names() {
  PCHECK_EQ(clean_player_name("Allen"), "ALLEN");
  PCHECK_EQ(clean_player_name("  St. Brown "), "ST. BROWN");
  PCHECK_EQ(clean_player_name("O'Neil"), "O'NEIL");
  PCHECK_EQ(clean_player_name("Valdes-Scantling"), "VALDES-SCA");
  PCHECK_EQ(clean_player_name("ABCDEFGHI-JK"), "ABCDEFGHI");
  PCHECK_EQ(clean_player_name("ABCDEFGH. JK"), "ABCDEFGH");
  PCHECK_EQ(clean_player_name("MAHOMES II"), "MAHOMES II");
  PCHECK_EQ(clean_player_name("Allén"), "");
  PCHECK_EQ(clean_player_name("A,B"), "");
  PCHECK_EQ(clean_player_name("A\nB"), "");
  PCHECK_EQ(clean_player_name("   "), "");
  PCHECK_EQ(clean_player_name(""), "");
  // A name needs a letter or a digit, and a run of spaces inside is one.
  PCHECK_EQ(clean_player_name("-"), "");
  PCHECK_EQ(clean_player_name("..."), "");
  PCHECK_EQ(clean_player_name("' '"), "");
  PCHECK_EQ(clean_player_name("----------AB"), "");  // nothing but hyphens before the cut
  PCHECK_EQ(clean_player_name("-1-"), "-1-");
  PCHECK_EQ(clean_player_name("A     B"), "A B");
  PCHECK_EQ(clean_player_name("J.    van Riemsdyk"), "J. VAN RIE");
  PCHECK_EQ(clean_player_name("Nugent-Hopkins"), "NUGENT-HOP");
  FavPlayer odd = make_player(NFL, 3918298, 2, "ALLEN");
  strcpy(odd.name, "-");
  PCHECK(!player_valid(odd));  // a blob from before the rule is emptied
  strcpy(odd.name, "A  B");
  PCHECK(!player_valid(odd));
  SetOp op;
  PCHECK(parse_set("player", "nfl,3918298,2,ALLEN", op, 16) && op.kind == SetKind::PLAYER && op.athlete == 3918298 &&
         op.team.id == 2 && op.team.league == NFL && op.name == "ALLEN");
  PCHECK(parse_set("playerdel", "nba,4594268", op, 16) && op.kind == SetKind::PLAYER_DEL && op.athlete == 4594268 &&
         op.team.league == NBA);
  PCHECK(!parse_set("player", "nfl,3918298,2,ALLEN,", op, 16));
  PCHECK(!parse_set("player", "nfl,3918298,,ALLEN", op, 16));
  PCHECK(!parse_set("player", ",3918298,2,ALLEN", op, 16));
  PCHECK(!parse_set("playerdel", "nfl,3918298,", op, 16));
  PCHECK(!parse_set("playerdel", "", op, 16));
  PCHECK(!parse_set("playerdel", "lpga,1", op, 16));
}

// ---- what a game read keeps --------------------------------------------------------

constexpr uint32_t kKohl = 4870883, kAnderson = 4879999, kResar = 5079684, kFIU = 2229, kJXST = 55;

void test_read() {
  const char *ncaa = "fixtures/event_ncaa_live.json";
  GameSnapshot plain;
  PCHECK(parse_event_str(slurp(ncaa), Sport::FOOTBALL, kFIU, plain));
  // Without favorites in the league the read keeps nothing more.
  PCHECK(plain.leaders.empty() && plain.play_athletes.empty());
  const List others{make_player(NBA, kKohl, 16, "KOHL")};
  GameSnapshot elsewhere = read(ncaa, Sport::FOOTBALL, kFIU, others, NCAA);
  PCHECK(elsewhere.leaders.empty() && elsewhere.play_athletes.empty());
  PCHECK(elsewhere.last_play == plain.last_play && elsewhere.play_id == plain.play_id &&
         elsewhere.team_score == plain.team_score && elsewhere.down_distance == plain.down_distance);

  const List fiu{make_player(NCAA, kKohl, kFIU, "KOHL"), make_player(NCAA, kAnderson, kFIU, "ANDERSON")};
  GameSnapshot g = read(ncaa, Sport::FOOTBALL, kFIU, fiu, NCAA);
  PCHECK(g.leaders.size() == 2 && g.leaders[0].athlete == kKohl && g.leaders[1].athlete == kAnderson);
  PCHECK_EQ(g.leaders[0].value, "21/34, 241 YDS, 2 TD");
  PCHECK(g.leaders[0].cat.empty());  // the category is basketball's only
  PCHECK(g.play_athletes.size() == 1 && g.play_athletes[0].id == kResar && g.play_athletes[0].team == kFIU);
  PCHECK(g.play_points == 0 && g.play_id == "401871053765");
  // The rest of the read is the same.
  PCHECK(g.last_play == plain.last_play && g.team_score == plain.team_score && g.opp_score == plain.opp_score &&
         g.possession == plain.possession && g.play_side == plain.play_side);

  // Soccer: each goal's scorer, only with a favorite in the league.
  GameSnapshot epl;
  PCHECK(parse_event_str(slurp("fixtures/event_epl_final.json"), Sport::SOCCER, 359, epl));
  PCHECK(epl.soc.goals.size() == 2 && epl.soc.goals[0].athlete == 0);
  const List saka{make_player(EPL, 280555, 359, "SAKA")};
  epl = read("fixtures/event_epl_final.json", Sport::SOCCER, 359, saka, EPL);
  PCHECK(epl.soc.goals.size() == 2 && epl.soc.goals[0].athlete == 218522 && epl.soc.goals[1].athlete == 280555);
  PCHECK(epl.leaders.empty() && epl.play_athletes.empty());

  // Hockey: nothing more, ESPN names nobody.
  const List wpg{make_player(NHL, 3899937, 28, "CONNOR")};
  GameSnapshot nhl = read("fixtures/event_nhl_live.json", Sport::HOCKEY, 28, wpg, NHL);
  PCHECK(nhl.leaders.empty() && nhl.play_athletes.empty());
}

// ---- the stat line ------------------------------------------------------------------

void test_stat_lines() {
  PCHECK_EQ(stat_trim("27/43, 295 YDS, 2 TD, 2 INT", 28), "27/43, 295 YDS, 2 TD, 2 INT");
  PCHECK_EQ(stat_trim("27/43, 295 YDS, 2 TD, 2 INT", 20), "27/43, 295 YDS, 2 TD");
  PCHECK_EQ(stat_trim("27/43, 295 YDS, 2 TD, 2 INT", 21), "27/43, 295 YDS, 2 TD");
  PCHECK_EQ(stat_trim("27/43, 295 YDS, 2 TD, 2 INT", 10), "27/43");
  PCHECK_EQ(stat_trim("ABCDEFGHIJKL", 5), "ABCDE");
  PCHECK_EQ(stat_trim("ABCD, EFGH", 5), "ABCD");

  // College football: the game's leaders, passing, rushing and receiving.
  const List fiu{make_player(NCAA, kKohl, kFIU, "KOHL"), make_player(NCAA, kResar, kFIU, "RESAR"),
                 make_player(NCAA, kAnderson, kFIU, "ANDERSON"), make_player(NCAA, 5122157, kJXST, "CREEL")};
  GameSnapshot g = read("fixtures/event_ncaa_live.json", Sport::FOOTBALL, kFIU, fiu, NCAA);
  // Resar leads nothing: no line for him.
  PCHECK_EQ(joined(player_lines(g, NCAA, fiu.p)),
            "KOHL 21/34, 241 YDS, 2 TD | ANDERSON 7 REC, 109 YDS, 2 TD | CREEL 19 CAR, 95 YDS");
  PCHECK(player_lines(g, NFL, fiu.p).empty());  // the board's game in another league
  GameSnapshot pre = g;
  pre.state = GameState::PRE;
  PCHECK(player_lines(pre, NCAA, fiu.p).empty());
  GameSnapshot post = g;
  post.state = GameState::POST;
  PCHECK(player_lines(post, NCAA, fiu.p).empty());

  // The NFL's longest line fits whole.
  const List ne{make_player(NFL, 4431452, 17, "MAYE")};
  GameSnapshot sb = read("fixtures/event_nfl_super_bowl.json", Sport::FOOTBALL, 17, ne, NFL);
  sb.state = GameState::IN;
  PCHECK_EQ(joined(player_lines(sb, NFL, ne.p)), "MAYE 27/43, 295 YDS, 2 TD, 2 INT");

  // Basketball: each team's leaders. ESPN's summary line when he leads it,
  // else the categories he leads, with their units.
  const List nba{make_player(NBA, 4396909, 11, "NESMITH"), make_player(NBA, 3032976, 16, "GOBERT"),
                 make_player(NBA, 4432816, 16, "BALL"), make_player(NBA, 5106060, 11, "WALKER"),
                 make_player(NBA, 4431671, 16, "MCDANIELS")};
  GameSnapshot b = read("fixtures/event_nba_live.json", Sport::BASKETBALL, 11, nba, NBA);
  PCHECK(b.leaders.size() == 6 && b.leaders[0].cat == "points");  // McDaniels leads nothing
  PCHECK_EQ(joined(player_lines(b, NBA, nba.p)), "NESMITH 19 PTS | GOBERT 9 REB | BALL 14 PTS, 7 AST | WALKER 7 AST");
  GameSnapshot only = b;
  only.leaders.erase(std::remove_if(only.leaders.begin(), only.leaders.end(),
                                    [](const Leader &l) { return l.cat == "rating"; }),
                     only.leaders.end());
  PCHECK_EQ(player_stat(only, 4432816), "7 AST");
  PCHECK_EQ(player_stat(only, 4396909), "19 PTS");
  Leader both{3032976, "points", "21"};
  only.leaders.push_back(both);
  PCHECK_EQ(player_stat(only, 3032976), "21 PTS, 9 REB");

  // Baseball: the batting line, from whichever category names him.
  const List tb{make_player(MLB, 4917849, 30, "MESA JR."), make_player(MLB, 33912, 10, "BELLINGER")};
  GameSnapshot m = read("fixtures/event_mlb_hr.json", Sport::BASEBALL, 10, tb, MLB);
  PCHECK(m.leaders.size() == 2);  // one line each, though ESPN lists Mesa Jr. in four categories
  PCHECK_EQ(joined(player_lines(m, MLB, tb.p)), "MESA JR. 1-3, HR, 2 RBI, R, K | BELLINGER 0-2");

  // Hockey and soccer have no leaders.
  const List wpg{make_player(NHL, 3899937, 28, "CONNOR")};
  PCHECK(player_lines(read("fixtures/event_nhl_live.json", Sport::HOCKEY, 28, wpg, NHL), NHL, wpg.p).empty());
}

// ---- scoring -------------------------------------------------------------------------

void test_football() {
  const List fiu{make_player(NCAA, kResar, kFIU, "RESAR")};
  // JXST 27, FIU 24, FIU's ball: Resar's 6-yard catch. The next read: his
  // touchdown.
  GameSnapshot prev = read("fixtures/event_ncaa_live.json", Sport::FOOTBALL, kFIU, fiu, NCAA);
  GameSnapshot td = prev;
  td.play_id = "401871053766";
  td.play_points = 6;
  td.team_score += 6;
  td.last_play = "J.Kohl pass complete to J.Resar for 31 yards, TOUCHDOWN";
  PCHECK_EQ(splash(prev, td, NCAA, fiu), "RESAR\nTOUCHDOWN");
  PCHECK_EQ(splash(prev, td, NCAA, fiu, true), "RESAR TOUCHDOWN");  // 16 characters a line
  PCHECK(board_splash_with_players(prev, td, true, false, kAlertsAll, false, NCAA, fiu.p).color == 0x091f3f);
  // The Favorite player switch off: the team's splash.
  PCHECK_EQ(splash(prev, td, NCAA, fiu, false, kAlertsAll & ~alert_bit(Alert::PLAYER)), "TOUCHDOWN!");
  // Favorite teams only left no alerts for this game: nothing.
  PCHECK_EQ(splash(prev, td, NCAA, fiu, false, 0), "");
  // The team's own switches don't matter to his.
  PCHECK_EQ(splash(prev, td, NCAA, fiu, false, alert_bit(Alert::PLAYER)), "RESAR\nTOUCHDOWN");
  // Someone else's touchdown, no favorite at all, or a list for another league.
  PCHECK_EQ(splash(prev, td, NCAA, List{make_player(NCAA, kKohl, kFIU, "KOHL")}), "TOUCHDOWN!");
  PCHECK_EQ(splash(prev, td, NCAA, List{}), "TOUCHDOWN!");
  PCHECK_EQ(splash(prev, td, NFL, fiu), "TOUCHDOWN!");

  // His points come from the play: a field goal.
  GameSnapshot fg = prev;
  fg.play_id = "401871053770";
  fg.play_points = 3;
  fg.team_score += 3;
  PCHECK_EQ(splash(prev, fg, NCAA, fiu), "RESAR\nFIELD GOAL");
  // A try is never his: its athletes are the touchdown's. A point read on
  // its own after his touchdown is the team's extra point, a 2-point
  // conversion the team's too.
  GameSnapshot xp = td;
  xp.play_id = "401871053767";
  xp.play_points = 1;
  xp.team_score += 1;
  PCHECK_EQ(splash(td, xp, NCAA, fiu), "EXTRA POINT!");
  GameSnapshot two = td;
  two.play_id = "401871053771";
  two.play_points = 2;
  two.team_score += 2;
  two.last_play = "Two-point conversion attempt, J.Resar pass from J.Kohl, ATTEMPT SUCCEEDS";
  PCHECK_EQ(splash(td, two, NCAA, fiu), "2-PT!");
  two.play_type = "Two Point Pass";
  PCHECK_EQ(splash(td, two, NCAA, fiu), "2-PT!");
  // The NFL writes the try into the touchdown's play, its type the
  // touchdown's: his touchdown, whatever points ESPN gives the play.
  GameSnapshot nfl7 = prev;
  nfl7.play_id = "401871053780";
  nfl7.play_points = 7;
  nfl7.team_score += 7;
  nfl7.play_type = "Passing Touchdown";
  nfl7.last_play = "J.Kohl pass deep left to J.Resar for 23 yards, TOUCHDOWN. T.Bass extra point is GOOD, Center-R.Ferguson.";
  PCHECK_EQ(splash(prev, nfl7, NCAA, fiu), "RESAR\nTOUCHDOWN");
  GameSnapshot nfl8 = nfl7;
  nfl8.play_points = 8;
  nfl8.team_score += 1;
  nfl8.last_play = "J.Resar up the middle for 30 yards, TOUCHDOWN. TWO-POINT CONVERSION ATTEMPT. J.Resar rushes. ATTEMPT SUCCEEDS.";
  PCHECK_EQ(splash(prev, nfl8, NCAA, fiu), "RESAR\nTOUCHDOWN");

  // A play that scored nothing (the kickoff after the score), the same play
  // read again, or points in a read whose play is no new one: not his.
  GameSnapshot kickoff = td;
  kickoff.play_points = 0;
  PCHECK_EQ(splash(prev, kickoff, NCAA, fiu), "TOUCHDOWN!");
  GameSnapshot same = td;
  same.play_id = prev.play_id;
  PCHECK_EQ(splash(prev, same, NCAA, fiu), "TOUCHDOWN!");
  // The other side scored: his name is in the play, but his side didn't score.
  GameSnapshot theirs = td;
  theirs.team_score = prev.team_score;
  theirs.opp_score += 6;
  PCHECK_EQ(splash(prev, theirs, NCAA, fiu), "JXST TOUCHDOWN");
  // ESPN lists him on the side that didn't score (a safety, tackled in his
  // own end zone): not his.
  GameSnapshot safety = prev;
  safety.play_id = "401871053772";
  safety.play_points = 2;
  safety.opp_score += 2;
  safety.last_play = "J.Resar run for a loss of 3 yards, tackled in the end zone, SAFETY";
  PCHECK_EQ(splash(prev, safety, NCAA, fiu), "JXST SAFETY");
  // The other side's favorite who scored is named, whether or not the
  // other side's plays splash for the team.
  const List jx{make_player(NCAA, 5122157, kJXST, "CREEL")};
  GameSnapshot run = prev;
  run.play_id = "401871053773";
  run.play_points = 6;
  run.opp_score += 6;
  run.play_athletes = {PlayAthlete{5122157, kJXST}};
  PCHECK_EQ(splash(prev, run, NCAA, jx, false, kAlertsAll, false), "CREEL\nTOUCHDOWN");
  // Before the game is on, or across a new game: nothing.
  GameSnapshot pre = prev;
  pre.state = GameState::PRE;
  PCHECK_EQ(splash(pre, td, NCAA, fiu), "");
  GameSnapshot other = td;
  other.event_id = "1";
  PCHECK_EQ(splash(prev, other, NCAA, fiu), "");
}

// ESPN's own college documents: the extra point is a play of its own, with an
// id below zero, that lists the touchdown's scorer (TE Lacota Dippre, RB
// Jamarion Morrow), never the kicker.
void test_college_try() {
  const char *day = "fixtures/day_ncaa_20261017.json";
  struct Case {
    size_t event;
    uint32_t team, scorer;
    const char *name;
  };
  for (const Case &c : {Case{0, 57, 5121998, "DIPPRE"}, Case{2, 245, 5141561, "MORROW"}}) {
    const List fav{make_player(NCAA, c.scorer, c.team, c.name)};
    GameSnapshot xp = read_json(event_of(day, c.event), Sport::FOOTBALL, c.team, fav, NCAA);
    PCHECK(xp.state == GameState::IN && xp.play_points == 1 && xp.play_id[0] == '-');
    PCHECK(xp.play_athletes.size() == 1 && xp.play_athletes[0].id == c.scorer && xp.play_athletes[0].team == c.team);
    // The read before: his touchdown. The one before that: the drive.
    GameSnapshot td = xp;
    td.play_id = "401856724500";
    td.play_points = 6;
    td.team_score -= 1;
    td.play_type = "Passing Touchdown";
    td.last_play = "pass complete for 12 yards, TOUCHDOWN";
    GameSnapshot drive = td;
    drive.play_id = "401856724480";
    drive.play_points = 0;
    drive.team_score -= 6;
    drive.play_type = "Pass Reception";
    drive.play_athletes.clear();
    drive.last_play = "pass complete for 9 yards";
    const std::string name = c.name;
    PCHECK_EQ(splash(drive, td, NCAA, fav), name + "\nTOUCHDOWN");
    // The extra point after it: the team's, not a second splash with his name.
    PCHECK_EQ(splash(td, xp, NCAA, fav), "EXTRA POINT!");
    // The touchdown and the extra point in one read: his touchdown, where
    // the team's splash would be the touchdown too.
    PCHECK_EQ(splash(drive, xp, NCAA, fav), name + "\nTOUCHDOWN");
    PCHECK_EQ(splash(drive, xp, NCAA, fav, true), name + " TOUCHDOWN");
    PCHECK_EQ(splash(drive, xp, NCAA, List{}), "TOUCHDOWN!");
    // ... and with the try missed.
    GameSnapshot missed = xp;
    missed.play_points = 0;
    missed.team_score -= 1;
    missed.play_type = "Extra Point Missed";
    PCHECK_EQ(splash(drive, missed, NCAA, fav), name + "\nTOUCHDOWN");
    // A favorite kicker isn't named either: ESPN doesn't list him.
    PCHECK_EQ(splash(td, xp, NCAA, List{make_player(NCAA, 1, c.team, "KICKER")}), "EXTRA POINT!");
    // A field goal and the try in one read is no touchdown of his.
    GameSnapshot fg_try = drive;
    fg_try.team_score = xp.team_score - 3;
    PCHECK_EQ(splash(fg_try, xp, NCAA, fav), "FIELD GOAL!");
  }
}

void test_baseball() {
  // NYY 2, TB 2: Mesa Jr.'s two-run home run for TB.
  const List tb{make_player(MLB, 4917849, 30, "MESA JR.")};
  GameSnapshot before = read("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, 10, tb, MLB);
  GameSnapshot hr = read("fixtures/event_mlb_hr.json", Sport::BASEBALL, 10, tb, MLB);
  PCHECK(hr.play_points == 2 && hr.play_athletes.size() == 1 && hr.play_athletes[0].team == 30);
  PCHECK_EQ(splash(before, hr, MLB, tb), "MESA JR.\nHOME RUN");
  PCHECK_EQ(splash(before, hr, MLB, tb, true), "MESA JR.\nHOME RUN");  // 17 characters
  PCHECK(board_splash_with_players(before, hr, true, false, kAlertsAll, false, MLB, tb.p).color == 0x092c5c);
  // The Yankees' panel that doesn't show the other side's plays still names him.
  PCHECK_EQ(splash(before, hr, MLB, tb, false, kAlertsAll, false), "MESA JR.\nHOME RUN");
  PCHECK_EQ(splash(before, hr, MLB, tb, false, kAlertsAll & ~alert_bit(Alert::PLAYER), false), "");
  // A Yankee favorite: the team's splash.
  PCHECK_EQ(splash(before, hr, MLB, List{make_player(MLB, 33912, 10, "BELLINGER")}), "TB HOME RUN");
  // Reads made before he was a favorite keep no athletes: nothing to credit.
  GameSnapshot plain_before, plain_hr;
  PCHECK(parse_event_str(slurp("fixtures/event_mlb_hr_before.json"), Sport::BASEBALL, 10, plain_before));
  PCHECK(parse_event_str(slurp("fixtures/event_mlb_hr.json"), Sport::BASEBALL, 10, plain_hr));
  PCHECK_EQ(splash(plain_before, plain_hr, MLB, tb), "TB HOME RUN");
  // A run that isn't a home run: the runs he drove in.
  GameSnapshot single = hr;
  single.play_type = "Single";
  single.last_play = "Mesa Jr. singled to center, Caminero scored, Walls scored.";
  PCHECK_EQ(splash(before, single, MLB, tb), "MESA JR.\n2 RBI");
  single.play_points = 1;
  single.opp_score = before.opp_score + 1;
  PCHECK_EQ(splash(before, single, MLB, tb), "MESA JR.\nRBI");
  // A grand slam.
  GameSnapshot slam = hr;
  slam.play_points = 4;
  slam.opp_score = before.opp_score + 4;
  PCHECK_EQ(splash(before, slam, MLB, tb), "MESA JR.\nGRAND SLAM");
}

// Another game's big play on the board (watch.h). The reads of other games
// keep no players, so the play that brought the board there is the team's,
// and it splashes once; a favorite's score in a later poll of that game names
// them, as on the board's own game. Back on the board's game, nothing that
// happened while it was away is splashed, a favorite's score included.
void test_watcher() {
  const List tb{make_player(MLB, 4917849, 30, "MESA JR.")};
  GameSnapshot before = read("fixtures/event_mlb_hr_before.json", Sport::BASEBALL, 10, tb, MLB);
  GameSnapshot hr = read("fixtures/event_mlb_hr.json", Sport::BASEBALL, 10, tb, MLB);
  const Splash brought{"TB HOME RUN", 0x092c5c};
  // The first poll's read credits him, but its splash is the one that brought the board.
  PCHECK_EQ(visit_splash(true, brought, GameSnapshot{}, hr, kAlertsAll, false, MLB, tb.p).text, "TB HOME RUN");
  // The next poll reads the same play: nothing again.
  PCHECK_EQ(visit_splash(false, brought, hr, hr, kAlertsAll, false, MLB, tb.p).text, "");
  // His home run between two later polls names him, either side counting.
  PCHECK_EQ(visit_splash(false, brought, before, hr, kAlertsAll, false, MLB, tb.p).text, "MESA JR.\nHOME RUN");
  const List nyy{make_player(MLB, 33912, 10, "BELLINGER")};
  PCHECK_EQ(visit_splash(false, brought, before, hr, kAlertsAll, false, MLB, nyy.p).text, "TB HOME RUN");
  // The Favorite player switch off, or the visited game in another league
  // than the list's: the team's splash.
  PCHECK_EQ(visit_splash(false, brought, before, hr, kAlertsAll & ~alert_bit(Alert::PLAYER), false, MLB, tb.p).text,
            "TB HOME RUN");
  PCHECK_EQ(visit_splash(false, brought, before, hr, kAlertsAll, false, NFL, tb.p).text, "TB HOME RUN");
  // A favorite player's score is no switch the watcher reads.
  PCHECK((watch_sport_mask(Sport::BASEBALL) & alert_bit(Alert::PLAYER)) == 0);
  // Back on the board's game after the visit: his home run while it was
  // away isn't splashed.
  PCHECK_EQ(splash(watch_back_prev(before, hr), hr, MLB, tb), "");
  // A game that ended meanwhile gets its win, never his name.
  GameSnapshot final_read = hr;
  final_read.state = GameState::POST;
  final_read.completed = true;
  PCHECK_EQ(splash(watch_back_prev(before, final_read), final_read, MLB, tb, false, kAlertsAll, true, true),
            "TB WINS!");
}

void test_basketball() {
  // IND 74, MIN 73 in the 3rd: McDaniels' jumper for MIN, read as MIN's game.
  const List min{make_player(NBA, 4431671, 16, "MCDANIELS")};
  GameSnapshot cur = read("fixtures/event_nba_live.json", Sport::BASKETBALL, 16, min, NBA);
  PCHECK(cur.team_abbr == "MIN" && cur.play_points == 2 && cur.play_athletes.size() == 1);
  // In the 3rd quarter it's a basket like any other: ESPN's last play is only
  // the latest at each read, so baskets don't splash.
  GameSnapshot prev = cur;
  prev.team_score -= 2;
  prev.play_id = "401914123410";
  PCHECK_EQ(splash(prev, cur, NBA, min), "");
  // The same basket taking the lead with a minute left: a late lead change.
  GameSnapshot late = cur, late_prev = prev;
  late.period = late_prev.period = 4;
  late_prev.display_clock = "1:10";
  late.display_clock = "0:58";
  late.team_score = 75;
  late.opp_score = 74;
  late_prev.team_score = 73;
  late_prev.opp_score = 74;
  PCHECK_EQ(splash(late_prev, late, NBA, min), "MCDANIELS\nLEAD CHANGE");
  PCHECK_EQ(splash(late_prev, late, NBA, min, true), "MCDANIELS\nLEAD CHANGE");
  PCHECK_EQ(splash(late_prev, late, NBA, List{}), "LEAD CHANGE!");
  // Someone else's go-ahead basket.
  GameSnapshot other = late;
  other.play_athletes = {PlayAthlete{4432816, 16}};
  PCHECK_EQ(splash(late_prev, other, NBA, min), "LEAD CHANGE!");
  // A buzzer beater at the end of the half.
  GameSnapshot bz = cur, bz_prev = prev;
  bz.period = bz_prev.period = 2;
  bz_prev.display_clock = "0.8";
  bz.display_clock = "0.0";
  bz_prev.team_score = 72;
  bz_prev.opp_score = 74;
  bz.team_score = 74;
  bz.opp_score = 74;
  PCHECK_EQ(splash(bz_prev, bz, NBA, min), "MCDANIELS\nBUZZER BEATER");
  // One that wins it, read while ESPN still has the game on: his.
  GameSnapshot horn = bz, horn_prev = bz_prev;
  horn.period = horn_prev.period = 4;
  horn.team_score = 75;
  PCHECK_EQ(splash(horn_prev, horn, NBA, min), "MCDANIELS\nBUZZER BEATER");
  // Read only at the final, ESPN's final document has no last play (no
  // situation at all), so nothing names him: the team's splash.
  GameSnapshot fin = read("fixtures/event_nba_final.json", Sport::BASKETBALL, 16, min, NBA);
  PCHECK(fin.state == GameState::POST && fin.completed && fin.play_athletes.empty() && fin.play_id.empty() &&
         fin.last_play.empty());
  GameSnapshot fin_prev = read("fixtures/event_nba_live.json", Sport::BASKETBALL, 16, min, NBA);
  fin_prev.period = 4;
  fin_prev.display_clock = "0.8";
  fin_prev.team_score = fin.team_score - 2;
  fin_prev.opp_score = fin.team_score - 1;
  fin.opp_score = fin_prev.opp_score;  // MIN's two at the horn win it by one
  fin.display_clock = "0.0";
  PCHECK_EQ(splash(fin_prev, fin, NBA, min), "BUZZER BEATER!");
  // Baseball's final document is the same: a walk-off read only at the final names nobody.
  const List chw{make_player(MLB, 1, 4, "BENINTENDI")};
  GameSnapshot mlb = read("fixtures/event_mlb_final.json", Sport::BASEBALL, 4, chw, MLB);
  PCHECK(mlb.state == GameState::POST && mlb.play_athletes.empty() && mlb.play_id.empty() && mlb.last_play.empty());
}

void test_soccer() {
  // ARS 1-0 from 58', then Saka's penalty at 90'+7'.
  const List ars{make_player(EPL, 280555, 359, "SAKA")};
  GameSnapshot end = read("fixtures/event_epl_final.json", Sport::SOCCER, 359, ars, EPL);
  end.state = GameState::IN;
  end.completed = false;
  GameSnapshot before = end;
  before.soc.goals.pop_back();
  before.team_score -= 1;
  PCHECK_EQ(splash(before, end, EPL, ars), "SAKA\nPENALTY GOAL");
  PCHECK_EQ(splash(before, end, EPL, ars, true), "SAKA\nPENALTY GOAL");  // 17 characters
  // The first goal is Guimaraes', not his.
  GameSnapshot first = before, nil = before;
  nil.soc.goals.pop_back();
  nil.team_score -= 1;
  PCHECK_EQ(splash(nil, first, EPL, ars), "GOAL!");
  PCHECK_EQ(splash(nil, first, EPL, List{make_player(EPL, 218522, 359, "GUIMARAES")}), "GUIMARAES\nGOAL");
  PCHECK_EQ(splash(nil, first, EPL, List{make_player(EPL, 218522, 359, "GUIMARAES")}, true), "GUIMARAES GOAL");
  // An own goal by a favorite counts for the other side: not his.
  GameSnapshot og = end;
  og.soc.goals.back().own_goal = true;
  og.soc.goals.back().ours = true;
  og.soc.goals.back().penalty = false;
  PCHECK_EQ(splash(before, og, EPL, ars), "GOAL!");
}

void test_hockey() {
  // COL @ WPG, 0-0, read as WPG's game.
  const List wpg{make_player(NHL, 3899937, 28, "CONNOR")};
  GameSnapshot prev = read("fixtures/event_nhl_live.json", Sport::HOCKEY, 28, wpg, NHL);
  GameSnapshot goal = prev;
  goal.team_score += 1;
  goal.last_play = "Kyle Connor Goal (10) Wrist Shot, assists: Mark Scheifele (20), Josh Morrissey (17)";
  PCHECK_EQ(splash(prev, goal, NHL, wpg), "CONNOR\nGOAL");
  PCHECK_EQ(splash(prev, goal, NHL, wpg, true), "CONNOR GOAL");
  // An assist isn't a goal.
  PCHECK_EQ(splash(prev, goal, NHL, List{make_player(NHL, 3041969, 28, "SCHEIFELE")}), "GOAL!");
  // Two favorites on the team with that name: nobody can say which.
  PCHECK_EQ(splash(prev, goal, NHL, List{make_player(NHL, 3899937, 28, "CONNOR"), make_player(NHL, 1, 28, "CONNOR")}),
            "GOAL!");
  // The same name on the other side.
  PCHECK_EQ(splash(prev, goal, NHL, List{make_player(NHL, 3899937, 17, "CONNOR")}), "GOAL!");
  // An initial tells two brothers apart.
  GameSnapshot brady = goal;
  brady.last_play = "Brady Tkachuk Goal (10) Snap Shot, assists: Matthew Tkachuk (20), Seth Jones (17)";
  const List tk{make_player(NHL, 10, 28, "M. TKACHUK"), make_player(NHL, 11, 28, "B. TKACHUK")};
  PCHECK_EQ(splash(prev, brady, NHL, tk), "B. TKACHUK\nGOAL");
  PCHECK_EQ(splash(prev, brady, NHL, List{make_player(NHL, 11, 28, "TKACHUK")}), "TKACHUK\nGOAL");
  PCHECK_EQ(splash(prev, brady, NHL, List{make_player(NHL, 10, 28, "M. TKACHUK")}), "GOAL!");
  // A fresh goal that came before its scorer, a part of a name, and an accent.
  GameSnapshot nameless = goal;
  nameless.last_play = ", assists: Neal Pionk (1), Dylan Samberg (1)";
  PCHECK_EQ(splash(prev, nameless, NHL, wpg), "GOAL!");
  GameSnapshot part = goal;
  part.last_play = "Kyle MacConnor Goal (1) Wrist Shot";
  PCHECK_EQ(splash(prev, part, NHL, wpg), "GOAL!");
  GameSnapshot accent = goal;
  accent.last_play = "Tim St\xc3\xbctzle Goal (3) Backhand";
  PCHECK_EQ(splash(prev, accent, NHL, List{make_player(NHL, 9, 28, "STUTZLE")}), "STUTZLE\nGOAL");
  // A second goal while ESPN's words still name the first scorer: the team's.
  GameSnapshot second = goal;
  second.team_score += 1;
  PCHECK_EQ(splash(goal, second, NHL, wpg), "GOAL!");
  // A goal against: the other side's play.
  GameSnapshot against = prev;
  against.opp_score += 1;
  against.last_play = goal.last_play;
  PCHECK_EQ(splash(prev, against, NHL, wpg), "COL GOAL");
  // A name the page cut to 10 characters is the start of the scorer's last name.
  struct Cut {
    const char *full, *words;
  };
  for (const Cut &c : {Cut{"Nugent-Hopkins", "Ryan Nugent-Hopkins Goal (5) Wrist Shot"},
                       Cut{"Marchessault", "Jonathan Marchessault Goal (4) Snap Shot, assists: Roman Josi (9)"},
                       Cut{"Ekman-Larsson", "Oliver Ekman-Larsson Goal (2) Slap Shot"},
                       Cut{"van Riemsdyk", "James van Riemsdyk Goal (7) Tip-In"},
                       Cut{"Pietrangelo", "Alex Pietrangelo Goal (1) Wrist Shot"},
                       Cut{"Bjorkstrand", "Oliver Bjorkstrand Goal (3) Backhand"},
                       Cut{"Siegenthaler", "Jonas Siegenthaler Goal (1) Wrist Shot"},
                       Cut{"E. Pettersson", "Elias Pettersson Goal (8) Wrist Shot"}}) {
    const std::string stored = clean_player_name(c.full);
    PCHECK(stored.size() == (size_t) kPlayerNameMax);
    GameSnapshot cut = goal;
    cut.last_play = c.words;
    PCHECK_EQ(splash(prev, cut, NHL, List{make_player(NHL, 7, 28, stored)}), stored + "\nGOAL");
  }
  const std::string marchessault = espn::detail::nhl_scorer("Jonathan Marchessault Goal (4) Snap Shot");
  PCHECK(espn::detail::nhl_name_is(marchessault, "MARCHESSAU"));
  PCHECK(!espn::detail::nhl_name_is(marchessault, "ARCHESSAUL"));  // not the start of a word
  PCHECK(espn::detail::nhl_name_is(marchessault, "J. MARCHES"));
  PCHECK(!espn::detail::nhl_name_is(marchessault, "M. MARCHES"));  // the initial still counts
  PCHECK(!espn::detail::nhl_name_is("KYLE CONNORS", "CONNOR"));    // a whole name is never cut
  PCHECK(espn::detail::nhl_name_is("JAMES VAN RIEMSDYK", "J. VAN RIE"));
  // More of the first name when teammates share the initial too, and a
  // last name with a dot in it.
  PCHECK(espn::detail::nhl_name_is("JEREMIAH SMITH", "JE. SMITH"));
  PCHECK(!espn::detail::nhl_name_is("JAMES SMITH", "JE. SMITH"));
  PCHECK(espn::detail::nhl_name_is("JACK ST. IVANY", "ST. IVANY"));
  PCHECK(!espn::detail::nhl_name_is("JACK IVANY", "ST. IVANY"));
  // Letters ESPN writes that the page folds the same way: \u00d8, \u2019.
  PCHECK(espn::detail::nhl_name_is(espn::detail::nhl_scorer("Mads S\xc3\xb8gaard Goal (1) Wrist Shot"), "SOGAARD"));
  PCHECK(espn::detail::nhl_name_is(espn::detail::nhl_scorer("Ryan O\xe2\x80\x99Reilly Goal (2) Wrist Shot"), "O'REILLY"));
  PCHECK(espn::detail::nhl_scorer("Kyle Connor Goal") == "KYLE CONNOR");
  PCHECK(espn::detail::nhl_scorer("Goal (10) Wrist Shot").empty());
  PCHECK(espn::detail::nhl_scorer("Delaying Game - Puck over glass served by Fedor Svechkov").empty());
}

// The panel's fireworks go with the play's words, never with a name: a
// favorite's splash gets them when the team's splash for the play would.
void test_fireworks() {
  struct Play {
    const char *word, *team;
    Sport sport;
  };
  const Play plays[] = {{"TOUCHDOWN", "TOUCHDOWN!", Sport::FOOTBALL}, {"FIELD GOAL", "FIELD GOAL!", Sport::FOOTBALL},
                        {"SAFETY", "SAFETY!", Sport::FOOTBALL},       {"SCORE", "SCORE!", Sport::FOOTBALL},
                        {"2 RBI", "2 RUNS!", Sport::BASEBALL},        {"HOME RUN", "HOME RUN!", Sport::BASEBALL},
                        {"GRAND SLAM", "GRAND SLAM!", Sport::BASEBALL}, {"WALK-OFF", "WALK-OFF!", Sport::BASEBALL},
                        {"GOAL", "GOAL!", Sport::HOCKEY},             {"GOAL", "GOAL!", Sport::SOCCER},
                        {"PENALTY GOAL", "PENALTY GOAL!", Sport::SOCCER}, {"LEAD CHANGE", "LEAD CHANGE!", Sport::BASKETBALL},
                        {"BUZZER BEATER", "BUZZER BEATER!", Sport::BASKETBALL}};
  for (const Play &pl : plays)
    for (const char *name : {"WINSTON", "WINSLOW", "ALLEN", "J. WINSTON"})
      for (bool wide : {false, true}) {
        List list{make_player(NFL, 1, 2, name)};
        PlayerScore s;
        s.slot = 0;
        s.word = pl.word;
        GameSnapshot g;
        const std::string text = esphome::gameday::panel_text(player_splash(s, g, list.p, wide).text, true);
        PCHECK(splash_fireworks(text, pl.sport) == splash_fireworks(pl.team, pl.sport));
        if (splash_fireworks(text, pl.sport) != splash_fireworks(pl.team, pl.sport))
          printf("  \"%s\" vs \"%s\"\n", text.c_str(), pl.team);
      }
  PCHECK(!splash_fireworks("WINSTON\n2-PT", Sport::FOOTBALL));
  PCHECK(splash_fireworks("DAL WINS!", Sport::FOOTBALL));
  PCHECK(splash_fireworks("SCHEFFLER WINS!", Sport::GOLF));
  PCHECK(!splash_fireworks("FIELD GOAL!", Sport::FOOTBALL));
  PCHECK(splash_fireworks("REMO GOAL", Sport::SOCCER));
  PCHECK(!splash_fireworks("PHI\nFUMBLE RECOVERY", Sport::FOOTBALL));
}

// Every word a favorite's splash can have, with every name length the panel
// keeps, on every board: one to four panels in a row and the 2x2, whose
// splash covers all four panels with its words in the middle 64 rows
// (board_layout.h). The name on its own first line, three lines at most that
// end inside those 64 rows, no word cut, and on three or four panels never
// more lines than on two.
void test_splash_fits() {
  using esphome::gameday::panel_text;
  using esphome::gameday::splash_box;
  using esphome::gameday::splash_chars;
  using esphome::gameday::SplashBox;
  const char *words[] = {"TOUCHDOWN", "FIELD GOAL", "SAFETY", "SCORE", "RBI", "4 RBI", "HOME RUN",
                         "GRAND SLAM", "WALK-OFF", "GOAL", "PENALTY GOAL", "LEAD CHANGE", "BUZZER BEATER"};
  struct Screen {
    int w, h;
  };
  GameSnapshot g;
  g.team_color = "002a5c";
  for (const char *word : words)
    for (size_t n = 1; n <= (size_t) kPlayerNameMax; n++)
      for (Screen sc : {Screen{64, 64}, Screen{128, 64}, Screen{192, 64}, Screen{256, 64}, Screen{128, 128}})
        for (const char *shape : {"A", "A-", "A ", "A."}) {
          const bool wide = sc.w >= 128;  // GamedayComponent::wide_(): two panels or more
          std::string name(n, shape[0]);
          if (n >= 3 && shape[1] != 0)
            name[n / 2] = shape[1];
          List list{make_player(NFL, 1, 2, clean_player_name(name))};
          PlayerScore s;
          s.slot = 0;
          s.word = word;
          const std::string raw = player_splash(s, g, list.p, wide).text;
          const std::string text = panel_text(raw, true);
          const auto lines = splash_test::layout(text, splash_chars(sc.w));
          const std::string kept = list.p[0].name;
          const SplashBox b = splash_box(sc.w, sc.h);
          const int line_rows = wide ? 16 : 12;  // spleen_16 from two panels, spleen_12 on one
          const bool ok = !lines.empty() && lines.size() <= 3 && lines[0].compare(0, kept.size(), kept) == 0 &&
                          b.text_y + (int) lines.size() * line_rows <= b.band_y + esphome::gameday::kBoardRows;
          PCHECK(ok);
          if (!ok)
            printf("  \"%s\" on %dx%d\n", text.c_str(), sc.w, sc.h);
          if (sc.w > 128)
            PCHECK(lines.size() <= splash_test::layout(text, 16).size());
        }
  PlayerScore none;
  List any{make_player(NFL, 1, 2, "X")};
  PCHECK(player_splash(none, g, any.p, false).text.empty());
}

}  // namespace

int run_player_tests(int &total_checks) {
  test_list();
  test_names();
  test_read();
  test_stat_lines();
  test_football();
  test_college_try();
  test_baseball();
  test_watcher();
  test_basketball();
  test_soccer();
  test_hockey();
  test_fireworks();
  test_splash_fits();
  total_checks += checks;
  return failures;
}
