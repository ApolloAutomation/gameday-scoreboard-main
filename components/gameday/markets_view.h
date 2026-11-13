// Stocks & Crypto frames: rows, one-symbol card, sparkline, ticker.
#pragma once

#include <cstdio>
#include <cstring>
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

// ---- logos ----------------------------------------------------------------------

// Stocks, funds and coins; not indexes or pairs.
inline bool market_has_logo(const char *sym, uint8_t kind) {
  const size_t len = strlen(sym);
  if (len == 0 || len > (size_t) kMarketSymMax || kind == (uint8_t) MarketKind::INDEX)
    return false;
  for (size_t i = 0; i < len; i++) {
    const char c = sym[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
      return false;
  }
  return true;
}

// Keyless logo host k's URL, first to last; 0: none.
inline size_t market_logo_url(const char *sym, uint8_t kind, uint8_t k, char *out, size_t n) {
  if (out == nullptr || n == 0 || !market_has_logo(sym, kind))
    return 0;
  char low[kMarketSymMax + 1], cls[kMarketSymMax + 1];
  const size_t len = strlen(sym);
  for (size_t i = 0; i <= len; i++) {
    low[i] = (char) (sym[i] >= 'A' && sym[i] <= 'Z' ? sym[i] - 'A' + 'a' : sym[i]);
    cls[i] = sym[i];
  }
  // Class shares as Yahoo writes them: BRK-B
  if (len >= 3 && cls[len - 2] == '.' && strchr(cls, '.') == cls + len - 2)
    cls[len - 2] = '-';
  int r = 0;
  if (kind == (uint8_t) MarketKind::CRYPTO) {
    // Colored coins first: Parqet's ETH is dim
    if (k == 0)
      r = snprintf(out, n, "https://raw.githubusercontent.com/spothq/cryptocurrency-icons/master/32/color/%s.png", low);
    else if (k == 1)
      r = snprintf(out, n, "https://assets.parqet.com/logos/crypto/%s?format=png&size=32", sym);
    else if (k == 2)
      r = snprintf(out, n, "https://assets.coincap.io/assets/icons/%s@2x.png", low);
  } else if (k == 0) {
    r = snprintf(out, n, "https://assets.parqet.com/logos/symbol/%s?format=png&size=32", cls);
  } else if (k == 1) {
    r = snprintf(out, n, "https://companiesmarketcap.com/img/company-logos/64/%s.png", cls);
  } else if (k == 2) {
    r = snprintf(out, n, "https://financialmodelingprep.com/image-stock/%s.png", sym);
  }
  return r > 0 && (size_t) r < n ? (size_t) r : 0;
}

inline bool market_any_logo(const MarketBlob &b) {
  for (uint8_t i = 0; i < market_count(b); i++)
    if (market_has_logo(b.syms[i].sym, b.syms[i].kind))
      return true;
  return false;
}

// Logos when some symbol has one; one panel: short symbols.
inline bool market_logos_fit(const MarketBlob &b, int w) {
  bool any = false;
  for (uint8_t i = 0; i < market_count(b); i++) {
    const MarketSymbol &s = b.syms[i];
    if (w < 128 && strlen(s.sym) > 4)
      return false;
    any = any || market_has_logo(s.sym, s.kind);
  }
  return any;
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
  MarketCol logo, sym, price, change, pct;  // logo.w 0: no logos
  MarketCol list;                           // the rows' block, for the highlight
  uint8_t rows{6};
  int16_t row_y{8};  // the first row; the title row above
  bool spark{false};
  int16_t spark_x{0}, spark_y{0}, spark_w{0}, spark_h{0};
  // The charted symbol's big logo; px 0: none
  int16_t focus_x{0}, focus_y{0};
  uint8_t focus_px{0};
  // Its name and move beside the logo; w 0: none
  MarketSpot name_at{}, move_at{};
};

constexpr int kMarketLogoPx = 7;  // a row's logo, 1 px apart
constexpr int kMarketLogoCol = kMarketLogoPx + 2;
constexpr int kMarketFocusPx = 24;  // the charted symbol's logo

// ticker: 8 rows; logos: a column; focus: a big logo
inline MarketGeom market_geom(int w, int h, bool ticker, bool spark, bool logos = false, bool focus = false) {
  MarketGeom g;
  g.w = (int16_t) w;
  g.h = (int16_t) h;
  g.list = {0, (int16_t) w};
  const int bottom = h - (ticker ? kMarketRowPx : 0);
  const int fit = (bottom - g.row_y) / kMarketRowPx;
  g.rows = (uint8_t) (fit < 1 ? 1 : fit > kMarketRowsMax ? kMarketRowsMax : fit);
  int bx = 0;
  if (w >= 192) {
    // A 190 px block: centered, or left beside the sparkline
    g.cols = 4;
    const bool side = spark && w - 190 >= 40;
    bx = side ? 0 : (w - 190) / 2;
    g.list = {(int16_t) bx, 190};
    g.sym = {(int16_t) bx, 40};
    g.price = {(int16_t) (bx + 40), 55};
    g.change = {(int16_t) (bx + 95), 50};
    g.pct = {(int16_t) (bx + 145), 45};
    if (logos) {
      // Change and percent give up a character each
      g.logo = {(int16_t) bx, kMarketLogoCol};
      g.sym = {(int16_t) (bx + 9), 40};
      g.price = {(int16_t) (bx + 49), 55};
      g.change = {(int16_t) (bx + 104), 46};
      g.pct = {(int16_t) (bx + 150), 40};
    }
    if (side) {
      g.spark = true;
      g.spark_x = 194;
      g.spark_y = (int16_t) (g.row_y + 1);
      g.spark_w = (int16_t) (w - 196);
      g.spark_h = (int16_t) (bottom - 1 - g.spark_y);
      if (focus) {
        // Logo, name and move over the line
        g.focus_x = g.spark_x;
        g.focus_y = g.row_y;
        g.focus_px = (uint8_t) kMarketFocusPx;
        const int16_t tx = (int16_t) (g.focus_x + kMarketFocusPx + 2);
        g.name_at = {tx, (int16_t) (g.row_y + 1), (int16_t) (w - tx)};
        g.move_at = {tx, (int16_t) (g.row_y + 14), (int16_t) (w - tx)};
        g.spark_y = (int16_t) (g.focus_y + kMarketFocusPx + 2);
        g.spark_h = (int16_t) (bottom - 1 - g.spark_y);
      }
      return g;
    }
  } else if (w >= 128) {
    g.cols = 3;
    g.sym = {0, 30};
    g.price = {30, 50};
    g.pct = {80, (int16_t) (w - 80)};
    if (logos) {
      g.logo = {0, kMarketLogoCol};
      g.sym = {9, 30};
      g.price = {39, 49};
      g.pct = {88, (int16_t) (w - 88)};
    }
  } else if (logos) {
    // Only when every symbol fits four characters
    g.logo = {0, kMarketLogoCol};
    g.sym = {9, 21};
    g.price = {30, (int16_t) (w - 30)};
  } else {
    g.sym = {0, 30};
    g.price = {30, (int16_t) (w - 30)};
  }
  // Narrower boards trade rows for the sparkline.
  if (spark && g.rows >= 5) {
    g.spark = true;
    // The logo's square under the rows: a row less
    const int strip = focus ? kMarketFocusPx : 2 * kMarketRowPx - 1;
    const int fit_rows = (bottom - g.row_y - strip) / kMarketRowPx;
    g.rows = (uint8_t) (fit_rows < 1 ? 1 : fit_rows > 4 ? 4 : fit_rows);
    const int top = g.row_y + g.rows * kMarketRowPx;
    g.spark_x = 0;
    g.spark_w = (int16_t) w;
    if (focus) {
      g.focus_x = (int16_t) bx;
      g.focus_y = (int16_t) (bottom - kMarketFocusPx);
      g.focus_px = (uint8_t) kMarketFocusPx;
      g.spark_x = (int16_t) (bx + kMarketFocusPx + 2);
      g.spark_w = (int16_t) (w - bx - g.spark_x);
    }
    g.spark_y = (int16_t) (top + 1);
    g.spark_h = (int16_t) (bottom - 1 - g.spark_y);
  }
  return g;
}

struct MarketRow {
  std::string sym, price, change, pct;
  uint32_t color{kMarketFlat};  // change and percent; two columns the value
  bool spark{false};            // the sparkline's symbol
};

// "SHOP.TO" too long is "SHOP", never "SHOP.T".
inline std::string market_sym_text(const std::string &sym, size_t max) {
  if (sym.size() <= max)
    return sym;
  const size_t cut = sym.find_first_of(".=");
  return sym.substr(0, cut != std::string::npos && cut > 0 && cut <= max ? cut : max);
}

// alt: two-column rows show percent, not price.
inline MarketRow market_row(const MarketSymbol &s, const MarketQuote &q, const MarketGeom &g, int64_t now, bool alt) {
  MarketRow r;
  r.sym = market_sym_text(s.sym, g.sym.chars());
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
  int8_t active{-1};  // the charted row on this page
  int8_t slot{-1};    // the charted symbol, 0 based
  bool logo{false};   // it has one
  std::string name, move;  // beside its logo, four panels
  uint32_t move_color{kMarketFlat};
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
    if (i == spark_slot)
      f.active = (int8_t) f.n;
    f.rows[f.n] = market_row(b.syms[i], q[i], g, now, alt);
    f.rows[f.n++].spark = i == spark_slot;
  }
  if (spark_slot >= 0) {
    const MarketSymbol &s = b.syms[spark_slot];
    const MarketQuote &sq = q[spark_slot];
    f.line = market_spark(sq, g.spark_x, g.spark_y, g.spark_w, g.spark_h);
    f.spark = f.line.n >= 2;
    f.slot = (int8_t) spark_slot;
    f.logo = market_has_logo(s.sym, s.kind);
    if (g.name_at.w > 0) {
      const size_t room = (size_t) (g.name_at.w / kMarketCharPx);
      f.name = market_sym_text(s.sym, room);
      if (sq.valid && sq.prev != 0) {
        f.move = market_percent_text(market_percent(sq), room);
        f.move_color = market_stale(sq, (MarketKind) s.kind, now) ? kMarketStale : market_color(market_dir(sq));
      }
    }
  }
  if (!f.spark) {
    f.active = f.slot = -1;
    f.logo = false;
    f.name.clear();
    f.move.clear();
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

}
