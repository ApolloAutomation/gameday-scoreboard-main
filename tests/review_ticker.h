// Review PNGs of the ticker rows beside a second area
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "png_write.h"
#include "ticker_render.h"

namespace espn {

struct TickerCase {
  const char *name;
  SplitShow show;
  AreaView view;
  const char *board, *area, *abbr;
  uint32_t team;
};

inline const std::vector<TickerCase> &ticker_cases() {
  static const char *const kGame =
      "Q3 8:12 | 2nd & 7 at DAL 34 | Prescott 18/26 211 yds 2 TD | Lamb 7 rec 96 yds | O/U 47.5";
  static const std::vector<TickerCase> k = {
      {"flights", SplitShow::FLIGHTS, AreaView::APP, kGame,
       "23 planes nearby | JBU1407 JetBlue, Airbus A321neo, JFK to CDG, 1,525 ft, 203 mph, 1.5 mi SE | LOT5N LOT, "
       "Boeing 777-200, JFK to WAW, 4,500 ft, 244 mph, 5.7 mi E",
       "", 0},
      {"weather", SplitShow::WEATHER, AreaView::APP, kGame,
       "72\xC2\xB0 Sunny | Feels like 74\xC2\xB0 | Wind SW 8 mph | Humidity 41% | Tonight 58\xC2\xB0 Clear", "", 0},
      {"markets", SplitShow::MARKETS, AreaView::APP, kGame,
       "AAPL 231.40 +1.2% | MSFT 418.02 -0.3% | BTC 98,120 +2.1% | ETH 3,410 -0.8%", "", 0},
      {"f1", SplitShow::F1, AreaView::APP, kGame,
       "Next: Las Vegas GP Sat 10:00 PM | Verstappen leads by 42 pts | Norris 2nd | Leclerc 3rd", "", 0},
      {"team", SplitShow::TEAM, AreaView::TEAM, kGame,
       "Q2 4:31 | Jones 12/19 141 yds | Barkley 14 car 88 yds | `2ED573 NYG -3.5 COVERING` | O/U 44", "NYG",
       0x0B2265},
      {"scores", SplitShow::SCORES, AreaView::SCORES, kGame, "Final | DAL 27 NYG 20 | PHI 31 WSH 17 | SF 24 SEA 21",
       "", 0},
      {"live", SplitShow::LIVE, AreaView::LIVE, kGame, "KC 14 LV 7 | BUF 10 MIA 10 | GB 3 CHI 0", "KC", 0xE31837},
      {"gameday", SplitShow::GAMEDAY, AreaView::GAME,
       "72\xC2\xB0 Sunny | Feels like 74\xC2\xB0 | Wind SW 8 mph | Humidity 41% | Tonight 58\xC2\xB0 Clear", kGame,
       "DAL", 0x041E42},
  };
  return k;
}

// The bottom rows of each panel row, 1 px apart
inline Canvas ticker_strip(const Canvas &cv, int rows) {
  Canvas out;
  out.w = cv.w;
  const int bands = cv.h / 64;
  out.h = bands * (rows + 1);
  out.px.assign((size_t) out.w * out.h, rgb565(0x202020));
  for (int b = 0; b < bands; b++)
    for (int y = 0; y < rows; y++)
      for (int x = 0; x < cv.w; x++)
        out.px[(size_t) (b * (rows + 1) + y) * out.w + x] = cv.px[(size_t) (b * 64 + 64 - rows + y) * cv.w + x];
  return out;
}

// Stacks shots under a caption each
struct TickerSheet {
  Canvas cv;
  void add(const Canvas &c, const std::string &caption, const ReviewFonts &fonts) {
    const int w = std::max(cv.w, c.w);
    Canvas next;
    next.w = w;
    next.h = cv.h + 10 + c.h + 2;
    next.px.assign((size_t) next.w * next.h, rgb565(0x101010));
    for (int y = 0; y < cv.h; y++)
      for (int x = 0; x < cv.w; x++)
        next.px[(size_t) y * w + x] = cv.px[(size_t) y * cv.w + x];
    ticker_glyphs(next, fonts.f[0], BoardFont::S8, 1, cv.h + 1, 0, w, 0x909090, caption);
    for (int y = 0; y < c.h; y++)
      for (int x = 0; x < c.w; x++)
        next.px[(size_t) (cv.h + 10 + y) * w + x] = c.px[(size_t) y * c.w + x];
    cv = next;
  }
};

inline int render_ticker_views(const std::string &dir, const ReviewFonts &fonts) {
  int n = 0;
  static const int kMoments[3][2] = {{0, 0}, {70, 4000}, {150, 8000}};
  auto save = [&](const Canvas &c, const std::string &name, int scale) {
    if (png_save(dir + "/" + name + ".png", c.px.data(), c.w, c.h, scale))
      n++;
    else
      fprintf(stderr, "can't write %s\n", name.c_str());
  };
  for (int cols = 3; cols <= 4; cols++) {
    TickerSheet sheet, before;
    for (const TickerCase &k : ticker_cases()) {
      for (int m = 0; m < 3; m++) {
        TickerShot s;
        s.cols = cols;
        s.show = k.show;
        s.view = k.view;
        s.board = k.board;
        s.area = k.area;
        s.abbr = k.abbr;
        s.team = k.team;
        s.scroll = kMoments[m][0];
        s.ms = kMoments[m][1];
        const Canvas full = ticker_render(s, fonts);
        char name[96];
        snprintf(name, sizeof name, "ticker_%d_%s_%d", cols * 64, k.name, m);
        save(full, name, 4);
        char cap[96];
        snprintf(cap, sizeof cap, "%s  scroll %d px  %u s", k.name, kMoments[m][0], (unsigned) (kMoments[m][1] / 1000));
        sheet.add(ticker_strip(full, 14), cap, fonts);
        if (m == 0) {
          s.before = true;
          before.add(ticker_strip(ticker_render(s, fonts), 14), std::string(k.name) + "  before", fonts);
        }
      }
    }
    // Large text: spleen_12 tickers
    TickerShot big;
    big.cols = cols;
    big.board = ticker_cases()[0].board;
    big.area = ticker_cases()[0].area;
    big.large = true;
    for (int m = 0; m < 3; m++) {
      big.scroll = kMoments[m][0];
      big.ms = kMoments[m][1];
      sheet.add(ticker_strip(ticker_render(big, fonts), 16), "flights  large text", fonts);
    }
    save(sheet.cv, "sheet_" + std::to_string(cols * 64), 4);
    save(before.cv, "sheet_" + std::to_string(cols * 64) + "_before", 4);
  }
  // Two rows of two: unchanged
  TickerSheet quad;
  for (const TickerCase &k : ticker_cases()) {
    TickerShot s;
    s.cols = 2;
    s.rows = 2;
    s.show = k.show;
    s.view = k.view;
    s.board = k.board;
    s.area = k.area;
    for (int m = 0; m < 2; m++) {
      s.scroll = kMoments[m][0];
      s.ms = kMoments[m][1];
      quad.add(ticker_strip(ticker_render(s, fonts), 14), std::string("2x2 ") + k.name, fonts);
    }
  }
  save(quad.cv, "sheet_2x2", 4);
  return n;
}

}  // namespace espn
