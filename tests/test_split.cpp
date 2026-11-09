// Split mode (components/gameday/split.h): where the second area goes on
// each arrangement, what it shows under the owner's choice and the night
// hours, the rotation of other live games and the watcher's part in it
// (watch.h), the scores rotation's card for it, its team's polls, and Off or
// dim lifted for it (idle.h).
#include <cstdio>
#include <string>
#include <vector>

#include "idle.h"
#include "recap.h"
#include "split.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define PCHECK(cond)                                         \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

void test_split_area() {
  // One and two panels: no second area, whatever the choice.
  for (int show = 0; show < kSplitCount; show++) {
    for (int cols = 1; cols <= 2; cols++) {
      const SplitArea a = split_area(cols, 1, (uint8_t) show);
      PCHECK(!a.on && a.main_w == cols * 64);
      PCHECK(split_effective((uint8_t) show, split_at(cols, 1)) == SplitShow::OFF);
    }
  }
  PCHECK(split_at(1, 1) == SplitAt::NONE && split_at(2, 1) == SplitAt::NONE);
  PCHECK(split_at(3, 1) == SplitAt::RIGHT1 && split_at(4, 1) == SplitAt::RIGHT2 && split_at(2, 2) == SplitAt::BOTTOM);
  PCHECK(std::string(split_at_key(SplitAt::BOTTOM)) == "bottom" && std::string(split_at_key(SplitAt::NONE)).empty());
  // Three and four wide: Off stretches the board across the row.
  PCHECK(!split_area(3, 1, 0).on && split_area(3, 1, 0).main_w == 192);
  PCHECK(!split_area(4, 1, 0).on && split_area(4, 1, 0).main_w == 256);
  for (int show = 1; show < kSplitCount; show++) {
    const SplitArea r1 = split_area(3, 1, (uint8_t) show);
    PCHECK(r1.on && r1.main_w == 128 && r1.x == 128 && r1.y == 0 && r1.w == 64 && r1.h == 64);
    const SplitArea r2 = split_area(4, 1, (uint8_t) show);
    PCHECK(r2.on && r2.main_w == 128 && r2.x == 128 && r2.y == 0 && r2.w == 128 && r2.h == 64);
  }
  // Two rows of two: always the bottom half, Off as the clock.
  for (int show = 0; show < kSplitCount; show++) {
    const SplitArea b = split_area(2, 2, (uint8_t) show);
    PCHECK(b.on && b.main_w == 128 && b.x == 0 && b.y == 64 && b.w == 128 && b.h == 64);
  }
  PCHECK(split_effective(0, SplitAt::BOTTOM) == SplitShow::CLOCK);
  PCHECK(split_effective(0, SplitAt::RIGHT2) == SplitShow::OFF);
  PCHECK(split_effective(3, SplitAt::RIGHT1) == SplitShow::SCORES);
  // A byte that makes no sense is Off, the clock on two rows of two.
  PCHECK(split_effective(9, SplitAt::RIGHT2) == SplitShow::OFF);
  PCHECK(split_effective(9, SplitAt::BOTTOM) == SplitShow::CLOCK);
}

void test_area_view() {
  AreaInputs in;
  in.show = SplitShow::OFF;
  PCHECK(area_view(in) == AreaView::NONE);
  in.show = SplitShow::CLOCK;
  PCHECK(area_view(in) == AreaView::CLOCK);
  in.main_clock = true;  // never a second clock
  PCHECK(area_view(in) == AreaView::DATE);
  PCHECK(std::string(area_view_key(AreaView::DATE)) == "date");
  in.main_clock = false;

  in.show = SplitShow::SCORES;
  PCHECK(area_view(in) == AreaView::CLOCK);  // no card yet
  in.scores = true;
  PCHECK(area_view(in) == AreaView::SCORES);
  in.night = true;  // the scores give way to the clock at night
  PCHECK(area_view(in) == AreaView::CLOCK);
  in.night = false;

  in = AreaInputs{};
  in.show = SplitShow::TEAM;
  PCHECK(area_view(in) == AreaView::CLOCK);  // no team: the clock, which asks for one
  in.team_set = true;
  PCHECK(area_view(in) == AreaView::CLOCK);  // nothing read yet
  in.team_card = true;
  PCHECK(area_view(in) == AreaView::TEAM);
  in.team_main = true;  // its game is the main board's
  PCHECK(area_view(in) == AreaView::CLOCK);
  in.team_main = false;
  in.night = true;  // its next game gives way at night, its game on doesn't
  PCHECK(area_view(in) == AreaView::CLOCK);
  in.team_on = true;
  PCHECK(area_view(in) == AreaView::TEAM);

  in = AreaInputs{};
  in.show = SplitShow::LIVE;
  PCHECK(area_view(in) == AreaView::CLOCK);  // nothing else live, no scores
  in.scores = true;
  PCHECK(area_view(in) == AreaView::SCORES);  // falls back to the scores
  in.rot_games = true;
  PCHECK(area_view(in) == AreaView::LIVE);
  in.night = true;
  PCHECK(area_view(in) == AreaView::CLOCK);
  in.jump = true;  // a big play still shows at night
  PCHECK(area_view(in) == AreaView::LIVE);
}

void test_scores_step() {
  PCHECK(scores_step(0, 0, false) == -1);
  PCHECK(scores_step(3, 10, false) == 3);  // the area alone: the rotation's own card
  PCHECK(scores_step(3, 10, true) == 8);   // half a loop from the main board's
  PCHECK(scores_step(7, 10, true) == 2);
  PCHECK(scores_step(0, 1, true) == -1);  // one card, and the main board has it
  PCHECK(scores_step(0, 1, false) == 0);
  for (size_t n = 2; n < 30; n++)
    for (size_t step = 0; step < n; step++)
      PCHECK(scores_step(step, n, true) != (int) step);
}

void test_a2_poll() {
  PCHECK(a2_poll_ms(GameState::IN, 0, false, false) == 10000);
  PCHECK(a2_poll_ms(GameState::IN, 0, false, true) == 15000);  // the main board's game is live too
  PCHECK(a2_poll_ms(GameState::POST, 0, false, false) == 60000);
  PCHECK(a2_poll_ms(GameState::PRE, 1800, false, false) == 60000);
  PCHECK(a2_poll_ms(GameState::PRE, 1800, true, false) == 15 * 60000);  // no time yet
  PCHECK(a2_poll_ms(GameState::PRE, 7200, false, true) == 15 * 60000);
  PCHECK(a2_poll_ms(GameState::NOT_FOUND, 0, false, false) == 6u * 3600u * 1000u);
}

WatchGame game(const char *event, uint8_t league, GameState st, const char *away = "AAA", const char *home = "BBB") {
  WatchGame g;
  snprintf(g.event_id, sizeof(g.event_id), "%s", event);
  g.league = league;
  g.state = st;
  watch_teams(g, away, home);
  return g;
}

void test_rotation() {
  const uint8_t nhl = (uint8_t) League::NHL, nba = (uint8_t) League::NBA;
  std::vector<WatchGame> games = {game("1", nhl, GameState::IN), game("2", nhl, GameState::NOT_FOUND),
                                  game("3", nba, GameState::IN), game("4", nhl, GameState::IN)};
  // In list order from the start, skipping a game not read yet.
  PCHECK(rot_pick(games, 0, "", 0, "") == 0);
  PCHECK(rot_pick(games, nhl, "1", 0, "") == 2);
  PCHECK(rot_pick(games, nba, "3", 0, "") == 3);
  PCHECK(rot_pick(games, nhl, "4", 0, "") == 0);  // around to the start
  // Never the main board's game.
  PCHECK(rot_pick(games, nhl, "1", nba, "3") == 3);
  PCHECK(rot_count(games, nba, "3") == 2);
  // A card that went away: from the start.
  PCHECK(rot_pick(games, nhl, "99", 0, "") == 0);
  // One game: it stays up.
  std::vector<WatchGame> one = {game("7", nhl, GameState::IN)};
  PCHECK(rot_pick(one, nhl, "7", 0, "") == 0);
  PCHECK(rot_pick(one, 0, "", nhl, "7") == -1);
  PCHECK(rot_count({}, 0, "") == 0);

  // The ticker: the other live games, not the card's nor the main board's.
  games[0].team_score = 3;
  games[0].opp_score = 2;
  watch_teams(games[0], "BOS", "NYR");
  watch_teams(games[3], "TOR", "MTL");
  watch_teams(games[2], "LAL", "GSW");
  const std::string t = rot_ticker(games, nhl, "4", nba, "3");
  PCHECK(t == "BOS 3 NYR 2");
  PCHECK(rot_ticker(games, 0, "", 0, "") == "BOS 3 NYR 2 | LAL 0 GSW 0 | TOR 0 MTL 0");

  // A card from a watched game's read.
  WatchGame w = game("401", nhl, GameState::IN, "BOS", "NYR");
  w.away_id = 1;
  w.home_id = 13;
  w.team_score = 2;
  w.opp_score = 1;
  w.period = 3;
  w.away_rgb[0] = 0xFF;
  w.home_rank = 0;
  w.away_rank = 4;
  snprintf(w.clock, sizeof(w.clock), "%s", "5:12");
  const GameSnapshot c = rot_card(w);
  PCHECK(c.valid && c.state == GameState::IN && c.event_id == "401" && c.sport == Sport::HOCKEY);
  PCHECK(c.team_abbr == "BOS" && c.opp_abbr == "NYR" && c.team_id == 1 && c.opp_id == 13);
  PCHECK(c.team_score == 2 && c.opp_score == 1 && c.period == 3 && c.display_clock == "5:12");
  PCHECK(c.team_color == "FF0000" && c.opp_color == "000000" && c.team_rank == 4);
  PCHECK(!c.team_logo.empty() && c.team_logo != c.opp_logo);
  PCHECK(c.play_id.empty() && c.down_distance.empty() && c.soc.goals.empty());
}

// A rotation card's clock row reads as the board's does for that sport: the
// watcher keeps no ESPN status, so the card stands one in (rot_status).
std::string rot_clock(uint8_t league, uint8_t period, const char *clock, Half half = Half::NONE, uint8_t reg = 4) {
  WatchGame w = game("9", league, GameState::IN);
  w.period = period;
  w.regulation = reg;
  w.half = half;
  snprintf(w.clock, sizeof(w.clock), "%s", clock);
  return clock_text(rot_card(w));
}

void test_rot_clock() {
  const uint8_t nfl = (uint8_t) League::NFL, ncaa = (uint8_t) League::NCAA, nba = (uint8_t) League::NBA,
                wnba = (uint8_t) League::WNBA, mcbb = (uint8_t) League::MCBB, nhl = (uint8_t) League::NHL,
                mlb = (uint8_t) League::MLB, epl = (uint8_t) League::EPL, mls = (uint8_t) League::MLS;
  PCHECK(rot_clock(nfl, 3, "5:36") == "5:36 3rd");
  PCHECK(rot_clock(ncaa, 1, "12:00") == "12:00 1st");
  PCHECK(rot_clock(nfl, 5, "8:12") == "8:12 OT");
  PCHECK(rot_clock(nfl, 2, "0:00") == "Halftime");
  PCHECK(rot_clock(nfl, 3, "0:00") == "End of 3rd");
  PCHECK(rot_clock(nba, 4, "1:04") == "1:04 4th");
  PCHECK(rot_clock(nba, 4, "45.3") == "45.3 4th");
  PCHECK(rot_clock(wnba, 2, "0.0") == "Halftime");
  PCHECK(rot_clock(nba, 3, "0.0") == "End 3rd");
  PCHECK(rot_clock(nba, 5, "2:10") == "2:10 OT");
  PCHECK(rot_clock(mcbb, 2, "12:34", Half::NONE, 2) == "12:34 2nd");
  PCHECK(rot_clock(mcbb, 1, "0:00", Half::NONE, 2) == "Halftime");
  PCHECK(rot_clock(nhl, 2, "10:00") == "10:00 2nd");
  PCHECK(rot_clock(nhl, 4, "3:10") == "3:10 OT");
  PCHECK(rot_clock(nhl, 1, "0:00") == "End of 1st");
  PCHECK(rot_clock(mlb, 5, "", Half::TOP) == "Top 5th");
  PCHECK(rot_clock(mlb, 7, "", Half::MID) == "Mid 7th");
  PCHECK(rot_clock(mlb, 11, "", Half::BOTTOM) == "Bot 11th");
  PCHECK(rot_clock(mlb, 2, "", Half::END) == "End 2nd");
  PCHECK(rot_clock(epl, 1, "67'") == "67'");
  PCHECK(rot_clock(mls, 2, "90'+3'") == "90'+3'");
  PCHECK(rot_period(1, 4) == "1st" && rot_period(12, 99) == "12th" && rot_period(21, 99) == "21st");
  PCHECK(rot_period(6, 4) == "2OT");
}

GameSnapshot hockey(const char *event, uint32_t away, uint32_t home, int as, int hs) {
  GameSnapshot g;
  g.valid = true;
  g.event_id = event;
  g.sport = Sport::HOCKEY;
  g.state = GameState::IN;
  g.period = 2;
  g.display_clock = "10:00";
  g.team_id = away;
  g.opp_id = home;
  g.team_abbr = "BOSTON";  // longer than a card keeps
  g.opp_abbr = "NYR";
  g.team_color = "FFB81C";
  g.opp_color = "0038A8";
  g.team_rank = 0;
  g.opp_rank = 7;
  g.team_score = as;
  g.opp_score = hs;
  return g;
}

void test_watch_all() {
  const uint8_t nhl = (uint8_t) League::NHL;
  WatchJob job;
  job.league = nhl;
  job.cycle = 5;
  job.ctx.live = false;  // My team
  job.ctx.all = true;    // the second area rotates every live game
  job.ctx.read_mask = kAlertsAll;
  job.ctx.mask = 0;  // every alert switch off
  job.ctx.team = {(uint8_t) League::NFL, 6};
  GameSnapshot g = hockey("501", 1, 13, 0, 0);
  watch_take(job, g);
  PCHECK(job.fresh.size() == 1 && !job.fresh[0].fav);
  const WatchGame &w = job.fresh[0];
  PCHECK(std::string(w.away_abbr) == "BOSTO" && std::string(w.home_abbr) == "NYR");  // cut to 5, still ended
  PCHECK(w.away_rgb[0] == 0xFF && w.away_rgb[1] == 0xB8 && w.away_rgb[2] == 0x1C && w.home_rank == 7);
  // The next read has a goal: kept, but no alert with every switch off.
  job.games = job.fresh;
  job.fresh.clear();
  job.cycle = 6;
  GameSnapshot goal = hockey("501", 1, 13, 1, 0);
  watch_take(job, goal);
  PCHECK(job.fresh.size() == 1 && !job.alert.valid);
  // With the goal switch on, the goal counts.
  job.fresh.clear();
  job.ctx.mask = alert_bit(Alert::HOCKEY_GOAL);
  watch_take(job, goal);
  PCHECK(job.alert.valid && job.alert.event_id == "501");
  // Favorite teams only: kept, no alert for a game with no favorite in it.
  job.fresh.clear();
  job.alert = WatchAlert{};
  job.ctx.fav_only = true;
  watch_take(job, goal);
  PCHECK(job.fresh.size() == 1 && !job.alert.valid);
  // A favorite's game still alerts.
  job.fresh.clear();
  job.ctx.favs.emplace_back(nhl, 1);
  watch_take(job, goal);
  PCHECK(job.fresh.size() == 1 && job.fresh[0].fav && job.alert.valid && job.alert.fav);
  // Never the main board's game, nor the second area's team's.
  job.fresh.clear();
  job.alert = WatchAlert{};
  job.ctx.board_league = nhl;
  job.ctx.board_event = "501";
  watch_take(job, goal);
  PCHECK(job.fresh.empty());
  job.ctx.board_event.clear();
  job.ctx.a2_league = nhl;
  job.ctx.a2_event = "501";
  watch_take(job, goal);
  PCHECK(job.fresh.empty() && !job.alert.valid);
  // Without all, My team watches only the favorites' games, as before.
  WatchCtx c;
  c.mask = kAlertsAll;
  PCHECK(watch_wants_teams(c, nhl, 1, 13) == 0);
  c.all = true;
  c.read_mask = kAlertsAll;
  PCHECK(watch_wants_teams(c, nhl, 1, 13) == 1);
  c.team = {nhl, 13};  // the panel's team counts as a favorite in the rotation
  PCHECK(watch_wants_teams(c, nhl, 1, 13) == 2);
  // Golf and racing are never watched.
  PCHECK(watch_wants_teams(c, (uint8_t) League::PGA, 1, 2) == 0);
  // Nor minor league baseball, the AHL or the ECHL, which ESPN doesn't have:
  // the rotation reads ESPN only, even with the panel's team in one of them.
  c.team = {(uint8_t) League::MILB, 234};
  PCHECK(watch_wants_teams(c, (uint8_t) League::MILB, 234, 260) == 0);
  PCHECK(watch_wants_teams(c, (uint8_t) League::AHL, 324, 330) == 0);
  PCHECK(watch_wants_teams(c, (uint8_t) League::ECHL, 70, 74) == 0);

  // A big play has the second area on 501. Its first read splashes the goal
  // since the watcher's read; each read then stands as the watcher's, so the
  // watcher's next read finds no goal the area already splashed.
  WatchGame seen = game("501", nhl, GameState::IN, "BOSTO", "NYR");
  seen.away_id = 1;
  seen.home_id = 13;
  watch_store(seen, hockey("501", 1, 13, 0, 0));
  const uint32_t mask = alert_bit(Alert::HOCKEY_GOAL);
  PCHECK(!watch_splash(seen, goal, mask, true).text.empty());
  watch_sync(seen, goal, 9);
  PCHECK(seen.team_score == 1 && seen.read_cycle == 9 && seen.tried_cycle == 9);
  PCHECK(watch_splash(seen, goal, mask, true).text.empty());
  PCHECK(std::string(seen.away_abbr) == "BOSTO");  // the card keeps its teams
  job.games = {seen};
  job.fresh.clear();
  job.alert = WatchAlert{};
  job.cycle = 10;
  job.ctx = WatchCtx{};
  job.ctx.all = true;
  job.ctx.read_mask = kAlertsAll;
  job.ctx.mask = mask;
  watch_take(job, goal);
  PCHECK(job.fresh.size() == 1 && !job.alert.valid);
  GameSnapshot two = hockey("501", 1, 13, 2, 0);
  job.fresh.clear();
  watch_take(job, two);
  PCHECK(job.alert.valid);
}

void test_watch_sources_team() {
  // The rotation in My team adds the team's league: a pro league as a whole,
  // a college team by its conference, each once.
  const Team *nyr = nullptr, *ala = nullptr;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::NHL && kTeams[i].espn_id == 13)
      nyr = &kTeams[i];
    if (kTeams[i].league == League::NCAA && kTeams[i].espn_id == 333)
      ala = &kTeams[i];
  }
  PCHECK(nyr != nullptr && ala != nullptr);
  const std::vector<WatchSource> src = watch_sources(false, {}, {nyr, ala, nyr}, kAlertsAll);
  PCHECK(src.size() == 2);
  PCHECK(src[0].league == (uint8_t) League::NHL && src[0].group == 0 && src[0].by_event && !src[0].fed);
  PCHECK(src[1].league == (uint8_t) League::NCAA && src[1].group == ala->group && src[1].by_event);
  // A minor league team (MLB's or HockeyTech's) adds no source: the rotation
  // never asks ESPN for their scoreboards.
  const Team *dur = nullptr, *syr = nullptr;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::MILB && kTeams[i].espn_id == 234)
      dur = &kTeams[i];
    if (kTeams[i].league == League::AHL && kTeams[i].espn_id == 324)
      syr = &kTeams[i];
  }
  PCHECK(dur != nullptr && syr != nullptr);
  PCHECK(watch_sources(false, {}, {dur, syr}, kAlertsAll).empty());
  PCHECK(watch_sources(false, {League::MILB, League::AHL}, {nyr, dur}, kAlertsAll).size() == 1);
}

void test_area2_busy() {
  IdleInputs in;
  in.clock_valid = true;
  in.minute = 12 * 60;
  in.show = (uint8_t) IdleShow::OFF;
  in.board_ready = true;
  in.connected = true;
  PCHECK(idle_state(in).dim);
  in.area2_busy = true;  // the second area's team is on: the panel lights
  const IdleState s = idle_state(in);
  PCHECK(!s.dim && s.view == IdleView::CLOCK);  // the main board's view stays
}

void test_recap_one_league() {
  // A scan of one league keeps the others' games to come and shares the
  // later-today cap with them.
  const int64_t now = 1762700000;  // a Sunday afternoon
  std::vector<RecapDay> days;
  std::vector<Pending> nba, nhl;
  for (int i = 0; i < 6; i++) {
    Pending p;
    p.league = (uint8_t) League::NBA;
    p.kickoff_epoch = now + 600 + i;
    p.away_abbr = "A";
    p.home_abbr = "B";
    nba.push_back(p);
    p.league = (uint8_t) League::NHL;
    nhl.push_back(p);
  }
  recap_absorb(days, {}, nba, now, recap_bit((uint8_t) League::NBA));
  PCHECK(recap_count(days, (uint8_t) League::NBA) == 0);
  size_t kept_nba = 0, kept_nhl = 0;
  for (const auto &d : days)
    (d.league == (uint8_t) League::NBA ? kept_nba : kept_nhl) += d.later.size();
  PCHECK(kept_nba == 6 && kept_nhl == 0);
  recap_absorb(days, {}, nhl, now, recap_bit((uint8_t) League::NHL));
  kept_nba = kept_nhl = 0;
  for (const auto &d : days)
    (d.league == (uint8_t) League::NBA ? kept_nba : kept_nhl) += d.later.size();
  // The NBA's games stay until its own next scan; the NHL gets its even
  // share of the cap, counting the NBA's, not the whole cap.
  PCHECK(kept_nba == 6 && kept_nhl == kRecapLaterCap / 2);
}

}  // namespace

int run_split_tests(int &total_checks) {
  test_split_area();
  test_area_view();
  test_scores_step();
  test_a2_poll();
  test_rotation();
  test_rot_clock();
  test_watch_all();
  test_watch_sources_team();
  test_area2_busy();
  test_recap_one_league();
  total_checks += checks;
  return failures;
}
