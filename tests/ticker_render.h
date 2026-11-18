// The panel's ticker rows, drawn as the LVGL page lays them out
#pragma once

#include <string>
#include <vector>

#include "field_render.h"
#include "ticker_seam.h"

namespace espn {

using esphome::gameday::BoardFont;

// tt_st_status_text: both tickers at 75%
inline uint32_t ticker_dim(uint32_t c) {
  return ((((c >> 16) & 0xFF) * 191 / 255) << 16) | ((((c >> 8) & 0xFF) * 191 / 255) << 8) | ((c & 0xFF) * 191 / 255);
}

struct TickerShot {
  int cols{4}, rows{1};
  SplitShow show{SplitShow::FLIGHTS};
  AreaView view{AreaView::APP};
  std::string board, area;  // each ticker's line
  std::string abbr;         // the area's game, for its tag
  uint32_t team{0xFFFFFF};
  bool large{false};  // Large text: spleen_12 tickers
  bool full{false};   // a display fills the row
  int scroll{0};      // the scrolling lines' offset, px
  uint32_t ms{0};     // time since the area's pages began
  bool before{false};  // both scrolling, side by side
};

// One line of glyphs from x, cut to [x0, x1)
inline void ticker_glyphs(Canvas &cv, const BdfFont &f, BoardFont bf, int x, int y, int x0, int x1, uint32_t rgb,
                          const std::string &s) {
  const uint16_t c = rgb565(rgb);
  if (!f.ok()) {
    // No BDFs: a box per glyph, none for spaces
    const int gw = esphome::gameday::font_px(bf);
    for (int ch : utf8_codes(s)) {
      for (int px = std::max(x, x0); ch != ' ' && px < std::min(x + gw - 1, x1); px++)
        for (int r = 1; r < esphome::gameday::font_rows(bf) - 1; r++)
          cv.put(px, y + r, c);
      x += gw;
    }
    return;
  }
  const int base = y + f.ascent;
  for (int ch : utf8_codes(s)) {
    auto it = f.g.find(ch);
    if (it == f.g.end())
      continue;
    const BdfGlyph &g = it->second;
    const int top = base - g.yo - g.h, bytes = (g.w + 7) / 8;
    for (int r = 0; r < g.h && r < (int) g.rows.size(); r++)
      for (int b = 0; b < g.w; b++)
        if ((g.rows[r] >> (8 * bytes - 1 - b)) & 1) {
          const int px = x + g.xo + b;
          if (px >= x0 && px < x1)
            cv.put(px, top + r, c);
        }
    x += g.dw;
  }
}

inline int ticker_text_w(const BdfFont &f, BoardFont bf, const std::string &s) {
  return f.ok() ? bdf_width(f, s) : (int) utf8_codes(s).size() * esphome::gameday::font_px(bf);
}

// LVGL's circular scroll: centered if it fits
inline void ticker_scroll(Canvas &cv, const BdfFont &f, BoardFont bf, int x0, int w, int y, uint32_t rgb,
                          const std::string &s, int scroll) {
  const int tw = ticker_text_w(f, bf, s);
  if (tw <= w) {
    ticker_glyphs(cv, f, bf, x0 + (w - tw) / 2, y, x0, x0 + w, rgb, s);
    return;
  }
  const int period = tw + 3 * esphome::gameday::font_px(bf);
  const int off = scroll % period;
  for (int k = 0; k < 3; k++)
    ticker_glyphs(cv, f, bf, x0 - off + k * period, y, x0, x0 + w, rgb, s);
}

// The board's and the area's tickers, nothing else
inline Canvas ticker_render(const TickerShot &s, const ReviewFonts &fonts) {
  using esphome::gameday::font_rows;
  Canvas cv;
  cv.w = s.cols * 64;
  cv.h = s.rows * 64;
  cv.px.assign((size_t) cv.w * cv.h, 0);
  const SplitArea a = split_area(s.cols, s.rows, (uint8_t) s.show, s.full);
  const TickerSeam seam = s.before ? TickerSeam{false, a.main_w, 0, 0} : ticker_seam(a);
  const BoardFont bf = s.large ? BoardFont::S12 : BoardFont::S8;
  const BdfFont &f = fonts.f[(int) bf];
  const int ty = 64 - font_rows(bf);
  ticker_scroll(cv, f, bf, 0, seam.board_w, ty, ticker_dim(0xFFFFFF), s.board, s.scroll);
  if (!a.on || area_clock(s.view) || s.view == AreaView::NONE)
    return cv;
  const int ay = a.y + ty;
  if (!area_ticker_paged(seam, false)) {
    ticker_scroll(cv, f, bf, a.x, a.w, ay, ticker_dim(0xFFFFFF), s.area, s.scroll);
    return cv;
  }
  const TickerTag tag = area_tag(s.view, s.show, s.abbr, s.team);
  const int tx = seam_text_x(seam, tag), tw = a.w - tx;
  const std::string pages = ticker_pages(s.area, tw / esphome::gameday::font_px(bf));
  if (pages.empty())
    return cv;
  for (int y = ay; y < a.y + 64; y++)
    cv.put(a.x + seam.line_x, y, rgb565(kSeamColor));
  const int rows = font_rows(bf);
  if (tag.text[0] != '\0') {
    ticker_glyphs(cv, fonts.f[0], BoardFont::S8, a.x + seam.tag_x, ay + (rows - 8) / 2, a.x + seam.tag_x,
                  a.x + seam.tag_x + tag_width(tag), tag.color, tag.text);
  } else {
    for (int y = 0; y < kTagPx; y++)
      for (int x = 0; x < kTagPx; x++)
        if (tag_lit(tag.icon, x, y))
          cv.put(a.x + seam.tag_x + x, ay + (rows - kTagPx) / 2 + y, rgb565(tag.color));
  }
  const size_t page = (size_t) (s.ms / kTickerPageMs) % page_count(pages);
  ticker_glyphs(cv, f, bf, a.x + tx, ay, a.x + tx, a.x + a.w, ticker_dim(tag_text_color(tag.color)),
                page_at(pages, page));
  return cv;
}

}  // namespace espn
