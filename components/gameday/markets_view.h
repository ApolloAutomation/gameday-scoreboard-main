// Stocks & Crypto frames: rows, one-symbol card, sparkline, ticker.
#pragma once

#include <string>
#include <vector>

#include "board_layout.h"
#include "markets.h"
#include "panel_text.h"

namespace espn {

using MarketFont = esphome::gameday::BoardFont;
using MarketSpot = esphome::gameday::Spot;

// ---- the sparkline points -------------------------------------------------------

struct SparkPoint {
  int16_t x, y;
};

struct SparkLine {
  uint8_t n{0};         // points; under 2 draws nothing
  int16_t base_y{-1};   // the last close's row; -1 none
  uint32_t color{kMarketFlat};
  SparkPoint pts[kSparkPoints];
};

// stretch: the day so far fills the box.
inline SparkLine market_spark(const MarketQuote &q, int x, int y, int w, int h, bool stretch = false) {
  SparkLine L;
  if (!q.valid || w < 2 || h < 2)
    return L;
  int first = -1, last = -1, filled = 0;
  for (int i = 0; i < kSparkPoints; i++)
    if (q.spark[i] != kSparkEmpty) {
      first = first < 0 ? i : first;
      last = i;
      filled++;
    }
  if (filled < 2)
    return L;
  const bool base = q.prev > 0 && std::isfinite(q.prev);
  double lo = q.spark_lo, hi = q.spark_hi;
  if (base) {
    lo = q.prev < lo ? q.prev : lo;
    hi = q.prev > hi ? q.prev : hi;
  }
  const double span = hi - lo;
  auto row = [&](double v) {
    double r = span > 0 && std::isfinite(span) ? std::round((v - lo) / span * (h - 1)) : (h - 1) / 2;
    r = std::isfinite(r) ? r : (h - 1) / 2;
    return (int16_t) (y + h - 1 - (r < 0 ? 0 : r > h - 1 ? h - 1 : (int) r));
  };
  const int from = stretch ? first : 0, to = stretch ? last : kSparkPoints - 1;
  for (int i = first; i <= last; i++) {
    if (q.spark[i] == kSparkEmpty)
      continue;
    const int px = to > from ? x + (i - from) * (w - 1) / (to - from) : x;
    L.pts[L.n++] = SparkPoint{(int16_t) px, row(market_spark_value(q, i))};
  }
  if (base)
    L.base_y = row(q.prev);
  L.color = market_color(market_dir(q));
  return L;
}

// ---- rows of symbols ------------------------------------------------------------

constexpr int kMarketCharPx = 5;  // spleen_8 is 5 px
constexpr int kMarketRowPx = 8;
constexpr int kMarketRowsMax = 7;

// A label; values align right in it.
struct MarketCol {
  int16_t x{0}, w{0};
  size_t chars() const { return (size_t) (w / kMarketCharPx); }
};

struct MarketGeom {
  int16_t w{64}, h{64};
  // 2: symbol, price or percent; 3: percent; 4: change
  uint8_t cols{2};
  MarketCol sym, price, change, pct;
  uint8_t rows{6};
  int16_t row_y{8};  // the first row; the title row above
  bool spark{false};
  int16_t spark_x{0}, spark_y{0}, spark_w{0}, spark_h{0};
};

// ticker: the area's bottom 8 rows hold one.
inline MarketGeom market_geom(int w, int h, bool ticker, bool spark) {
  MarketGeom g;
  g.w = (int16_t) w;
  g.h = (int16_t) h;
  const int bottom = h - (ticker ? kMarketRowPx : 0);
  const int fit = (bottom - g.row_y) / kMarketRowPx;
  g.rows = (uint8_t) (fit < 1 ? 1 : fit > kMarketRowsMax ? kMarketRowsMax : fit);
  int bx = 0;
  if (w >= 192) {
    // A 190 px block: centered, or left beside the sparkline
    g.cols = 4;
    const bool side = spark && w - 190 >= 40;
    bx = side ? 0 : (w - 190) / 2;
    g.sym = {(int16_t) bx, 40};
    g.price = {(int16_t) (bx + 40), 55};
    g.change = {(int16_t) (bx + 95), 50};
    g.pct = {(int16_t) (bx + 145), 45};
    if (side) {
      g.spark = true;
      g.spark_x = 194;
      g.spark_y = (int16_t) (g.row_y + 1);
      g.spark_w = (int16_t) (w - 196);
      g.spark_h = (int16_t) (bottom - 1 - g.spark_y);
      return g;
    }
  } else if (w >= 128) {
    g.cols = 3;
    g.sym = {0, 30};
    g.price = {30, 50};
    g.pct = {80, (int16_t) (w - 80)};
  } else {
    g.sym = {0, 30};
    g.price = {30, (int16_t) (w - 30)};
  }
  // Narrower boards trade rows for the sparkline.
  if (spark && g.rows >= 5) {
    g.spark = true;
    g.rows = 4;
    g.spark_x = 0;
    g.spark_y = (int16_t) (g.row_y + g.rows * kMarketRowPx + 1);
    g.spark_w = (int16_t) w;
    g.spark_h = (int16_t) (bottom - 1 - g.spark_y);
  }
  return g;
}

struct MarketRow {
  std::string sym, price, change, pct;
  uint32_t color{kMarketFlat};  // change and percent; two columns the value
  bool spark{false};            // the sparkline's symbol
};

// alt: two-column rows show percent, not price.
inline MarketRow market_row(const MarketSymbol &s, const MarketQuote &q, const MarketGeom &g, int64_t now, bool alt) {
  MarketRow r;
  r.sym = std::string(s.sym).substr(0, g.sym.chars());
  if (!q.valid) {
    r.price = "--";
    return r;
  }
  const double pct = market_percent(q);
  r.color = market_stale(q, (MarketKind) s.kind, now) ? kMarketStale : market_color(market_dir(q));
  if (g.cols == 2) {
    r.price = alt && std::isfinite(pct) ? market_percent_text(pct, g.price.chars())
                                        : market_price_text(q.price, g.price.chars());
    return r;
  }
  r.price = market_price_text(q.price, g.price.chars());
  r.pct = market_percent_text(pct, g.pct.chars());
  if (g.cols == 4 && q.prev != 0)
    r.change = market_change_text(market_change(q), q.price, g.change.chars());
  return r;
}

// "STOCKS", "CRYPTO" or "MARKETS", by what is listed.
inline const char *market_title(const MarketBlob &b) {
  bool coin = false, other = false;
  for (uint8_t i = 0; i < market_count(b); i++)
    (b.syms[i].kind == (uint8_t) MarketKind::CRYPTO ? coin : other) = true;
  return coin && other ? "MARKETS" : coin ? "CRYPTO" : "STOCKS";
}

struct MarketFrame {
  std::string title;   // wide boards, left; with the page
  std::string status;  // right, or centered on one panel
  uint32_t status_color{0xFFFFFF};
  uint8_t n{0}, page{0}, pages{1};
  MarketRow rows[kMarketRowsMax];
  bool spark{false};
  SparkLine line;
};

constexpr uint32_t kMarketOpenColor = 0x2ED573, kMarketShutColor = 0xA0A0A0;

// page wraps; alt flips two-column rows to percent.
inline MarketFrame market_frame(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], const MarketGeom &g,
                                int64_t now, uint8_t page, bool alt) {
  MarketFrame f;
  const uint8_t count = market_count(b);
  f.pages = (uint8_t) (count == 0 ? 1 : (count + g.rows - 1) / g.rows);
  f.page = (uint8_t) (page % f.pages);
  const bool wide = g.w >= 128;
  const MarketBoardState st = market_board_state(b, q, now);
  if (st.slot >= 0) {
    f.status = market_state_text(st.state, q[st.slot], now, wide);
    f.status_color = st.state == MarketState::OPEN ? kMarketOpenColor : kMarketShutColor;
  } else if (!wide) {
    f.status = market_title(b);
  }
  if (wide) {
    f.title = market_title(b);
    if (f.pages > 1)
      f.title += " " + std::to_string(f.page + 1) + "/" + std::to_string(f.pages);
  }
  const int spark_slot = g.spark ? market_spark_slot(b) : -1;
  for (int i = f.page * g.rows; i < count && f.n < g.rows; i++) {
    f.rows[f.n] = market_row(b.syms[i], q[i], g, now, alt);
    f.rows[f.n++].spark = i == spark_slot;
  }
  if (spark_slot >= 0) {
    f.line = market_spark(q[spark_slot], g.spark_x, g.spark_y, g.spark_w, g.spark_h);
    f.spark = f.line.n >= 2;
  }
  return f;
}

// ---- one symbol at a time -------------------------------------------------------

struct MarketCard {
  std::string sym, status, price, change, pct;
  MarketFont sym_font{MarketFont::S12}, price_font{MarketFont::S16};
  MarketSpot sym_at{}, status_at{}, price_at{}, change_at{}, pct_at{};  // status, pct right aligned
  uint32_t color{kMarketFlat};
  bool spark{false};
  SparkLine line;
};

// For a 64x64 area; wider ones fit more digits.
inline MarketCard market_card(const MarketSymbol &s, const MarketQuote &q, int w, int h, bool ticker, int64_t now) {
  using esphome::gameday::font_px;
  using esphome::gameday::font_rows;
  MarketCard c;
  const MarketState st = market_state(q, (MarketKind) s.kind, now);
  c.sym = s.sym;
  c.status = st == MarketState::OPEN ? "" : market_state_text(st, q, now, false);
  // Symbol in spleen_12, else spleen_8; status dropped last.
  const int n = (int) c.sym.size(), status_px = c.status.empty() ? 0 : 2 + (int) c.status.size() * kMarketCharPx;
  if (n * font_px(MarketFont::S8) + status_px > w)
    c.status.clear();
  const int room = w - (c.status.empty() ? 0 : status_px);
  c.sym_font = n * font_px(MarketFont::S12) <= room ? MarketFont::S12 : MarketFont::S8;
  c.sym_at = {0, 0, (int16_t) w};
  c.status_at = {0, (int16_t) (c.sym_font == MarketFont::S12 ? 2 : 0), (int16_t) w};
  const int price_y = font_rows(MarketFont::S12) + 1;
  c.price = "--";
  c.price_font = MarketFont::S16;
  if (q.valid) {
    // Biggest font holding every decimal, else spleen_8 cut
    const int d = market_decimals(q.price);
    for (MarketFont f : {MarketFont::S16, MarketFont::S12, MarketFont::S8}) {
      c.price_font = f;
      c.price = market_price_text(q.price, (size_t) (w / font_px(f)));
      const bool whole = c.price.size() * font_px(f) <= (size_t) w &&
                         (c.price == market_number(q.price, d, true, false) ||
                          c.price == market_number(q.price, d, false, false));
      if (whole)
        break;
    }
  }
  c.price_at = {0, (int16_t) (price_y + (font_rows(MarketFont::S16) - font_rows(c.price_font)) / 2), (int16_t) w};
  const int change_y = price_y + font_rows(MarketFont::S16) + 1;
  const size_t chars = (size_t) (w / kMarketCharPx);
  if (q.valid && q.prev != 0) {
    c.pct = market_percent_text(market_percent(q), chars);
    c.change = market_change_text(market_change(q), q.price, chars);
    if (c.change.size() + 1 + c.pct.size() > chars)
      c.change = market_change_text(market_change(q), q.price, chars - c.pct.size() - 1);
    if (c.change.size() + 1 + c.pct.size() > chars)
      c.change.clear();
    c.color = market_stale(q, (MarketKind) s.kind, now) ? kMarketStale : market_color(market_dir(q));
  }
  c.change_at = {0, (int16_t) change_y, (int16_t) w};
  c.pct_at = {0, (int16_t) change_y, (int16_t) w};
  const int spark_y = change_y + kMarketRowPx + 2, bottom = h - (ticker ? kMarketRowPx : 0);
  if (bottom - 1 - spark_y >= 4) {
    c.line = market_spark(q, 0, spark_y, w, bottom - 1 - spark_y);
    c.spark = c.line.n >= 2;
  }
  return c;
}

// ---- the ticker line ------------------------------------------------------------

// Each symbol, price and move; spans color the moves.
inline std::string market_ticker(const MarketBlob &b, const MarketQuote (&q)[kMarketSlots], int64_t now,
                                 std::vector<esphome::gameday::TickerSpan> *spans) {
  std::string line;
  for (uint8_t i = 0; i < market_count(b); i++) {
    if (!q[i].valid)
      continue;
    if (!line.empty())
      line += " | ";
    line += std::string(b.syms[i].sym) + " " + market_price_text(q[i].price, 12);
    if (q[i].prev == 0)
      continue;
    const std::string move = market_change_text(market_change(q[i]), q[i].price, 12) + " " +
                             market_percent_text(market_percent(q[i]), 8);
    line += " " + move;
    const int dir = market_dir(q[i]);
    if (spans != nullptr && dir != 0 && !market_stale(q[i], (MarketKind) b.syms[i].kind, now))
      spans->push_back({move, market_color(dir)});
  }
  return line;
}

}  // namespace espn
