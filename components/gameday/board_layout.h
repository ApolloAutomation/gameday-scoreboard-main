// The parts of the scoreboard page (firmware/pages/gameday-live.yaml) that
// change with the panel arrangement: the board's width, where each of the
// board's widgets goes on it (board_view.cpp places them), where a splash
// goes, what the second area's clock shows (split.h) and where the heat and
// power warning (pages/guard.yaml) goes. The board itself is always 64
// rows: the whole screen of one row of panels (64, 128, 192 or 256 wide, or
// 128 beside a second area), the top half of a 2x2 (128x128). Pure, with no
// ESPHome or LVGL includes, so the host tests compile it directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome::gameday {

static const int kPanelPx = 64;   // one panel, each way
static const int kBoardRows = 64;  // the board's height on every arrangement

// The board's width for panels panels in a row (a 2x2 is two wide).
inline int board_px(int panels) { return panels > 0 ? panels * kPanelPx : kPanelPx; }

// The characters a splash line holds on a board w px wide: spleen_16 (8 px a
// character) from 128 wide, spleen_12 (6 px) on one panel. 10 on one panel,
// 16 on two, 24 on three, 32 on four. The words are sized for two panels
// (decide_splash's wide), so a wider board fits them in as many lines or
// fewer.
inline size_t splash_chars(int w) { return (size_t) (w >= 128 ? w / 8 : w / 6); }

// A splash (tt_splash) covers the whole screen, a 2x2's bottom half too. Its
// words and the fireworks sit in 64 rows: the board's, or the middle ones of
// a 2x2. The fireworks canvas is 128x64, in the middle of a wider board. The
// words start 16 rows into those 64 on a wide board (three lines of
// spleen_16 end at the bottom) and 18 on one panel (three of spleen_12).
struct SplashBox {
  int height;  // the splash: the screen's height
  int band_y;  // the top of its 64 rows
  int fx_x;    // the fireworks canvas's left edge
  int text_y;  // the words' label
};
static const int kFxPx = 128;  // the fireworks canvas's width (gameday-live.yaml)

inline SplashBox splash_box(int w, int h) {
  SplashBox b;
  b.height = h > kBoardRows ? h : kBoardRows;
  b.band_y = (b.height - kBoardRows) / 2;
  b.fx_x = w > kFxPx ? (w - kFxPx) / 2 : 0;
  b.text_y = b.band_y + (w >= 128 ? 16 : 18);
  return b;
}

// The heat and power warning (pages/guard.yaml): a line kGuardRows high
// along the bottom of a screen h rows high. On one row of panels it covers
// the ticker; on a 2x2 it sits under the bottom half's ticker.
static const int kGuardRows = 9;
inline int guard_line_y(int h) { return h - kGuardRows; }

// The board's own ticker, along its bottom 8 rows, shows when the board is
// the whole screen's height: one row of panels, or the top half of a 2x2
// beside its second area (split.h), which has a ticker of its own.
inline bool board_ticker(int h) { return h <= kBoardRows; }

// The second area's clock (split.h; gd_lower, 64 rows): the time
// (spleen_16, 16 rows from row 5), the date (spleen_12) and its ticker
// (spleen_16). Under the main board's clock card, which has the time
// already, the time stays hidden; the date shows only when the card has the
// next game under its time rather than the date, so neither is up twice.
// card_date: the clock card shows the date. The ticker always ends above the heat and power
// warning's line (guard_line_y), which would cut its letters off; under the
// clock card what shows is centered in the 64 rows.
struct LowerHalf {
  bool time;
  bool date;
  int date_y;
  int ticker_y;
};
static const int kLowerTimeY = 5, kLowerTimeRows = 16, kLowerDateRows = 12, kLowerTickerRows = 16;

inline LowerHalf lower_half(bool clock_card, bool card_date) {
  if (!clock_card)
    return LowerHalf{true, true, 23, kBoardRows - kGuardRows - kLowerTickerRows};
  if (card_date)
    return LowerHalf{false, false, 23, (kBoardRows - kLowerTickerRows) / 2};
  return LowerHalf{false, true, 12, 36};
}

// Where the game layout's widgets go on a board w px wide. From 128 wide
// the logos sit at the far edges with the abbreviations and big scores in
// two columns between them, the records (or a college rank) under the logos
// with the timeout pips below them, and the game clock and the situation
// row (down and distance, a goal, a series, baseball's count, bases and
// outs) in the middle. On one panel the logos sit side by side over the
// abbreviations (the clock in their row while the game is on) and the
// scores, with the bases between the scores and the outs as dots under
// them; a single panel has no room for records, pips or the situation row,
// which stay hidden and keep no place here (zero). A badge takes its logo's
// place. Index 0 is the team's side, on the left; 1 the opponent's.
enum class BoardFont : uint8_t { S8, S12, S16 };  // spleen_8, spleen_12, spleen_16

struct Spot {
  int16_t x, y, w;
};

struct GameGeom {
  bool wide;  // 128 wide and up
  int16_t logo_x[2], logo_y;
  BoardFont abbr_font, score_font, clock_font;
  Spot abbr[2], score[2], clock;
  Spot rec[2], down;      // wide only
  int16_t pip_x[6];       // wide only: the team's three timeouts, then the opponent's
  int16_t pip_y;
  BoardFont msg_font;     // the no-game message (centered both ways)
  BoardFont sub_font;     // the clock card's second line, under its spleen_16 time
  int16_t clock_time_y, clock_sub_y;
  BoardFont splash_font;  // the splash's words, at splash_box's text_y
  int16_t ticker_y;       // the board's own ticker, along its bottom
  // Baseball: first, second and third base as a small diamond (first on the
  // right), each base_px square.
  int16_t base_px;
  int16_t base_x[3], base_y[3];
  int16_t out_x[3], out_y;  // one panel only: the out dots
  Spot count, outs;         // wide only: the count left of the diamond, "2 OUT" right of it
};

inline GameGeom game_geom(int w) {
  GameGeom g{};
  const bool wide = w >= 128;
  const int half = w / 2;  // also the diamond's middle
  g.wide = wide;
  g.ticker_y = kBoardRows - 8;
  g.msg_font = g.sub_font = wide ? BoardFont::S12 : BoardFont::S8;
  g.clock_time_y = wide ? 6 : 14;
  g.clock_sub_y = wide ? 26 : 34;
  g.splash_font = wide ? BoardFont::S16 : BoardFont::S12;
  if (wide) {
    const int colw = half - 36;
    g.logo_x[0] = 2;
    g.logo_x[1] = w - 34;
    g.logo_y = 4;
    g.abbr_font = BoardFont::S12;
    g.score_font = BoardFont::S16;
    g.clock_font = BoardFont::S12;
    g.abbr[0] = {36, 4, (int16_t) colw};
    g.abbr[1] = {(int16_t) half, 4, (int16_t) colw};
    g.score[0] = {36, 18, (int16_t) colw};
    g.score[1] = {(int16_t) half, 18, (int16_t) colw};
    g.clock = {32, 35, (int16_t) (w - 64)};  // the records' row, between them
    g.rec[0] = {0, 38, 36};
    g.rec[1] = {(int16_t) (w - 36), 38, 36};
    g.down = {32, 48, (int16_t) (w - 64)};
    // Centered under each logo, 18 px in from its edge
    for (int i = 0; i < 3; i++) {
      g.pip_x[i] = (int16_t) (18 - 9 + i * 7);
      g.pip_x[3 + i] = (int16_t) (w - 18 - 9 + i * 7);
    }
    g.pip_y = 48;
    g.base_px = 4;
    g.base_x[0] = (int16_t) (half + 3);
    g.base_x[1] = (int16_t) (half - 2);
    g.base_x[2] = (int16_t) (half - 7);
    g.base_y[0] = 51;
    g.base_y[1] = 47;
    g.base_y[2] = 51;
    g.count = {32, 48, (int16_t) (half - 9 - 32)};
    g.outs = {(int16_t) (half + 9), 48, 28};  // "2 OUT" is 25 px in spleen_8
  } else {
    g.logo_x[0] = 0;
    g.logo_x[1] = (int16_t) (w - 32);
    g.logo_y = 0;
    g.abbr_font = BoardFont::S8;
    g.score_font = BoardFont::S12;
    g.clock_font = BoardFont::S8;
    g.abbr[0] = {0, 33, (int16_t) half};
    g.abbr[1] = {(int16_t) half, 33, (int16_t) half};
    g.score[0] = {0, 42, (int16_t) half};
    g.score[1] = {(int16_t) half, 42, (int16_t) half};
    g.clock = {0, 33, (int16_t) w};  // in the abbreviations' row
    g.base_px = 3;
    g.base_x[0] = (int16_t) (half + 3);
    g.base_x[1] = (int16_t) (half - 1);
    g.base_x[2] = (int16_t) (half - 5);
    g.base_y[0] = 47;
    g.base_y[1] = 43;
    g.base_y[2] = 47;
    for (int i = 0; i < 3; i++)
      g.out_x[i] = (int16_t) (half - 5 + i * 4);
    g.out_y = 51;
  }
  return g;
}

// A team color the board can show as text: a dark color vanishes on the
// panel, so its brightest channel is lifted to floor with the others in
// proportion, and black becomes white. The abbreviations lift to 0xC0, a
// fireworks splash's words to 0xE0.
inline uint32_t lift_color(uint32_t c, uint32_t floor) {
  uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  uint32_t m = r > g ? r : g;
  if (b > m)
    m = b;
  if (m == 0)
    return 0xFFFFFF;
  if (m < floor) {
    r = r * floor / m;
    g = g * floor / m;
    b = b * floor / m;
  }
  return (r << 16) | (g << 8) | b;
}

// The no-game screen is up in the game's place: the clock card, a
// leaderboard's card, or no game to show (game_state "NOT_FOUND", "BYE" or
// empty). Its message is a leaderboard card's next_line, the component's
// words (the finals rotation's headers, "No live games") or "No upcoming
// game".
inline bool no_game_screen(const std::string &game_state, bool clock_card, bool board_card) {
  return clock_card || board_card || game_state == "NOT_FOUND" || game_state == "BYE" || game_state.empty();
}

// The clock card's second line is the date, not the next game: on one panel,
// or with no next game known.
inline bool clock_card_date(int w, bool have_next) { return w < 128 || !have_next; }

}  // namespace esphome::gameday
