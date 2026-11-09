// The parts of the scoreboard page (firmware/pages/gameday-live.yaml) that
// change with the panel arrangement: the board's width, where each of the
// board's widgets goes on it (board_view.cpp places them), where a splash
// and its logo go, what the second area's clock shows (split.h) and where
// the heat and power warning (pages/guard.yaml) goes, at the owner's Clock
// size and Text size. The board itself is always 64 rows: the whole screen
// of one row of panels (64, 128, 192 or 256 wide, or 128 beside a second
// area), the top half of a 2x2 (128x128). Pure, with no ESPHome or LVGL
// includes, so the host tests compile it directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace esphome::gameday {

static const int kPanelPx = 64;   // one panel, each way
static const int kBoardRows = 64;  // the board's height on every arrangement

// The panel's fonts, all Spleen and fixed width: spleen_8, spleen_12 and
// spleen_16 (the theme's), which hold every letter panel_text() puts out,
// and spleen_24 and spleen_32, which hold only a Large clock's and a
// splash's points' characters (gameday-common.yaml's font block).
enum class BoardFont : uint8_t { S8, S12, S16, S24, S32 };
// A character's width, a line's height, and the first and last rows of a
// capital's or a digit's ink in its line (from the fonts' own bitmaps), which
// is what the eye sees: a line's top rows and its descenders' are blank in
// capitals.
inline int font_px(BoardFont f) {
  static const int8_t k[] = {5, 6, 8, 12, 16};
  return k[(int) f];
}
inline int font_rows(BoardFont f) {
  static const int8_t k[] = {8, 12, 16, 24, 32};
  return k[(int) f];
}
inline int font_ink_top(BoardFont f) {
  static const int8_t k[] = {1, 1, 2, 4, 6};
  return k[(int) f];
}
inline int font_ink_bottom(BoardFont f) {
  static const int8_t k[] = {6, 8, 11, 18, 25};
  return k[(int) f];
}
// The last row any of its characters inks (a descender's, a comma's): the
// tail of y, p or q (and of ç, ý and ÿ) on spleen_12's last row. spleen_24
// and spleen_32 have no letter with a tail among their few.
inline int font_desc_bottom(BoardFont f) {
  static const int8_t k[] = {7, 11, 14, 18, 25};
  return k[(int) f];
}

// The owner's Clock size and Text size (Normal or Large, set_request.h): the
// clock's digits, and the ticker's letters where a bigger font fits its row.
struct Sizes {
  bool clock{false};  // Large clock
  bool text{false};   // Large text
};

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
// along the bottom of a screen h rows high, kGuardRowsLarge with Large text,
// whose ticker's line is 12 rows (game_geom). On one row of panels it covers
// the ticker; on a 2x2 it sits under the bottom half's ticker. Its words
// (spleen_8) sit in the middle of the line: guard_label_y rows down.
static const int kGuardRows = 9, kGuardRowsLarge = 12;
inline int guard_rows(bool large_text) { return large_text ? kGuardRowsLarge : kGuardRows; }
inline int guard_line_y(int h, bool large_text = false) { return h - guard_rows(large_text); }
inline int guard_label_y(bool large_text) { return (guard_rows(large_text) - kGuardRows + 1) / 2; }

// The board's own ticker, along its bottom 8 rows, shows when the board is
// the whole screen's height: one row of panels, or the top half of a 2x2
// beside its second area (split.h), which has a ticker of its own.
inline bool board_ticker(int h) { return h <= kBoardRows; }

// The second area's clock (split.h; gd_lower, 64 rows): the time
// (spleen_16, 16 rows from row 5), the date (spleen_12) and its ticker
// (spleen_16). Under the main board's clock card, which has the time
// already, the time stays hidden; the date shows only when the card has the
// next game under its time rather than the date, so neither is up twice.
// card_date: the clock card shows the date. The ticker always ends above the
// heat and power warning's line (guard_line_y), which would cut its letters
// off; under the clock card what shows is centered in the 64 rows.
// A Large clock is spleen_24, "12:45 PM" from 128 wide; on one panel (64
// wide) the digits alone, with the AM or PM under them in spleen_8. The
// ticker is spleen_16 at either text size, already the panel's biggest with
// every letter; with Large text it ends three rows higher, above the
// warning's taller line.
static const int kLowerTimeY = 5, kLowerTimeRows = 16, kLowerDateRows = 12, kLowerTickerRows = 16;
struct LowerHalf {
  bool time;
  bool date;
  int date_y;
  int ticker_y;
  BoardFont time_font{BoardFont::S16};
  int time_y{kLowerTimeY};
  bool ampm{false};  // the time's digits alone, the AM or PM on a line of its own (spleen_8)
  int ampm_y{0};
};

inline LowerHalf lower_half(bool clock_card, bool card_date, int w = 128, Sizes sz = Sizes{}) {
  // The ticker's last row above the warning's line at this text size
  const int ticker_end = kBoardRows - guard_rows(sz.text) - kLowerTickerRows;
  if (clock_card) {
    if (card_date)
      return LowerHalf{false, false, 23, (kBoardRows - kLowerTickerRows) / 2};
    return LowerHalf{false, true, 12, 36};
  }
  if (!sz.clock)
    return LowerHalf{true, true, 23, ticker_end};
  if (w < 128)
    return LowerHalf{true, true, 26, ticker_end, BoardFont::S24, 0, true, 19};
  return LowerHalf{true, true, 22, ticker_end < 38 ? ticker_end : 38, BoardFont::S24, 1};
}

// The main board's clock card (the off-season clock and the idle display's
// Clock): its time, and under it the next game on a wide board (two lines
// of spleen_12) or the date on one panel. A Large clock is spleen_24
// ("12:45 PM", 96 px) up to 128 wide and spleen_32 (128 px) on three or four
// panels in a row; one panel has room for its digits alone, so the AM or PM
// goes on a line of its own under them, then the date.
struct ClockCardGeom {
  BoardFont time_font;
  int16_t time_y;
  bool ampm;  // the digits alone, the AM or PM under them in spleen_8
  int16_t ampm_y;
  BoardFont sub_font;
  int16_t sub_y;
};

inline ClockCardGeom clock_card_geom(int w, bool large_clock) {
  const bool wide = w >= 128;
  if (!large_clock)
    return wide ? ClockCardGeom{BoardFont::S16, 6, false, 0, BoardFont::S12, 26}
                : ClockCardGeom{BoardFont::S16, 14, false, 0, BoardFont::S8, 34};
  if (!wide)
    return ClockCardGeom{BoardFont::S24, 4, true, 25, BoardFont::S8, 35};
  if (w < 192)
    return ClockCardGeom{BoardFont::S24, 1, false, 0, BoardFont::S12, 24};
  return ClockCardGeom{BoardFont::S32, 0, false, 0, BoardFont::S12, 28};
}

// The clock's time split for a line of its own for the AM or PM: "12:45 PM"
// is "12:45" and "PM".
inline std::string clock_digits(const std::string &time) { return time.substr(0, time.find(' ')); }
inline std::string clock_ampm(const std::string &time) {
  const size_t sp = time.find(' ');
  return sp == std::string::npos ? std::string() : time.substr(sp + 1);
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
// Large text makes the ticker spleen_12, its 12-row line (the tails of y, p
// and q on the last) four rows higher, so on a wide board the situation
// row's letters (down and distance, the count, the outs) move up three rows
// and the pips and the bases two, and on one panel the scores move up one,
// the possession line under them closer, to keep clear of it.

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
  BoardFont ticker_font;
  int16_t score_poss_dy;  // one panel: from a score's top to the possession line under it
  // Baseball: first, second and third base as a small diamond (first on the
  // right), each base_px square.
  int16_t base_px;
  int16_t base_x[3], base_y[3];
  int16_t out_x[3], out_y;  // one panel only: the out dots
  Spot count, outs;         // wide only: the count left of the diamond, "2 OUT" right of it
};

inline GameGeom game_geom(int w, bool large_text = false) {
  GameGeom g{};
  const bool wide = w >= 128;
  const int half = w / 2;  // also the diamond's middle
  // On a wide board, with Large text: the situation row's letters, whose
  // tails (spleen_8's last row) end above the ticker's capitals, and the
  // pips and the bases, which end above them too
  const int lift = large_text ? 3 : 0, shape_lift = large_text ? 2 : 0;
  g.wide = wide;
  g.ticker_font = large_text ? BoardFont::S12 : BoardFont::S8;
  g.ticker_y = (int16_t) (kBoardRows - font_rows(g.ticker_font));
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
    g.down = {32, (int16_t) (48 - lift), (int16_t) (w - 64)};
    // Centered under each logo, 18 px in from its edge
    for (int i = 0; i < 3; i++) {
      g.pip_x[i] = (int16_t) (18 - 9 + i * 7);
      g.pip_x[3 + i] = (int16_t) (w - 18 - 9 + i * 7);
    }
    g.pip_y = (int16_t) (48 - shape_lift);
    g.base_px = 4;
    g.base_x[0] = (int16_t) (half + 3);
    g.base_x[1] = (int16_t) (half - 2);
    g.base_x[2] = (int16_t) (half - 7);
    g.base_y[0] = (int16_t) (51 - shape_lift);
    g.base_y[1] = (int16_t) (47 - shape_lift);
    g.base_y[2] = (int16_t) (51 - shape_lift);
    g.count = {32, (int16_t) (48 - lift), (int16_t) (half - 9 - 32)};
    g.outs = {(int16_t) (half + 9), (int16_t) (48 - lift), 28};  // "2 OUT" is 25 px in spleen_8
  } else {
    g.logo_x[0] = 0;
    g.logo_x[1] = (int16_t) (w - 32);
    g.logo_y = 0;
    g.abbr_font = BoardFont::S8;
    g.score_font = BoardFont::S12;
    g.clock_font = BoardFont::S8;
    g.abbr[0] = {0, 33, (int16_t) half};
    g.abbr[1] = {(int16_t) half, 33, (int16_t) half};
    const int16_t sy = large_text ? 41 : 42;
    g.score[0] = {0, sy, (int16_t) half};
    g.score[1] = {(int16_t) half, sy, (int16_t) half};
    g.score_poss_dy = large_text ? 10 : 12;
    g.clock = {0, 33, (int16_t) w};  // in the abbreviations' row
    g.base_px = 3;
    g.base_x[0] = (int16_t) (half + 3);
    g.base_x[1] = (int16_t) (half - 1);
    g.base_x[2] = (int16_t) (half - 5);
    g.base_y[0] = (int16_t) (sy + 5);
    g.base_y[1] = (int16_t) (sy + 1);
    g.base_y[2] = (int16_t) (sy + 5);
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

// ---- a splash with its logo --------------------------------------------------

// The lines LVGL breaks text into in a label chars characters wide: a new
// line at "\n", else at a space or after a hyphen once the next word won't
// fit. 0 when a word is longer than a line, which LVGL would cut in two.
inline size_t wrap_lines(const std::string &text, size_t chars) {
  if (chars == 0)
    return 0;
  size_t lines = 0;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    lines++;
    size_t line = 0;  // the characters on the line so far
    bool any = false, joined = false;
    for (size_t i = start; i < end;) {
      if (text[i] == ' ') {
        i++;
        continue;
      }
      size_t j = i;  // a word runs to a space, or through a hyphen
      while (j < end && text[j] != ' ' && text[j] != '-')
        j++;
      if (j < end && text[j] == '-')
        j++;
      const size_t n = j - i;
      if (n > chars)
        return 0;
      const size_t sep = any && !joined ? 1 : 0;
      if (any && line + sep + n > chars) {
        lines++;
        line = n;
      } else {
        line += sep + n;
      }
      any = true;
      joined = text[j - 1] == '-';
      i = j;
    }
    start = end + 1;
  }
  return lines;
}

// A score's or a big play's splash shows the logo of the side it is about
// (alerts.h, Splash::abbr), or both sides' for the whole game's (overtime,
// basketball's final), with the points beside or under it ("+6", "+2 RUNS")
// and the words, the fireworks or the color flash behind them as before.
// Each logo sits on a tile 2 px bigger, black with round corners, so it
// stands out on the color flash too; while the logo downloads the tile is the
// team's abbreviation badge (badge.h). Two tiles side by side on one panel
// lose the edge.
// - One panel (and the second area's 64x64): the tile with the points'
//   number beside it, or alone in the middle, or both logos, over the words in
//   spleen_12, two lines at most, else spleen_8 in three.
// - 128 wide (and the second area's 128x64): the tile and the points beside
//   it in the middle, over the words in spleen_16, two lines at most.
// - Three or four in a row: the tile at the left, the words in the middle 128
//   px (124 on three panels), the points or the other logo at the right.
// - Two rows of two, over all four panels: the tile, the words under it in up
//   to three lines, the points under them.
// The points are the biggest font they fit: spleen_32 or spleen_24, all of
// them ("+2 RUNS") or just their number ("+2") where they don't fit whole.
// words are the splash's words without the side's name, which the logo says
// (Splash::lead). Nothing fits (logos false): the words alone, as a splash
// without a logo has them (splash_box).
static const int kSplashTile = 34;  // a logo's tile: the 32 px logo and a 1 px edge
static const int kSplashLogo = 32;
static const int kSplashGap = 6;  // between a tile and what is beside it

struct SplashLayout {
  bool logos{false};
  uint8_t tiles{0};
  int16_t tile_px{kSplashTile};
  int16_t tile_x[2]{};
  int16_t tile_y{0};
  bool points{false};
  bool points_whole{true};  // all of the points, else their number alone
  BoardFont points_font{BoardFont::S32};
  Spot pts{};  // the points' label, the text centered in it
  BoardFont words_font{BoardFont::S16};
  Spot words{};  // the words' label, the lines centered in it
  uint8_t lines{0};
};

// The points' number: "+2" of "+2 RUNS".
inline std::string points_number(const std::string &points) { return points.substr(0, points.find(' ')); }

inline SplashLayout splash_layout(int w, int h, int tiles, const std::string &points, const std::string &words) {
  SplashLayout L;
  if (tiles <= 0 || words.empty() || w < kPanelPx || h < kBoardRows)
    return L;
  L.tiles = tiles > 1 ? 2 : 1;
  // The four layouts: one panel, two rows of two, 128 wide, three or four in
  // a row. Each has the words' fonts to try, biggest first, with the lines
  // each may take; the fonts the points may take, all of them (from
  // spleen_32 down to whole_last, none past it) or their number alone (down
  // to number_last); and the room beside or under the tile for them.
  enum { ONE, TWO_ROWS, WIDE, ROW } kind = w < 128 ? ONE : h > kBoardRows ? TWO_ROWS : w < 192 ? WIDE : ROW;
  const int side = (w - 128) / 2 > kSplashTile ? (w - 128) / 2 : kSplashTile;  // ROW: the tile's and the points' room
  struct Try {
    BoardFont font;
    uint8_t lines;
  };
  static const Try kTries[4][2] = {{{BoardFont::S12, 2}, {BoardFont::S8, 3}},
                                   {{BoardFont::S16, 3}, {BoardFont::S16, 3}},
                                   {{BoardFont::S16, 2}, {BoardFont::S12, 2}},
                                   {{BoardFont::S16, 3}, {BoardFont::S16, 3}}};
  static const int8_t kWholeLast[4] = {(int8_t) BoardFont::S32 + 1, (int8_t) BoardFont::S24, (int8_t) BoardFont::S24,
                                       (int8_t) BoardFont::S24};
  static const int8_t kNumberLast[4] = {(int8_t) BoardFont::S16, (int8_t) BoardFont::S32, (int8_t) BoardFont::S32,
                                        (int8_t) BoardFont::S16};
  const int room[4] = {w - kSplashTile, w, w - kSplashTile - kSplashGap, side - 2};
  L.words = kind == ROW ? Spot{(int16_t) side, 0, (int16_t) (w - 2 * side)} : Spot{0, 0, (int16_t) w};
  for (const Try &t : kTries[kind]) {
    const size_t n = wrap_lines(words, (size_t) (L.words.w / font_px(t.font)));
    if (n > 0 && n <= t.lines) {
      L.words_font = t.font;
      L.lines = (uint8_t) n;
      break;
    }
  }
  if (L.lines == 0)
    return SplashLayout{};
  // The biggest font the points fit their room in: all of them, else their number.
  const int whole_n = (int) points.size(), number_n = (int) points_number(points).size();
  for (int whole = 1; L.tiles == 1 && whole_n > 0 && !L.points && whole >= 0; whole--)
    for (int f = (int) BoardFont::S32; f >= (whole ? kWholeLast[kind] : kNumberLast[kind]) && !L.points; f--)
      if ((whole ? whole_n : number_n) * font_px((BoardFont) f) <= room[kind]) {
        L.points = true;
        L.points_whole = whole || number_n == whole_n;
        L.points_font = (BoardFont) f;
      }
  const int points_px = (L.points_whole ? whole_n : number_n) * font_px(L.points_font);
  const int lines_px = L.lines * font_rows(L.words_font);
  const int blank = font_ink_top(L.words_font);  // the words' first rows, blank in capitals
  switch (kind) {
    case ONE:
      // The tile with the points' number beside it, or alone in the middle,
      // or both logos, over the words.
      L.tile_px = L.tiles == 2 ? kSplashLogo : kSplashTile;
      L.tile_y = (int16_t) ((kBoardRows - (L.tile_px + 1 + lines_px)) / 2);
      L.tile_x[0] = (int16_t) (L.tiles == 1 && !L.points ? (w - kSplashTile) / 2 : 0);
      L.tile_x[1] = (int16_t) (w - kSplashLogo);
      L.pts = {kSplashTile, 0, (int16_t) (w - kSplashTile)};
      L.words.y = (int16_t) (L.tile_y + L.tile_px + 1);
      break;
    case TWO_ROWS: {
      // The tile, the words, the points, down the middle
      const int head = L.tiles == 2 ? 2 * kSplashTile + kSplashGap : kSplashTile;
      const int points_rows = L.points ? font_rows(L.points_font) - 2 : 0;
      L.tile_y = (int16_t) ((h - (kSplashTile + 2 + lines_px + points_rows)) / 2);
      L.tile_x[0] = (int16_t) ((w - head) / 2);
      L.tile_x[1] = (int16_t) (L.tile_x[0] + kSplashTile + kSplashGap);
      L.words.y = (int16_t) (L.tile_y + kSplashTile + 2);
      L.pts = {0, (int16_t) (L.words.y + lines_px - 2), (int16_t) w};
      break;
    }
    case WIDE: {
      // The tile and the points side by side, over the words
      const int head = kSplashTile + (L.tiles == 2 ? kSplashGap + kSplashTile : L.points ? kSplashGap + points_px : 0);
      L.tile_y = (int16_t) ((h - (kSplashTile + lines_px - blank)) / 2);
      L.tile_x[0] = (int16_t) ((w - head) / 2);
      L.tile_x[1] = (int16_t) (L.tile_x[0] + kSplashTile + kSplashGap);
      L.pts = {L.tile_x[1], 0, (int16_t) points_px};
      L.words.y = (int16_t) (L.tile_y + kSplashTile - blank);
      break;
    }
    case ROW:
      // The tile, the words, the points or the other logo
      L.tile_y = (int16_t) ((h - kSplashTile) / 2);
      L.tile_x[0] = (int16_t) ((side - kSplashTile) / 2);
      L.tile_x[1] = (int16_t) (w - side + (side - kSplashTile) / 2);
      L.pts = {(int16_t) (w - side), 0, (int16_t) side};
      L.words.y = (int16_t) ((h - lines_px) / 2);
      break;
  }
  // The points' ink centered on the tile's middle row, but under the words
  // on two rows of two
  if (kind != TWO_ROWS)
    L.pts.y = (int16_t) (L.tile_y + L.tile_px / 2 -
                         (font_ink_top(L.points_font) + font_ink_bottom(L.points_font) + 1) / 2);
  if (!L.points)
    L.pts = Spot{};
  L.logos = true;
  return L;
}

}  // namespace esphome::gameday
