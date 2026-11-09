// The parts of the scoreboard page (firmware/pages/gameday-live.yaml) that
// change with the panel arrangement: the board's width, where a splash goes,
// what a 2x2's bottom half shows and where the heat and power warning
// (pages/guard.yaml) goes. The board itself is always 64 rows: the whole
// screen of one row of panels (64, 128, 192 or 256 wide), the top half of a
// 2x2 (128x128). Pure, with no ESPHome or LVGL includes, so the host tests
// compile it directly.
#pragma once

#include <cstddef>

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

// The board's own ticker, along its bottom 8 rows, shows on one row of
// panels. A 2x2 has the ticker once, bigger, in its bottom half.
inline bool board_ticker(int h) { return h <= kBoardRows; }

// A 2x2's bottom half (gd_lower, 64 rows under the board): the time
// (spleen_16, 16 rows from row 5), the date (spleen_12) and the ticker
// (spleen_16). Under the clock card, which has the time already, the time
// stays hidden; the date shows only when the card has the next game under
// its time rather than the date, so neither is up twice. card_date: the
// clock card shows the date. The ticker always ends above the heat and power
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

}  // namespace esphome::gameday
