#include <algorithm>
#include <cstdio>
#include <cstring>
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
// Second areas match one and two panel sizes.
const Screen kScreens[] = {{64, 64}, {128, 64}, {192, 64}, {256, 64}, {128, 128}};

struct Box {
  int x0, y0, x1, y1;  // [x0, x1) x [y0, y1)
  bool empty() const { return x1 <= x0 || y1 <= y0; }
};
bool overlap(const Box &a, const Box &b) {
  return !a.empty() && !b.empty() && a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
bool inside(const Box &a, const Screen &s) { return a.x0 >= 0 && a.y0 >= 0 && a.x1 <= s.w && a.y1 <= s.h; }

// Ink rows of capitals and digits, longest line wide.
Box text_box(int x, int y, int w, BoardFont f, const std::vector<std::string> &lines) {
  size_t widest = 0;
  for (const auto &l : lines)
    widest = std::max(widest, l.size());
  const int px = (int) widest * font_px(f);
  const int left = x + (w - px) / 2;
  return Box{left, y + font_ink_top(f), left + px, y + ((int) lines.size() - 1) * font_rows(f) + font_ink_bottom(f) + 1};
}

// Clear pixels between two boxes, along the separating side
int clearance(const Box &a, const Box &b) {
  return std::max(std::max(b.x0 - a.x1, a.x0 - b.x1), std::max(b.y0 - a.y1, a.y0 - b.y1));
}

// Each line's ink, every glyph's own rows
std::vector<Box> line_boxes(int x, int y, int w, BoardFont f, const std::vector<std::string> &lines) {
  std::vector<Box> out;
  for (size_t i = 0; i < lines.size(); i++) {
    int top = 99, bottom = -1;
    for (char c : lines[i]) {
      if (c == ' ')
        continue;
      int t, b;
      esphome::gameday::glyph_rows(f, c, t, b);
      top = std::min(top, t);
      bottom = std::max(bottom, b);
    }
    if (bottom < 0)
      continue;
    const int px = (int) lines[i].size() * font_px(f), left = x + (w - px) / 2, row = y + (int) i * font_rows(f);
    out.push_back(Box{left, row + top, left + px, row + bottom + 1});
  }
  return out;
}

// Mirrors board_view.cpp: words without the side's name.
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
  // Words wrap as LVGL does, every word whole.
  const size_t chars = (size_t) (L.words.w / font_px(L.words_font));
  const std::vector<std::string> lines = splash_test::layout(words, chars);
  if (lines.empty() || lines.size() != L.lines || wrap_lines(words, chars) != L.lines)
    fail("the words' lines");
  const Box wb = text_box(L.words.x, L.words.y, L.words.w, L.words_font, lines);
  // Last line's descenders must stay on the screen too.
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
  // Points: whole or just the number, centered.
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
  // Every line of words 2 px clear of logos and points
  const std::vector<Box> ink = line_boxes(L.words.x, L.words.y, L.words.w, L.words_font, lines);
  for (size_t i = 1; i < boxes.size(); i++) {
    for (const Box &b : ink)
      if (clearance(b, boxes[i]) < esphome::gameday::kWordsGap)
        fail("words within 2 px of a logo or the points");
    if (i + 1 == boxes.size() && L.points && L.tiles == 1 && clearance(boxes[1], boxes[i]) < 2)
      fail("the points within 2 px of the logo");
  }
  // Fireworks on screen, in the canvas, clear of words
  const Box fx{L.fx.x, L.fx.y, L.fx.x + L.fx.w, L.fx.y + L.fx_h};
  if (fx.empty() || !inside(fx, sc) || L.fx.w > esphome::gameday::kFxPx || L.fx_h > kBoardRows)
    fail("the fireworks off the screen or bigger than their canvas");
  bool behind = false;
  for (const Box &b : ink)
    behind = behind || clearance(b, fx) < esphome::gameday::kWordsGap;
  if (behind && L.fx_h != kBoardRows)
    fail("fireworks touching the words");
  if (!behind && L.fx_h < esphome::gameday::kFxMinRows)
    fail("fireworks too short to show");
  // Points over fireworks get a black box
  if (L.points && !behind) {
    const Box &pb = boxes.back();
    const Box mask{L.pts.x, L.pts.y, L.pts.x + L.pts.w, L.pts.y + font_rows(L.points_font)};
    const int gap = esphome::gameday::kWordsGap;
    if (clearance(pb, fx) < gap && !L.pts_mask)
      fail("fireworks touching the points");
    if (L.pts_mask && (mask.x0 > pb.x0 - gap || mask.y0 > pb.y0 - gap || mask.x1 < pb.x1 + gap || mask.y1 < pb.y1 + gap))
      fail("the points' box within 2 px of them");
    for (size_t i = 0; L.pts_mask && i + 1 < boxes.size(); i++)
      if (overlap(mask, boxes[i]))
        fail("the points' box over the words or a logo");
    for (const Box &b : ink)
      if (L.pts_mask && overlap(mask, b))
        fail("the points' box over a line of words");
  }
  return L;
}

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
  // A plain run's words already count them: number only.
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 2).points, "+2 RUNS");
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 1).points, "+1 RUN");
  LCHECK_EQ(play(Alert::GRAND_SLAM, AlertSide::OPP, 4).points, "+4 RUNS");
  LCHECK_EQ(play(Alert::WALK_OFF, AlertSide::TEAM, 1).points, "+1 RUN");
  // A home run's own runs, not earlier ones this read.
  g.play_points = 2;
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 3).points, "+2 RUNS");
  LCHECK_EQ(play(Alert::GRAND_SLAM, AlertSide::TEAM, 5).points, "+4 RUNS");
  LCHECK_EQ(play(Alert::WALK_OFF, AlertSide::TEAM, 3).points, "+3 RUNS");  // the walk-off is the inning's
  LCHECK_EQ(play(Alert::RUN, AlertSide::TEAM, 3).points, "+3");            // "3 RUNS!": the score's
  g.play_points = 3;  // more than the side gained: the gain
  LCHECK_EQ(play(Alert::HOME_RUN, AlertSide::TEAM, 2).points, "+2 RUNS");
  g.play_points = 0;
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
  // Golf and racing winners have no logo: words alone.
  bool ok = true;
  LCHECK(!check_splash(Splash{"SCHEFFLER WINS!", 0xD4AF37, "", false, 0, ""}, kScreens[0], ok).logos && ok);
}

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

void test_splash_places() {
  GameSnapshot g;
  g.team_abbr = "DAL";
  g.opp_abbr = "PHI";
  const Splash td = big_play_splash(BigPlay{Alert::TOUCHDOWN, AlertSide::TEAM, 6}, g, false, false);
  const Splash td_wide = big_play_splash(BigPlay{Alert::TOUCHDOWN, AlertSide::TEAM, 6}, g, false, true);
  const Splash hr = big_play_splash(BigPlay{Alert::HOME_RUN, AlertSide::OPP, 2}, g, false, true);
  bool ok = true;
  // One panel: logo left, "+6" beside it, words under.
  SplashLayout L = check_splash(td, Screen{64, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 0 && L.points && L.points_font == BoardFont::S24 && L.pts.x == kSplashTile);
  LCHECK(L.words_font == BoardFont::S12 && L.lines == 1);
  // "+2 RUNS" doesn't fit beside it: number alone.
  L = check_splash(big_play_splash(BigPlay{Alert::HOME_RUN, AlertSide::OPP, 2}, g, false, false), Screen{64, 64}, ok);
  LCHECK(ok && L.points && !L.points_whole && L.points_font == BoardFont::S24);
  // No points: the logo in the middle
  L = check_splash(big_play_splash(BigPlay{Alert::INTERCEPTION, AlertSide::OPP, 0}, g, false, false), Screen{64, 64},
                   ok);
  LCHECK(ok && !L.points && L.tile_x[0] == (64 - kSplashTile) / 2 && L.lines == 2);
  // Both logos side by side, without the edge.
  L = check_splash(big_play_splash(BigPlay{Alert::OVERTIME, AlertSide::GAME, 0}, g, false, false), Screen{64, 64},
                   ok);
  LCHECK(ok && L.tiles == 2 && L.tile_px == 32 && L.tile_x[0] == 0 && L.tile_x[1] == 32);
  // 128 wide: logo and "+6" side by side, centered.
  L = check_splash(td_wide, Screen{128, 64}, ok);
  LCHECK(ok && L.points_font == BoardFont::S32 && L.points_whole && L.words_font == BoardFont::S16 && L.lines == 1);
  LCHECK(L.tile_x[0] + kSplashTile + esphome::gameday::kSplashGap == L.pts.x + esphome::gameday::kWordsGap &&
         L.tile_x[0] * 2 + kSplashTile + 6 + 32 == 128);
  // Its points on black, clear of the bursts
  LCHECK(L.pts_mask && L.pts.w == 32 + 2 * esphome::gameday::kWordsGap);
  // "+2 RUNS" whole in spleen_24 beside the logo
  L = check_splash(hr, Screen{128, 64}, ok);
  LCHECK(ok && L.points_whole && L.points_font == BoardFont::S24);
  // Three and four wide: logo left, points right.
  L = check_splash(td_wide, Screen{192, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 0 && L.pts.x == 192 - kSplashTile && L.points_font == BoardFont::S32);
  LCHECK(L.words.x == kSplashTile && L.words.w == 192 - 2 * kSplashTile);
  L = check_splash(td_wide, Screen{256, 64}, ok);
  LCHECK(ok && L.tile_x[0] == 15 && L.pts.x == 192 && L.words.x == 64 && L.words.w == 128);
  // Two rows of two: stacked, points under the words.
  L = check_splash(hr, Screen{128, 128}, ok);
  LCHECK(ok && L.points_whole && L.points_font == BoardFont::S32 && L.pts.y > L.words.y && L.words.y > L.tile_y);
  LCHECK(L.tile_x[0] == (128 - kSplashTile) / 2);
  // 128 wide: TOUCHDOWN 2 px under the tile
  using esphome::gameday::kWordsGap;
  L = check_splash(td_wide, Screen{128, 64}, ok);
  LCHECK(ok && L.words.y + font_ink_top(L.words_font) == L.tile_y + kSplashTile + kWordsGap);
  // A long name with TOUCHDOWN: two smaller lines
  const Splash name = Splash{"ST. BROWN TOUCHDOWN JR", 0x0076B6, "DET", false, 0, "+6"};
  L = check_splash(name, Screen{128, 64}, ok);
  LCHECK(ok && L.lines == 2 && L.words_font == BoardFont::S12);
  // Fireworks beside the logo, above the words
  for (const Screen &sc : kScreens) {
    L = check_splash(td_wide, sc, ok);
    const int ink = L.words.y + font_ink_top(L.words_font);
    const int row_w = sc.w > 128 && sc.h == kBoardRows ? (int) L.words.w - 2 * kWordsGap : sc.w;
    LCHECK(ok && L.fx.w == std::min(row_w, 128) && L.fx.y + L.fx_h == ink - kWordsGap);
    // A points box only 64 or 128 wide
    LCHECK(L.pts_mask == (sc.w <= 128 && sc.h == kBoardRows));
    if (sc.w <= 128)
      LCHECK(L.fx.x == 0 && L.fx.y == 0);  // whole canvas on the screen
  }
  // Two rows of two: the free rows above the band too
  L = check_splash(td_wide, Screen{128, 128}, ok);
  LCHECK(L.fx.y == 0 && L.fx_h > 32 && L.fx_h <= kBoardRows);
}

void test_clock_page() {
  using esphome::gameday::clock_page_fonts;
  using esphome::gameday::ClockPageFonts;
  for (int w : {64, 128, 192, 256})
    for (int h : {64, 128}) {
      const ClockPageFonts n = clock_page_fonts(w, h, false), l = clock_page_fonts(w, h, true);
      LCHECK(n.time == BoardFont::S8 && n.date == BoardFont::S8);  // the theme's own font
      // "09:41 PM" and "Mon, Nov 09" fit the width
      LCHECK(8 * font_px(l.time) <= w && 11 * font_px(l.date) <= w);
      LCHECK(l.date <= BoardFont::S16);  // full glyphs for the day's letters
      LCHECK(font_rows(l.time) + 6 + font_rows(l.date) <= h);
      LCHECK(l.time == (w >= 128 ? BoardFont::S32 : BoardFont::S16));
      LCHECK(l.date == (w >= 128 ? BoardFont::S16 : BoardFont::S8));
    }
}

void test_large_text() {
  using esphome::gameday::game_geom;
  using esphome::gameday::GameGeom;
  for (int w : {64, 128, 192, 256}) {
    const GameGeom n = game_geom(w), g = game_geom(w, true);
    LCHECK(n.ticker_font == BoardFont::S8 && n.ticker_y == 56);
    LCHECK(g.ticker_font == BoardFont::S12 && g.ticker_y == 52);
    // Descenders of y, p and q stay on the board.
    LCHECK(g.ticker_y + font_desc_bottom(g.ticker_font) < kBoardRows);
    LCHECK(n.ticker_y + font_desc_bottom(n.ticker_font) < kBoardRows);
    const int ticker_ink = g.ticker_y + font_ink_top(g.ticker_font);
    if (g.wide) {
      // "|" and "/" ink the ticker's first row.
      LCHECK(g.down.y + font_desc_bottom(BoardFont::S8) < ticker_ink);
      for (const GameGeom &x : {n, g})
        LCHECK(x.pip_y + 3 <= x.ticker_y && x.base_y[0] + x.base_px <= x.ticker_y);
      LCHECK(g.down.y > g.rec[0].y + font_ink_bottom(BoardFont::S8));  // still under the records
      LCHECK(g.pip_y > g.rec[0].y + font_ink_bottom(BoardFont::S8));
      LCHECK(g.count.y == g.down.y && g.outs.y == g.down.y);
      LCHECK(g.down.y == n.down.y - 3 && g.pip_y == n.pip_y - 2 && g.score[0].y == n.score[0].y);
      LCHECK(g.base_y[1] == n.base_y[1] - 3 && g.base_y[1] > g.clock.y + font_ink_bottom(BoardFont::S12));
    } else {
      // Scores, possession line, bases and outs don't collide.
      const int score_ink = g.score[0].y + font_ink_bottom(BoardFont::S12);
      LCHECK(g.score[0].y + g.score_poss_dy > score_ink);
      LCHECK(n.score[0].y + n.score_poss_dy == n.score[0].y + font_rows(BoardFont::S12));  // Normal: right under the score's rows
      // All of them end above the ticker's line.
      for (const GameGeom &x : {n, g}) {
        LCHECK(x.score[0].y + x.score_poss_dy + 2 <= x.ticker_y);
        LCHECK(x.base_y[0] + x.base_px <= x.ticker_y && x.out_y + 2 <= x.ticker_y);
        LCHECK(x.base_y[0] + x.base_px < x.out_y);
      }
      LCHECK(g.score[0].y > g.abbr[0].y + font_ink_bottom(BoardFont::S8));
    }
  }
  // The warning line covers the larger ticker, words centered.
  using esphome::gameday::guard_label_y;
  using esphome::gameday::guard_rows;
  LCHECK(esphome::gameday::guard_line_y(kBoardRows, true) <= game_geom(128, true).ticker_y);
  LCHECK(esphome::gameday::guard_line_y(kBoardRows) == 55);
  LCHECK(guard_label_y(false) == 0);
  for (bool large : {false, true}) {
    const int above = guard_label_y(large) + font_ink_top(BoardFont::S8);
    const int below = guard_rows(large) - 1 - (guard_label_y(large) + font_ink_bottom(BoardFont::S8));
    LCHECK(below - above >= 0 && below - above <= 1);
  }
}

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
        // Wide: next game and start under it; narrow: the date.
        const int sub_end = c.sub_y + (wide ? font_rows(c.sub_font) : 0) + font_desc_bottom(c.sub_font);
        const auto g = game_geom(w, large_text);
        LCHECK(sub_end < g.ticker_y + font_ink_top(g.ticker_font));
        LCHECK(large == (c.time_font != BoardFont::S16));
        if (!large) {
          // Normal matches game_geom's clock card.
          LCHECK(c.time_y == g.clock_time_y && c.sub_y == g.clock_sub_y && c.sub_font == g.sub_font && !c.ampm);
        }
      }
  LCHECK(clock_card_geom(64, true).ampm && !clock_card_geom(128, true).ampm);
  LCHECK(clock_card_geom(192, true).time_font == BoardFont::S32);
}

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
            // spleen_16 at both sizes: the largest with every letter.
            LCHECK(l.ticker_y + font_ink_top(BoardFont::S16) > bottom);
            // Descenders end above the warning line at this size.
            LCHECK(l.ticker_y + font_desc_bottom(BoardFont::S16) < kBoardRows - guard_rows(text));
            // Clock card: no time; date only beside the next game.
            if (card) {
              LCHECK(!l.time && l.date == !card_date);
            } else {
              LCHECK(l.time && l.date);
            }
          }
  const LowerHalf n = lower_half(false, false, 64, Sizes{});
  LCHECK(n.time_font == BoardFont::S16 && n.time_y == 5 && n.date_y == 23 && n.ticker_y == 39 && !n.ampm);
  LCHECK(lower_half(false, false, 128, Sizes{false, true}).ticker_y == 36);
}

// ---- the game clock at Clock size Large ---------------------------------------

using esphome::gameday::game_clock;
using esphome::gameday::game_geom;
using esphome::gameday::GameGeom;
using esphome::gameday::glyph_rows;
using esphome::gameday::record_fit;
using esphome::gameday::record_label_x;
using esphome::gameday::Spot;

// Ink rows measured from the Spleen bitmaps, in hex.
const char kRowChars[] = "0123456789:'+./-ADEFHILMNOPRSTUabdefhilmnoprstuy&#";
struct MeasuredRows {
  BoardFont font;
  const char *top, *bottom;
};
const MeasuredRows kMeasured[] = {
    {BoardFont::S8, "11111111113026041111111111111112002001022222202201",
     "66666666666266746666666666666666666666666676666766"},
    {BoardFont::S12, "11111111114138051111111111111113113111133333313312",
     "888888888883789588888888888888888888888888b8888b88"},
    {BoardFont::S16, "2222222222515a172222222222222225225222255555525522",
     "bbbbbbbbbbb49bc7bbbbbbbbbbbbbbbbbbbbbbbbbbebbbbebb"},
};

// The parsers' clock texts, the longest ones included.
const char *const kClockTexts[] = {
    "15:00 1st", "12:04 2nd", "5:21 3rd", "0:07 4th", "45.3 4th", "10:00 OT", "2:10 OT",  "8:12 2OT",
    "20:00 1st", "Halftime",  "End 3rd",  "End of 1st", "End of 2nd", "SO",  "Delayed",  "Final/OT",
    "Final/SO",  "67'",       "90'+5'",   "HT",       "ET HT",    "ET 105'",  "PENS 3-2", "PENS 10-9",
    "in 45m",    "in 2h",     "in 2h 5m", "in 23h 59m", "In Progress"};
const char *const kInnings[] = {"1st", "5th", "9th", "10th", "12th"};
const char *const kHalves[] = {"Top ", "Mid ", "Bot ", "End "};
const char *const kBaseballBreaks[] = {"Delayed", "Rain Delay", "Suspended"};

// Records; eight characters overflow the row
const char *const kRecords[][2] = {{"", ""},           {"2-1", "1-2"},         {"15-3", "12-6"},
                                   {"45-37", "22-10"}, {"10-5-2", "8-9"},      {"8-9", "10-3-1"},
                                   {"30-15-5", "28-20-3"}, {"1stNFCE", "9-8"}, {"41-30-11", "38-31-13"}};

struct ClockScreen {
  int w;
  const char *name;
  bool area;  // a second area: no pips, diamond or corner icon
};
const ClockScreen kClockScreens[] = {{64, "one panel", false},     {128, "two wide", false},
                                     {192, "three wide", false},   {256, "four wide", false},
                                     {128, "2x2 top half", false}, {128, "2x2 second area", true},
                                     {64, "three wide's second area", true}};

Box spot_box(const Spot &s, int y0, int y1) { return Box{s.x, s.y + y0, s.x + s.w, s.y + y1 + 1}; }

// A centered label's text, clipped to the label
Box text_in(const Spot &s, int chars, int px, int y0, int y1) {
  const int tw = chars * px, left = s.x + (s.w - tw) / 2;
  return Box{std::max<int>(left, s.x), s.y + y0, std::min<int>(left + tw, s.x + s.w), s.y + y1 + 1};
}

// Records as drawn: the rank only where it fits
std::vector<Box> record_parts(const GameGeom &g, int clock_px, const char *const rec[2], uint8_t rank) {
  std::vector<Box> b;
  const int px = font_px(BoardFont::S8);
  for (int i = 0; i < 2 && g.wide; i++) {
    const std::string shown = rank_label(rank, rec[i], record_fit(g, clock_px));
    if (shown.empty())
      continue;
    Spot at = g.rec[i];
    at.x = (int16_t) record_label_x(g, i, (int) shown.size() * px, clock_px);
    b.push_back(text_in(at, (int) shown.size(), px, 0, font_ink_bottom(BoardFont::S8)));
  }
  return b;
}

std::vector<Box> logo_parts(const GameGeom &g) {
  const int px = esphome::gameday::kLogoPx;
  return {Box{g.logo_x[0], g.logo_y, g.logo_x[0] + px, g.logo_y + px},
          Box{g.logo_x[1], g.logo_y, g.logo_x[1] + px, g.logo_y + px}};
}

// The rest of a live game, but the corner icon
std::vector<Box> live_parts(const GameGeom &g, const ClockScreen &s, bool large_text, bool baseball) {
  std::vector<Box> b;
  const int w = s.w;
  for (int i = 0; i < 2; i++)
    b.push_back(spot_box(g.score[i], font_ink_top(g.score_font), font_ink_bottom(g.score_font)));
  if (g.wide) {
    for (int i = 0; i < 2; i++) {
      b.push_back(spot_box(g.abbr[i], font_ink_top(g.abbr_font), font_ink_bottom(g.abbr_font)));
      b.push_back(Box{g.abbr[i].x, g.abbr[i].y + font_rows(g.abbr_font), g.abbr[i].x + g.abbr[i].w,
                      g.abbr[i].y + font_rows(g.abbr_font) + 2});  // the possession line
    }
    if (baseball && s.area) {
      // "2-1 2 OUT": digits and capitals only
      b.push_back(spot_box(g.down, font_ink_top(BoardFont::S8), font_desc_bottom(BoardFont::S8)));
    } else if (baseball) {
      b.push_back(spot_box(g.count, font_ink_top(BoardFont::S8), font_desc_bottom(BoardFont::S8)));
      b.push_back(spot_box(g.outs, font_ink_top(BoardFont::S8), font_desc_bottom(BoardFont::S8)));
      for (int i = 0; i < 3; i++)
        b.push_back(Box{g.base_x[i], g.base_y[i], g.base_x[i] + g.base_px, g.base_y[i] + g.base_px});
    } else {
      // Down and distance: "&" inks the label's top row.
      b.push_back(spot_box(g.down, 0, font_desc_bottom(BoardFont::S8)));
      for (int i = 0; i < 6 && !s.area; i++)
        b.push_back(Box{g.pip_x[i], g.pip_y, g.pip_x[i] + 5, g.pip_y + 3});
    }
  } else {
    b.push_back(Box{0, g.score[0].y + g.score_poss_dy, w, g.score[0].y + g.score_poss_dy + 2});
    for (int i = 0; i < 3 && baseball && !s.area; i++) {
      b.push_back(Box{g.base_x[i], g.base_y[i], g.base_x[i] + g.base_px, g.base_y[i] + g.base_px});
      b.push_back(Box{g.out_x[i], g.out_y, g.out_x[i] + 2, g.out_y + 2});
    }
  }
  b.push_back(Box{0, g.ticker_y, w, kBoardRows});
  b.push_back(Box{0, esphome::gameday::guard_line_y(kBoardRows, large_text), w, kBoardRows});
  return b;
}

// Each glyph's own ink, so spaces leave gaps.
std::vector<Box> clock_glyphs(BoardFont f, const Spot &s, const std::string &text) {
  std::vector<Box> b;
  const int px = font_px(f), left = s.x + (s.w - (int) text.size() * px) / 2;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == ' ')
      continue;
    int top, bottom;
    glyph_rows(f, text[i], top, bottom);
    b.push_back(Box{left + (int) i * px, s.y + top, left + (int) (i + 1) * px, s.y + bottom + 1});
  }
  return b;
}

bool same_spot(const Spot &a, const Spot &b) { return a.x == b.x && a.y == b.y && a.w == b.w; }

void test_glyph_rows() {
  for (const MeasuredRows &m : kMeasured)
    for (size_t i = 0; kRowChars[i] != 0; i++) {
      int top, bottom;
      glyph_rows(m.font, kRowChars[i], top, bottom);
      const int real_top = std::stoi(std::string(1, m.top[i]), nullptr, 16);
      const int real_bottom = std::stoi(std::string(1, m.bottom[i]), nullptr, 16);
      // Never above the ink top unless it inks there
      const int ink_top = std::min(real_top, font_ink_top(m.font));
      LCHECK(top <= real_top && bottom >= real_bottom && top >= ink_top);
      if (top > real_top || bottom < real_bottom || top < ink_top)
        printf("  '%c' in %dpx: %d-%d, inks %d-%d\n", kRowChars[i], font_rows(m.font), top, bottom, real_top,
               real_bottom);
    }
}

// The pick for one text, checked against everything near it
BoardFont check_clock(const ClockScreen &s, bool large_text, bool large_clock, const std::string &text,
                      bool baseball, const char *const rec[2], uint8_t rank = 0) {
  const GameGeom g = game_geom(s.w, large_text, large_clock);
  const GameGeom n = game_geom(s.w, large_text);
  const size_t rec_chars = g.wide ? std::max(strlen(rec[0]), strlen(rec[1])) : 0;
  const auto pick = game_clock(g, text, rec_chars, baseball);
  bool ok = true;
  auto fail = [&](const char *why) {
    if (ok)
      printf("  \"%s\" on %s (%s text, %s clock): %s\n", text.c_str(), s.name, large_text ? "Large" : "Normal",
             large_clock ? "Large" : "Normal", why);
    ok = false;
  };
  const int clock_px = (int) text.size() * font_px(pick.font);
  const bool big = large_clock && pick.font == g.clock_font;
  if (big && clock_px > pick.spot.w)
    fail("wider than its label");
  if (!large_clock) {
    // Normal is the clock as it always was.
    if (pick.font != n.clock_font || !same_spot(pick.spot, n.clock))
      fail("Normal clock size changed the clock");
    LCHECK(ok);
    return pick.font;
  }
  const std::vector<Box> logos = logo_parts(g), rest = live_parts(g, s, large_text, baseball);
  const std::vector<Box> recs = record_parts(g, clock_px, rec, rank);
  std::vector<Box> parts = rest;
  parts.insert(parts.end(), logos.begin(), logos.end());
  if (!s.area)
    parts.push_back(Box{s.w - 9, 0, s.w, 13});  // the heat or power icon
  // Beside the normal clock the records are as before.
  const size_t row_chars = (size_t) (esphome::gameday::kRecRowPx / font_px(BoardFont::S8));
  const bool recs_fit = g.wide && record_fit(g, clock_px) >= std::min(rec_chars, row_chars);
  if (big && g.wide && !recs_fit)
    fail("a record cut short");
  if (big || recs_fit)
    parts.insert(parts.end(), recs.begin(), recs.end());
  for (const Box &c : clock_glyphs(pick.font, pick.spot, text)) {
    if (!inside(c, Screen{s.w, kBoardRows}))
      fail("off the board");
    for (const Box &p : parts)
      if (overlap(c, p))
        fail("on top of another part");
  }
  // The logos and records moved up: still clear
  for (const Box &m : logos)
    for (const Box &p : rest)
      if (overlap(m, p))
        fail("a logo on another part");
  for (const Box &m : recs) {
    if (!inside(m, Screen{s.w, kBoardRows}))
      fail("a record off the board");
    for (const Box &p : rest)
      if (overlap(m, p))
        fail("a record on another part");
    for (const Box &l : logos)
      if (overlap(m, l))
        fail("a record on a logo");
  }
  LCHECK(ok);
  return pick.font;
}

void test_game_clock() {
  using esphome::gameday::records_clear;
  test_glyph_rows();
  // Normal: the record rows' own rule, centered
  for (int w : {64, 128, 192, 256})
    for (int px = 0; px <= w; px++)
      LCHECK(record_fit(game_geom(w), px) == record_row_chars(px, w));
  for (int w : {128, 192, 256})
    for (bool large_clock : {false, true}) {
      const GameGeom g = game_geom(w, false, large_clock);
      LCHECK(g.rec_slide == large_clock);
      for (int px = 0; px <= w; px += 2)
        for (size_t chars = 0; chars <= 7; chars++)
          LCHECK(records_clear(g, px, chars) == (record_fit(g, px) >= chars));
    }
  // Large: records slide out only as far as they must
  for (int w : {128, 192, 256}) {
    const GameGeom g = game_geom(w, false, true);
    for (int clock_px = 0; clock_px <= w; clock_px += 2)
      for (int chars = 0; chars <= 7; chars++) {
        const int px = chars * font_px(BoardFont::S8), pad = (kRecordRowPx - px) / 2;
        const int left = w / 2 - clock_px / 2, right = left + clock_px;
        const int x0 = record_label_x(g, 0, px, clock_px) + pad, x1 = record_label_x(g, 1, px, clock_px) + pad;
        const int c0 = g.rec[0].x + pad, c1 = g.rec[1].x + pad;
        LCHECK(x0 >= 0 && x1 + px <= w && x0 <= c0 && x1 >= c1);
        if (clock_px == 0 || chars == 0 || !records_clear(g, clock_px, (size_t) chars))
          continue;
        LCHECK(x0 + px < left && x1 > right);
        LCHECK(x0 == c0 || x0 + px == left - 1);
        LCHECK(x1 == c1 || x1 == right + 1);
      }
  }
  for (const ClockScreen &s : kClockScreens)
    for (bool large_text : {false, true})
      for (bool large_clock : {false, true})
        for (const auto &rec : kRecords) {
          for (const char *t : kClockTexts)
            for (uint8_t rank : {0, 5})
              check_clock(s, large_text, large_clock, t, false, rec, rank);
          for (const char *t : kBaseballBreaks)
            check_clock(s, large_text, large_clock, t, true, rec);
          // An inning's halves keep one size.
          for (const char *inning : kInnings) {
            const BoardFont top = check_clock(s, large_text, large_clock, std::string("Top ") + inning, true, rec);
            for (const char *half : kHalves)
              LCHECK(check_clock(s, large_text, large_clock, half + std::string(inning), true, rec) == top);
          }
        }
  // Large moves the logos and records up only.
  for (int w : {128, 192, 256})
    for (bool large_text : {false, true}) {
      const GameGeom n = game_geom(w, large_text), g = game_geom(w, large_text, true);
      LCHECK(n.logo_y == 4 && g.logo_y == 0 && g.rec[0].y == n.rec[0].y - 4 && g.rec[1].y == g.rec[0].y);
      LCHECK(g.abbr[0].y == n.abbr[0].y && g.score[0].y == n.score[0].y && g.down.y == n.down.y);
      LCHECK(g.pip_y == n.pip_y && g.base_y[1] == n.base_y[1] && g.ticker_y == n.ticker_y);
      LCHECK(g.clock_font == BoardFont::S16 && g.clock_fall_font == BoardFont::S12);
    }
  for (bool large_text : {false, true}) {
    const GameGeom n = game_geom(64, large_text), g = game_geom(64, large_text, true);
    LCHECK(g.logo_y == n.logo_y && g.score[0].y == n.score[0].y && g.out_y == n.out_y);
    LCHECK(g.clock_font == BoardFont::S12 && g.clock_fall_font == BoardFont::S8 && same_spot(g.clock_fall, n.clock));
  }
  // The owner's board: Halftime on a 2x2's top half
  const char *const college[2] = {"15-3", "12-6"};
  const char *const none[2] = {"", ""};
  const char *const short_rec[2] = {"2-1", "1-2"};
  for (const ClockScreen &s : kClockScreens)
    for (bool large_text : {false, true}) {
      const bool wide = s.w >= 128;
      const BoardFont big = wide ? BoardFont::S16 : BoardFont::S12, normal = wide ? BoardFont::S12 : BoardFont::S8;
      LCHECK(check_clock(s, large_text, false, "Halftime", false, college) == normal);
      LCHECK(check_clock(s, large_text, true, "Halftime", false, college) == big);
      LCHECK(check_clock(s, large_text, true, "Halftime", false, college, 12) == big);
      LCHECK(check_clock(s, large_text, true, "5:21 3rd", false, college) == big);
      LCHECK(check_clock(s, large_text, true, "PENS 3-2", false, college) == big);
      LCHECK(check_clock(s, large_text, true, "in 2h 5m", false, college) == big);
      // Ten characters fit beside short records.
      for (const char *t : {"12:04 2nd", "End of 2nd", "in 23h 59m"})
        LCHECK(check_clock(s, large_text, true, t, false, college) == big);
      LCHECK(check_clock(s, large_text, true, "12:04 2nd", false, short_rec) == big);
      LCHECK(check_clock(s, large_text, true, "End of 2nd", false, none) == big);
      // Two wide: records slide out, but only so far
      const char *const pro[2] = {"45-37", "22-10"};
      const char *const hockey[2] = {"10-3-1", "8-9-2"};
      LCHECK(check_clock(s, large_text, true, "12:04 2nd", false, pro) == big);
      LCHECK(check_clock(s, large_text, true, "End of 2nd", false, pro) == (s.w == 128 ? normal : big));
      for (const char *t : {"5:21 3rd", "Halftime", "0:45 3rd", "9:59 2nd", "End 3rd"})
        LCHECK(check_clock(s, large_text, true, t, false, hockey) == big);
      LCHECK(check_clock(s, large_text, true, "19:59 1st", false, hockey) == (s.w == 128 ? normal : big));
      // Soccer's minutes clear the logos on one panel too.
      for (const char *t : {"67'", "45'+2'", "90'+5'", "ET 105'", "ET HT"})
        LCHECK(check_clock(s, large_text, true, t, false, college) == big);
      // One panel, Large text: no room under "Top".
      LCHECK(check_clock(s, large_text, true, "Top 5th", true, college) == (!wide && large_text ? normal : big));
      LCHECK(check_clock(s, large_text, true, "Top 10th", true, college) == (!wide && large_text ? normal : big));
      // "/" reaches the logos' row on one panel.
      LCHECK(check_clock(s, large_text, true, "Final/OT", false, college) == (wide ? big : normal));
    }
}

// A standing yields to the large clock, as to the normal one.
void test_clock_standing() {
  using esphome::gameday::clock_standing;
  const char *const standings[] = {"1st NFCE", "10th NLW", "Seed 4", "Seed 12", "2nd AL", "1st"};
  const char *const records[] = {"", "10-6", "10-6-1", "30-15-5"};
  const char *const others[] = {"", "9-8", "45-37", "10-3-1", "28-20-3"};
  for (int w : {64, 128, 192, 256})
    for (bool large_text : {false, true})
      for (bool large_clock : {false, true}) {
        const GameGeom g = game_geom(w, large_text, large_clock);
        for (const char *clock : kClockTexts)
          for (const char *st : standings)
            for (const char *rec : records)
              for (const char *other : others) {
                const std::string got = clock_standing(g, clock, st, rec, strlen(other), false, board_standing);
                const size_t n = strlen(clock);
                if (!large_clock && g.wide)
                  LCHECK_EQ(got, board_standing(st, record_row_chars((int) n * kClockCharPx, w)));
                if (!g.wide)
                  continue;
                // The clock the board then picks
                const size_t mine = (got.empty() ? std::string(rec) : got).size();
                const auto pick = game_clock(g, clock, std::max(mine, strlen(other)), false);
                LCHECK(got.size() <= record_fit(g, (int) n * font_px(pick.font)));
                // Large whenever a squeezed standing allows it
                const std::string sq = board_standing(st, record_fit(g, (int) n * font_px(g.clock_font)));
                const size_t sq_n = (sq.empty() ? std::string(rec) : sq).size();
                if (game_clock(g, clock, std::max(sq_n, strlen(other)), false).font == g.clock_font)
                  LCHECK(pick.font == g.clock_font);
              }
      }
  // Two wide, Large: a place shortens for the big clock
  const GameGeom g = game_geom(128, false, true);
  LCHECK_EQ(clock_standing(g, "5:21 3rd", "1st NFCE", "10-6", 3, false, board_standing), "1st");
  LCHECK_EQ(clock_standing(g, "Halftime", "Seed 12", "10-6", 3, false, board_standing), "Seed12");
  LCHECK_EQ(clock_standing(g, "0:07 4th", "Seed 4", "10-6", 3, false, board_standing), "Seed 4");
  LCHECK(game_clock(g, "5:21 3rd", 6, false).font == BoardFont::S16);
  // A long other record keeps the normal clock's room.
  LCHECK_EQ(clock_standing(g, "5:21 3rd", "1st NFCE", "10-6", 7, false, board_standing), "1stNFCE");
  // Squeezed away, the record must fit too
  LCHECK_EQ(clock_standing(g, "End of 2nd", "Seed 12", "10-6-1", 3, false, board_standing), "Seed12");
  LCHECK_EQ(clock_standing(g, "End of 2nd", "Seed 12", "10-6", 3, false, board_standing), "");
}

}  // namespace

int run_layout_tests(int &total_checks) {
  test_wrap_lines();
  test_splash_sides();
  test_splash_layouts();
  test_player_layouts();
  test_splash_places();
  test_clock_page();
  test_large_text();
  test_clock_card();
  test_lower_sizes();
  test_game_clock();
  test_clock_standing();
  total_checks += checks;
  return failures;
}
