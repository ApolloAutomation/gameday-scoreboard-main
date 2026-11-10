// Panel-arrangement layout math; pure, so host tests compile it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace esphome::gameday {

static const int kPanelPx = 64;   // one panel, each way
static const int kBoardRows = 64;  // the board's height on every arrangement

// spleen_24/32 hold only clock and points glyphs.
enum class BoardFont : uint8_t { S8, S12, S16, S24, S32 };
// Metrics measured from the fonts' own bitmaps.
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
// Lowest inked row, descenders included.
inline int font_desc_bottom(BoardFont f) {
  static const int8_t k[] = {7, 11, 14, 18, 25};
  return k[(int) f];
}

struct Sizes {
  bool clock{false};  // Large clock size
  bool text{false};   // Large text size
};

// A 2x2 counts as two panels wide.
inline int board_px(int panels) { return panels > 0 ? panels * kPanelPx : kPanelPx; }

// Words are sized for two panels; wider boards fit.
inline size_t splash_chars(int w) { return (size_t) (w >= 128 ? w / 8 : w / 6); }

// Text offsets let three lines end at the bottom.
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

// Large text needs 12 rows to cover its ticker.
static const int kGuardRows = 9, kGuardRowsLarge = 12;
inline int guard_rows(bool large_text) { return large_text ? kGuardRowsLarge : kGuardRows; }
inline int guard_line_y(int h, bool large_text = false) { return h - guard_rows(large_text); }
inline int guard_label_y(bool large_text) { return (guard_rows(large_text) - kGuardRows + 1) / 2; }

// A 2x2's second area has its own ticker.
inline bool board_ticker(int h) { return h <= kBoardRows; }

// Never repeat the clock card's time or date.
static const int kLowerTimeY = 5, kLowerTimeRows = 16, kLowerDateRows = 12, kLowerTickerRows = 16;
struct LowerHalf {
  bool time;
  bool date;
  int date_y;
  int ticker_y;
  BoardFont time_font{BoardFont::S16};
  int time_y{kLowerTimeY};
  bool ampm{false};  // AM or PM on its own line (spleen_8)
  int ampm_y{0};
};

inline LowerHalf lower_half(bool clock_card, bool card_date, int w = 128, Sizes sz = Sizes{}) {
  // Ticker ends above the warning, which would clip it.
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

// One panel fits only the digits; AM/PM goes below.
struct ClockCardGeom {
  BoardFont time_font;
  int16_t time_y;
  bool ampm;  // AM or PM under the digits (spleen_8)
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

inline std::string clock_digits(const std::string &time) { return time.substr(0, time.find(' ')); }
inline std::string clock_ampm(const std::string &time) {
  const size_t sp = time.find(' ');
  return sp == std::string::npos ? std::string() : time.substr(sp + 1);
}

// Index 0 is the team's side, on the left.

struct Spot {
  int16_t x, y, w;
};

struct GameGeom {
  bool wide;  // 128 wide and up
  int16_t logo_x[2], logo_y;
  BoardFont abbr_font, score_font, clock_font;
  Spot abbr[2], score[2], clock;
  Spot rec[2], down;      // wide boards only
  int16_t pip_x[6];       // wide only: the team's three timeouts, then the opponent's
  int16_t pip_y;
  BoardFont msg_font;     // the no-game message (centered both ways)
  BoardFont sub_font;     // the clock card's second line, under its spleen_16 time
  int16_t clock_time_y, clock_sub_y;
  BoardFont splash_font;  // the splash's words, at splash_box's text_y
  int16_t ticker_y;       // the board's own ticker, along its bottom
  BoardFont ticker_font;
  int16_t score_poss_dy;  // one panel: score top to possession line
  // Bases first to third; first is on the right.
  int16_t base_px;
  int16_t base_x[3], base_y[3];
  int16_t out_x[3], out_y;  // one panel only: the out dots
  Spot count, outs;         // wide only: count left, "2 OUT" right
};

inline GameGeom game_geom(int w, bool large_text = false) {
  GameGeom g{};
  const bool wide = w >= 128;
  const int half = w / 2;  // also the diamond's middle
  // Large text lifts these clear of the taller ticker.
  const int lift = large_text ? 3 : 0, pip_lift = large_text ? 2 : 0;
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
    g.pip_y = (int16_t) (48 - pip_lift);
    g.base_px = 4;
    g.base_x[0] = (int16_t) (half + 3);
    g.base_x[1] = (int16_t) (half - 2);
    g.base_x[2] = (int16_t) (half - 7);
    g.base_y[0] = (int16_t) (51 - lift);
    g.base_y[1] = (int16_t) (47 - lift);
    g.base_y[2] = (int16_t) (51 - lift);
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
    const int16_t sy = large_text ? 40 : 42;
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
    g.out_y = (int16_t) (large_text ? 50 : 51);
  }
  return g;
}

// Dark colors vanish on the panel, so lift them.
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

inline bool no_game_screen(const std::string &game_state, bool clock_card, bool board_card) {
  return clock_card || board_card || game_state == "NOT_FOUND" || game_state == "BYE" || game_state.empty();
}

inline bool clock_card_date(int w, bool have_next) { return w < 128 || !have_next; }

// ---- a splash with its logo --------------------------------------------------

// Mirrors LVGL wrapping; 0 if LVGL would split a word.
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

// Tiles 2 px bigger so logos stand out on flashes.
static const int kSplashTile = 34;  // 32 px logo plus a 1 px edge
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
  // Per layout: word fonts biggest first, then point fonts.
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
  // Biggest font that fits: whole points, else the number.
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
      // Tile beside the number, or both logos, over words.
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
      // Tile and points side by side, over the words.
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
  // Center the points' ink on the tile's middle row.
  if (kind != TWO_ROWS)
    L.pts.y = (int16_t) (L.tile_y + L.tile_px / 2 -
                         (font_ink_top(L.points_font) + font_ink_bottom(L.points_font) + 1) / 2);
  if (!L.points)
    L.pts = Spot{};
  L.logos = true;
  return L;
}

}  // namespace esphome::gameday
