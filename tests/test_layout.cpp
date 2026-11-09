// A splash with its logo and points (board_layout.h, splash_layout; the
// splash's side and points from alerts.h, players.h and the win), and the
// Clock size and Text size layouts (game_geom, clock_card_geom,
// lower_half), on every board: one to four panels in a row, two rows of two,
// and the second area's 64x64 and 128x64. Every splash the panel can put up
// keeps its logo, its words in the lines they fit and its points, inside
// the screen with nothing on top of anything else.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "alerts.h"
#include "board_layout.h"
#include "panel_text.h"
#include "players.h"
#include "splash_layout.h"

using namespace espn;
using esphome::gameday::BoardFont;
using esphome::gameday::font_desc_bottom;
using esphome::gameday::font_ink_bottom;
using esphome::gameday::font_ink_top;
using esphome::gameday::font_px;
using esphome::gameday::font_rows;
using esphome::gameday::kBoardRows;
using esphome::gameday::kSplashTile;
using esphome::gameday::panel_text;
using esphome::gameday::splash_layout;
using esphome::gameday::SplashLayout;
using esphome::gameday::wrap_lines;

namespace {

int checks = 0;
int failures = 0;

#define LCHECK(cond)                                           \
  do {                                                         \
    checks++;                                                  \
    if (!(cond)) {                                             \
      failures++;                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                          \
  } while (0)

#define LCHECK_EQ(a, b)                                                                       \
  do {                                                                                        \
    checks++;                                                                                 \
    const std::string va = (a), vb = (b);                                                     \
    if (va != vb) {                                                                           \
      failures++;                                                                             \
      printf("FAIL %s:%d: %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
    }                                                                                         \
  } while (0)

struct Screen {
  int w, h;
};
// The boards a splash covers: one to four panels in a row, two rows of two
// (all four panels), and the second area's 64x64 (three wide) and 128x64
// (four wide, or a 2x2's bottom half), which have the same sizes as one and
// two panels.
const Screen kScreens[] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};

struct Box {
  int x0, y0, x1, y1;  // [x0, x1) x [y0, y1)
  bool empty() const { return x1 <= x0 || y1 <= y0; }
};
bool overlap(const Box &a, const Box &b) {
  return !a.empty() && !b.empty() && a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
bool inside(const Box &a, const Screen &s) { return a.x0 >= 0 && a.y0 >= 0 && a.x1 <= s.w && a.y1 <= s.h; }

// A label's lines of text centered in a spot: the rows the capitals and
// digits ink, across the longest line's width.
Box text_box(int x, int y, int w, BoardFont f, const std::vector<std::string> &lines) {
  size_t widest = 0;
  for (const auto &l : lines)
    widest = std::max(widest, l.size());
  const int px = (int) widest * font_px(f);
  const int left = x + (w - px) / 2;
  return Box{left, y + font_ink_top(f), left + px, y + ((int) lines.size() - 1) * font_rows(f) + font_ink_bottom(f) + 1};
}

// What the panel draws for a splash on a screen, as board_view.cpp does: the
// words without the side's name beside its logo, laid out, and checked.
// Returns the layout; ok false (with the reason printed) when anything is
// off.
SplashLayout check_splash(const Splash &sp, const Screen &sc, bool &ok) {
  ok = true;
  const int tiles = sp.both ? 2 : sp.abbr.empty() ? 0 : 1;
  const std::string words =
      panel_text(tiles == 1 ? sp.text.substr(std::min<size_t>(sp.lead, sp.text.size())) : sp.text, true);
  const SplashLayout L = splash_layout(sc.w, sc.h, tiles, sp.points, words);
  auto fail = [&](const char *why) {
    if (ok)
      printf("  \"%s\" (%s) on %dx%d: %s\n", sp.text.c_str(), sp.points.c_str(), sc.w, sc.h, why);
    ok = false;
  };
  if (tiles == 0) {
    if (L.logos)
      fail("a logo with no side");
    return L;
  }
  if (!L.logos || L.tiles != tiles) {
    fail("no logo");
    return L;
  }
  // The words: in the lines LVGL makes of them, every word whole.
  const size_t chars = (size_t) (L.words.w / font_px(L.words_font));
  const std::vector<std::string> lines = splash_test::layout(words, chars);
  if (lines.empty() || lines.size() != L.lines || wrap_lines(words, chars) != L.lines)
    fail("the words' lines");
  const Box wb = text_box(L.words.x, L.words.y, L.words.w, L.words_font, lines);
  // The whole label's rows, its last line's descenders too, stay on the screen.
  if (!inside(wb, sc) || L.words.y + ((int) L.lines - 1) * font_rows(L.words_font) +
                                 font_desc_bottom(L.words_font) >= sc.h)
    fail("the words off the screen");
  if (L.words.x < 0 || L.words.x + L.words.w > sc.w)
    fail("the words' label off the screen");
  std::vector<Box> boxes{wb};
  for (int i = 0; i < L.tiles; i++) {
    const Box t{L.tile_x[i], L.tile_y, L.tile_x[i] + L.tile_px, L.tile_y + L.tile_px};
    if (!inside(t, sc))
      fail("a logo off the screen");
    if (L.tile_px < esphome::gameday::kSplashLogo)
      fail("a logo's tile smaller than the logo");
    boxes.push_back(t);
  }
  // The points, when the play has any: all of them or their number, in the
  // spot they are centered in.
  if (tiles == 1 && !sp.points.empty()) {
    if (!L.points) {
      fail("no points");
    } else {
      const std::string shown = L.points_whole ? sp.points : esphome::gameday::points_number(sp.points);
      const int px = (int) shown.size() * font_px(L.points_font);
      if (px > L.pts.w)
        fail("the points wider than their spot");
      if (L.pts.x < 0 || L.pts.x + L.pts.w > sc.w)
        fail("the points' spot off the screen");
      const Box pb = text_box(L.pts.x, L.pts.y, L.pts.w, L.points_font, {shown});
      if (!inside(pb, sc))
        fail("the points off the screen");
      boxes.push_back(pb);
    }
  } else if (L.points) {
    fail("points for a play with none");
  }
  for (size_t i = 0; i < boxes.size(); i++)
    for (size_t j = i + 1; j < boxes.size(); j++)
      if (overlap(boxes[i], boxes[j]))
        fail("two parts on top of each other");
  return L;
}

// LVGL's line breaks (splash_test::layout) and the layout's count agree.
void test_wrap_lines() {
  for (const char *t : {"", "TOUCHDOWN!", "PHI TOUCHDOWN", "INTER-CEPTION!", "CIN\nINTER-CEPTION", "A B C D",
                        "FUMBLE RECOVERY!", "J. WINSTON\nBUZZER BEATER", "NYY WALK-OFF", "\n\n", "A-B-C-D-E-F",
                        "WINSTON 2-PT", "  LEADING", "TRAILING  ", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"})
    for (size_t w : {1, 4, 5, 8, 10, 12, 15, 16, 21, 24, 32}) {
      const auto lines = splash_test::layout(t, w);
      LCHECK(wrap_lines(t, w) == lines.size());
      if (wrap_lines(t, w) != lines.size())
        printf("  \"%s\" at %zu: %zu vs %zu\n", t, w, wrap_lines(t, w), lines.size());
    }
  LCHECK(wrap_lines("TOUCHDOWN", 0) == 0);
}

// Who a splash is about and what it scored: the side's logo for a score, a
// turnover, a card, a lead change or a win, both logos for overtime and the
// final, and the points for a score only.
void test_splash_sides() {
  GameSnapshot g;
  g.team_abbr = "DAL";
  g.opp_abbr = "PHI";
  g.team_color = "002a5c";
  g.opp_color = "004c54";
  auto play = [&](Alert a, AlertSide side, int pts, bool neutral = false, bool wide = true) {
    return big_play_splash(BigPlay{a, side, pts}, g, neutral, wide);
  };
  Splash s = play(Alert::TOUCHDOWN, AlertSide::TEAM, 6);
  LCHECK_EQ(s.text, "TOUCHDOWN!");
  LCHECK_EQ(s.abbr, "DAL");
  LCHECK(s.lead == 0 && !s.both);
  LCHECK_EQ(s.points, "+6");
  s = play(Alert::TOUCHDOWN, AlertSide::OPP, 7);
  LCHECK_EQ(s.text, "PHI TOUCHDOWN");
  LCHECK_EQ(s.abbr, "PHI");
  LCHECK_EQ(s.text.substr(s.lead), "TOUCHDOWN");
  LCHECK_EQ(s.points, "+7");
  LCHECK_EQ(play(Alert::FIELD_GOAL, AlertSide::TEAM, 3).points, "+3");
  LCHECK_EQ(play(Alert::EXTRA_POINT, AlertSide::TEAM, 1).points, "+1");
  LCHECK_EQ(play(Alert::TWO_POINT, AlertSide::TEAM, 2).points, "+2");
  LCHECK_EQ(play(Alert::SAFETY, AlertSide::OPP, 2).points, "+2");
  LCHECK_EQ(play(Alert::SCORE, AlertSide::TEAM, 9).points, "+9");
  // Baseball: a home run's runs, a plain run's number (its words count them)
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 2).points, "+2 RUNS");
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 1).points, "+1 RUN");
  LCHECK_EQ(play(Alert::GRAND_SLAM, AlertSide::OPP, 4).points, "+4 RUNS");
  LCHECK_EQ(play(Alert::WALK_OFF, AlertSide::TEAM, 1).points, "+1 RUN");
  s = play(Alert::RUN, AlertSide::TEAM, 2);
  LCHECK_EQ(s.text, "2 RUNS!");
  LCHECK_EQ(s.points, "+2");
  LCHECK_EQ(play(Alert::HOCKEY_GOAL, AlertSide::TEAM, 1).points, "+1");
  LCHECK_EQ(play(Alert::SOCCER_GOAL, AlertSide::OPP, 1).points, "+1");
  LCHECK_EQ(play(Alert::BUZZER_BEATER, AlertSide::TEAM, 3).points, "+3");
  // No points: a turnover, a card, a lead change
  s = play(Alert::INTERCEPTION, AlertSide::OPP, 0, false, false);
  LCHECK_EQ(s.text, "PHI\nINTER-CEPTION");
  LCHECK_EQ(s.text.substr(s.lead), "INTER-CEPTION");
  LCHECK(s.abbr == "PHI" && s.points.empty());
  s = play(Alert::FUMBLE, AlertSide::TEAM, 0);
  LCHECK(s.abbr == "DAL" && s.points.empty() && s.lead == 0);
  s = play(Alert::RED_CARD, AlertSide::TEAM, 0);  // the card names the side, ours too
  LCHECK_EQ(s.text.substr(s.lead), "RED CARD");
  LCHECK(s.abbr == "DAL" && s.points.empty() && s.color == 0xCC0000);
  LCHECK(play(Alert::LEAD_CHANGE, AlertSide::TEAM, 2).points.empty());
  // The whole game's: both logos, no points
  for (Alert a : {Alert::OVERTIME, Alert::FINAL}) {
    s = play(a, AlertSide::GAME, 0);
    LCHECK(s.both && s.abbr.empty() && s.points.empty() && s.lead == 0);
  }
  // Neutral (the live modes): both sides named, both with points
  s = play(Alert::HOME_RUN, AlertSide::TEAM, 3, true);
  LCHECK_EQ(s.text, "DAL HOME RUN");
  LCHECK(s.abbr == "DAL" && s.lead == 4 && s.points == "+3 RUNS");
  // More than any score is: none
  LCHECK(splash_points(Alert::TOUCHDOWN, 100).empty() && splash_points(Alert::TOUCHDOWN, 0).empty() &&
         splash_points(Alert::TOUCHDOWN, -6).empty() && splash_points(Alert::PLAYER, 6).empty());
  // The win: the winner's logo, "WINS!"
  GameSnapshot a = g, b = g;
  a.valid = b.valid = true;
  a.event_id = b.event_id = "1";
  a.state = GameState::IN;
  b.state = GameState::POST;
  b.completed = true;
  b.sport = a.sport = Sport::FOOTBALL;
  b.team_score = 20;
  b.opp_score = 27;
  s = decide_splash(a, b, true, true);
  LCHECK_EQ(s.text, "PHI WINS!");
  LCHECK(s.abbr == "PHI" && s.points.empty());
  LCHECK_EQ(s.text.substr(s.lead), "WINS!");
  b.team_score = 30;
  s = decide_splash(a, b, true, false);
  LCHECK_EQ(s.text.substr(s.lead), "WINS!");
  LCHECK_EQ(s.abbr, "DAL");
  // A favorite player's: their team's logo and the points
  FavPlayer list[kPlayerSlots]{};
  player_put(list, make_player((uint8_t) League::NFL, 1, 2, "ALLEN"));
  PlayerScore ps;
  ps.slot = 0;
  ps.word = "TOUCHDOWN";
  ps.side = AlertSide::OPP;
  ps.type = Alert::TOUCHDOWN;
  ps.points = 7;
  s = player_splash(ps, g, list, true);
  LCHECK_EQ(s.text, "ALLEN TOUCHDOWN");
  LCHECK(s.abbr == "PHI" && s.lead == 0 && s.points == "+7");
  ps.type = Alert::LEAD_CHANGE;
  ps.points = 2;
  LCHECK(player_splash(ps, g, list, true).points.empty());
}

// Every big play, side and abbreviation length, and the win, on every board.
void test_splash_layouts() {
  GameSnapshot g;
  g.team_color = "002a5c";
  g.opp_color = "004c54";
  int shown = 0;
  for (int t = 1; t <= (int) Alert::SCORE; t++)
    for (AlertSide side : {AlertSide::TEAM, AlertSide::OPP, AlertSide::GAME})
      for (const char *abbr : {"NY", "DAL", "UTAH", "UMASS"})
        for (bool neutral : {false, true})
          for (int points : {1, 2, 3, 4, 6, 8, 12, 99})
            for (const Screen &sc : kScreens) {
              const bool wide = sc.w >= 128;  // wide_() and a2_wide_()
              g.team_abbr = g.opp_abbr = abbr;
              const Splash sp = big_play_splash(BigPlay{(Alert) t, side, points}, g, neutral, wide);
              if (sp.text.empty())
                continue;
              bool ok = true;
              check_splash(sp, sc, ok);
              LCHECK(ok);
              shown++;
            }
  LCHECK(shown > 1000);
  // The win, for every abbreviation length
  for (const char *abbr : {"NY", "DAL", "UTAH", "UMASS"})
    for (const Screen &sc : kScreens) {
      const std::string a = abbr;
      const Splash sp{a + " WINS!", 0x002a5c, a, false, (uint8_t) (a.size() + 1), ""};
      bool ok = true;
      check_splash(sp, sc, ok);
      LCHECK(ok);
    }
  // Golf's and racing's winners have no logo: the words alone, as before.
  bool ok = true;
  LCHECK(!check_splash(Splash{"SCHEFFLER WINS!", 0xD4AF37, "", false, 0, ""}, kScreens[0], ok).logos && ok);
}

// Every favorite's word with every name length and shape the panel keeps.
void test_player_layouts() {
  const char *words[] = {"TOUCHDOWN", "FIELD GOAL", "SAFETY", "SCORE", "RBI", "4 RBI", "HOME RUN",
                         "GRAND SLAM", "WALK-OFF", "GOAL", "PENALTY GOAL", "LEAD CHANGE", "BUZZER BEATER"};
  GameSnapshot g;
  g.team_abbr = "BUF";
  g.team_color = "00338d";
  for (const char *word : words)
    for (size_t n = 1; n <= (size_t) kPlayerNameMax; n++)
      for (const char *shape : {"A", "A-", "A ", "A."})
        for (const Screen &sc : kScreens) {
          std::string name(n, shape[0]);
          if (n >= 3 && shape[1] != 0)
            name[n / 2] = shape[1];
          FavPlayer list[kPlayerSlots]{};
          player_put(list, make_player((uint8_t) League::NFL, 1, 2, clean_player_name(name)));
          PlayerScore ps;
          ps.slot = 0;
          ps.word = word;
          ps.type = Alert::TOUCHDOWN;
          ps.points = 7;
          const Splash sp = player_splash(ps, g, list, sc.w >= 128);
          bool ok = true;
          check_splash(sp, sc, ok);
          LCHECK(ok);
        }
}

// Where the parts go, as the owner sees them.
void test_splash_places() {
  GameSnapshot g;
  g.team_abbr = "DAL";
  g.opp_abbr = "PHI";
  const Splash td = big_play_splash(BigPlay{Alert::TOUCHDOWN, AlertSide::TEAM, 6}, g, false, false);
  const Splash td_wide = big_play_splash(BigPlay{Alert::TOUCHDOWN, AlertSide::TEAM, 6}, g, false, true);
  const Splash hr = big_play_splash(BigPlay{Alert::HOME_RUN, AlertSide::OPP, 2}, g, false, true);
  bool ok = true;
  // One panel: the logo at the left with "+6" beside it in spleen_24, over
  // TOUCHDOWN! on a line of spleen_12.
  SplashLayout L = check_splash(td, Screen{64, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 0 && L.points && L.points_font == BoardFont::S24 && L.pts.x == kSplashTile);
  LCHECK(L.words_font == BoardFont::S12 && L.lines == 1);
  // A home run's runs don't fit beside it: the number alone.
  L = check_splash(big_play_splash(BigPlay{Alert::HOME_RUN, AlertSide::OPP, 2}, g, false, false), Screen{64, 64}, ok);
  LCHECK(ok && L.points && !L.points_whole && L.points_font == BoardFont::S24);
  // No points: the logo in the middle
  L = check_splash(big_play_splash(BigPlay{Alert::INTERCEPTION, AlertSide::OPP, 0}, g, false, false), Screen{64, 64},
                   ok);
  LCHECK(ok && !L.points && L.tile_x[0] == (64 - kSplashTile) / 2 && L.lines == 2);
  // Both logos side by side on one panel, without the edge
  L = check_splash(big_play_splash(BigPlay{Alert::OVERTIME, AlertSide::GAME, 0}, g, false, false), Screen{64, 64},
                   ok);
  LCHECK(ok && L.tiles == 2 && L.tile_px == 32 && L.tile_x[0] == 0 && L.tile_x[1] == 32);
  // 128 wide: the logo and "+6" in spleen_32 side by side in the middle
  L = check_splash(td_wide, Screen{128, 64}, ok);
  LCHECK(ok && L.points_font == BoardFont::S32 && L.points_whole && L.words_font == BoardFont::S16 && L.lines == 1);
  LCHECK(L.tile_x[0] + kSplashTile + esphome::gameday::kSplashGap == L.pts.x && L.tile_x[0] * 2 + kSplashTile + 6 + 32 == 128);
  // "+2 RUNS" whole in spleen_24 beside the logo
  L = check_splash(hr, Screen{128, 64}, ok);
  LCHECK(ok && L.points_whole && L.points_font == BoardFont::S24);
  // Three and four wide: the logo at the left, the points at the right
  L = check_splash(td_wide, Screen{192, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 0 && L.pts.x == 192 - kSplashTile && L.points_font == BoardFont::S32);
  LCHECK(L.words.x == kSplashTile && L.words.w == 192 - 2 * kSplashTile);
  L = check_splash(td_wide, Screen{256, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 15 && L.pts.x == 192 && L.words.x == 64 && L.words.w == 128);
  // Two rows of two: down the middle, "+2 RUNS" whole in spleen_32 under the words
  L = check_splash(hr, Screen{128, 128}, ok);
  LCHECK(ok && L.points_whole && L.points_font == BoardFont::S32 && L.pts.y > L.words.y && L.words.y > L.tile_y);
  LCHECK(L.tile_x[0] == (128 - kSplashTile) / 2);
}

// Large text: the ticker in spleen_12, three rows higher, and the rows over
// it moved up to keep clear of it; a leaderboard keeps its own ticker.
void test_large_text() {
  using esphome::gameday::game_geom;
  using esphome::gameday::GameGeom;
  for (int w : {64, 128, 192, 256}) {
    const GameGeom n = game_geom(w), g = game_geom(w, true);
    LCHECK(n.ticker_font == BoardFont::S8 && n.ticker_y == 56);
    LCHECK(g.ticker_font == BoardFont::S12 && g.ticker_y == 53);
    // The ticker's whole line on the board, its descenders too
    LCHECK(g.ticker_y + font_desc_bottom(g.ticker_font) < kBoardRows);
    const int ticker_ink = g.ticker_y + font_ink_top(g.ticker_font);
    if (g.wide) {
      // The situation row's letters, the pips and the bases end above the ticker's.
      LCHECK(g.down.y + font_desc_bottom(BoardFont::S8) < ticker_ink);
      LCHECK(g.pip_y + 3 <= ticker_ink && g.base_y[0] + g.base_px <= ticker_ink);
      LCHECK(g.down.y > g.rec[0].y + font_ink_bottom(BoardFont::S8));  // still under the records
      LCHECK(g.pip_y > g.rec[0].y + font_ink_bottom(BoardFont::S8));
      LCHECK(g.down.y == n.down.y - 2 && g.score[0].y == n.score[0].y);
    } else {
      // The scores, the possession line under them, the bases and the outs
      const int score_ink = g.score[0].y + font_ink_bottom(BoardFont::S12);
      LCHECK(g.score[0].y + g.score_poss_dy > score_ink && g.score[0].y + g.score_poss_dy + 2 <= ticker_ink);
      LCHECK(n.score[0].y + n.score_poss_dy == n.score[0].y + font_rows(BoardFont::S12));  // as before
      LCHECK(g.base_y[0] + g.base_px <= ticker_ink && g.out_y + 2 <= ticker_ink);
      LCHECK(g.score[0].y > g.abbr[0].y + font_ink_bottom(BoardFont::S8));
    }
  }
  // The warning's line covers the bigger ticker on one row of panels.
  LCHECK(esphome::gameday::guard_line_y(kBoardRows, true) <= game_geom(128, true).ticker_y);
  LCHECK(esphome::gameday::guard_line_y(kBoardRows) == 55);
}

// The clock card's time at each size: it fits the board, and nothing under
// it runs into it or into the ticker.
void test_clock_card() {
  using esphome::gameday::clock_card_geom;
  using esphome::gameday::ClockCardGeom;
  using esphome::gameday::game_geom;
  LCHECK_EQ(esphome::gameday::clock_digits("12:45 PM"), "12:45");
  LCHECK_EQ(esphome::gameday::clock_ampm("12:45 PM"), "PM");
  LCHECK_EQ(esphome::gameday::clock_ampm("12:45"), "");
  for (int w : {64, 128, 192, 256})
    for (bool large : {false, true})
      for (bool large_text : {false, true}) {
        const ClockCardGeom c = clock_card_geom(w, large);
        const bool wide = w >= 128;
        // The widest time, as its label shows it
        const std::string time = c.ampm ? "12:45" : "12:45 PM";
        LCHECK((int) time.size() * font_px(c.time_font) <= w);
        LCHECK(c.time_y + font_ink_top(c.time_font) >= 0);
        int bottom = c.time_y + font_ink_bottom(c.time_font);
        if (c.ampm) {
          LCHECK(c.ampm_y + font_ink_top(BoardFont::S8) > bottom);
          bottom = c.ampm_y + font_desc_bottom(BoardFont::S8);
        }
        LCHECK(c.sub_y + font_ink_top(c.sub_font) > bottom);
        // Two lines under it on a wide board (the next game and its start), the date on one panel
        const int sub_end = c.sub_y + (wide ? font_rows(c.sub_font) : 0) + font_desc_bottom(c.sub_font);
        const auto g = game_geom(w, large_text);
        LCHECK(sub_end < g.ticker_y + font_ink_top(g.ticker_font));
        LCHECK(large == (c.time_font != BoardFont::S16));
        if (!large) {
          // Normal is the card as it has always been.
          LCHECK(c.time_y == g.clock_time_y && c.sub_y == g.clock_sub_y && c.sub_font == g.sub_font && !c.ampm);
        }
      }
  LCHECK(clock_card_geom(64, true).ampm && !clock_card_geom(128, true).ampm);
  LCHECK(clock_card_geom(192, true).time_font == BoardFont::S32);
}

// The second area's clock at each size: the time, the AM or PM, the date and
// the ticker one under the other, the ticker's letters above the warning's
// line, and the time inside the area's width.
void test_lower_sizes() {
  using esphome::gameday::guard_rows;
  using esphome::gameday::LowerHalf;
  using esphome::gameday::lower_half;
  using esphome::gameday::Sizes;
  for (int w : {64, 128})
    for (bool clock : {false, true})
      for (bool text : {false, true})
        for (bool card : {false, true})
          for (bool card_date : {false, true}) {
            const LowerHalf l = lower_half(card, card_date, w, Sizes{clock, text});
            int bottom = -1;
            if (l.time) {
              const std::string time = l.ampm ? "12:45" : "12:45 PM";
              LCHECK((int) time.size() * font_px(l.time_font) <= w);
              LCHECK(l.time_y + font_ink_top(l.time_font) >= 0);
              LCHECK(clock == (l.time_font == BoardFont::S24));
              bottom = l.time_y + font_ink_bottom(l.time_font);
            }
            if (l.ampm) {
              LCHECK(l.time && l.ampm_y + font_ink_top(BoardFont::S8) > bottom);
              bottom = l.ampm_y + font_desc_bottom(BoardFont::S8);
            }
            if (l.date) {
              LCHECK(l.date_y + font_ink_top(BoardFont::S12) > bottom);
              LCHECK(w < 128 || (int) std::string("Wed, Sep 30").size() * font_px(BoardFont::S12) <= w);
              bottom = l.date_y + font_desc_bottom(BoardFont::S12);
            }
            // The ticker is spleen_16 at both text sizes, the biggest with every letter.
            LCHECK(l.ticker_y + font_ink_top(BoardFont::S16) > bottom);
            // Its letters, descenders too, end above the warning's line at this text size.
            LCHECK(l.ticker_y + font_desc_bottom(BoardFont::S16) < kBoardRows - guard_rows(text));
            // Under the clock card no time, and the date only when the card has the next game.
            if (card) {
              LCHECK(!l.time && l.date == !card_date);
            } else {
              LCHECK(l.time && l.date);
            }
          }
  // Normal on both is the layout as before.
  const LowerHalf n = lower_half(false, false, 64, Sizes{});
  LCHECK(n.time_font == BoardFont::S16 && n.time_y == 5 && n.date_y == 23 && n.ticker_y == 39 && !n.ampm);
  LCHECK(lower_half(false, false, 128, Sizes{false, true}).ticker_y == 37);
}

}  // namespace

int run_layout_tests(int &total_checks) {
  test_wrap_lines();
  test_splash_sides();
  test_splash_layouts();
  test_player_layouts();
  test_splash_places();
  test_large_text();
  test_clock_card();
  test_lower_sizes();
  total_checks += checks;
  return failures;
}
